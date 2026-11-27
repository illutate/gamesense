#include "skeet/client/enhanced_features.hpp"
#include "skeet/config.hpp"
#include "skeet/debug_log.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/esp/effects_config.hpp"
#include "skeet/esp/other_config.hpp"
#include "skeet/events/game_events.hpp"
#include "skeet/events/hit_log.hpp"
#include "skeet/events/registry.hpp"
#include "skeet/misc/config.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/render_queue.hpp"
#include "skeet/script/cvar_api.hpp"
#include "skeet/script/globals_api.hpp"
#include "skeet/text_metrics.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {

constexpr std::uint32_t player_spawn_hash = 0xC90B4171u;
constexpr std::uint32_t item_purchase_hash = 0xD6CAA6EDu;
constexpr std::uint32_t userid_field_hash = 0x8361150Eu;
constexpr std::uint32_t attacker_field_hash = 0x455B4DBFu;
constexpr std::uint32_t weapon_field_hash = 0xFDF97DB0u;
constexpr std::uint32_t damage_field_hash = 0x32130FCFu;
constexpr std::uint32_t health_field_hash = 0xF952B07Eu;

std::atomic<EnhancedFeaturesRuntime*> active_instance{nullptr};
std::atomic<bool> unsafe_dt_warning_flag{false};

void spawn_cb(void* owner, events::GameEventReader& e, events::EventPlayerIndices& p) {
    static_cast<EnhancedFeaturesRuntime*>(owner)->on_player_spawn(e, p);
}
void hurt_cb(void* owner, events::GameEventReader& e, events::EventPlayerIndices& p) {
    static_cast<EnhancedFeaturesRuntime*>(owner)->on_player_hurt(e, p);
}
void purchase_cb(void* owner, events::GameEventReader& e, events::EventPlayerIndices& p) {
    static_cast<EnhancedFeaturesRuntime*>(owner)->on_item_purchase(e, p);
}
void round_prestart_cb(void* owner, events::GameEventReader&, events::EventPlayerIndices&) {
    static_cast<EnhancedFeaturesRuntime*>(owner)->on_round_prestart();
}

template <class T> T read_mem(const void* ptr, std::uint32_t offset) noexcept {
    T out{};
    if (ptr)
        std::memcpy(&out, static_cast<const std::byte*>(ptr) + offset, sizeof(out));
    return out;
}

float mono_seconds() noexcept {
    static const double inv_freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f.QuadPart > 0 ? (1.0 / static_cast<double>(f.QuadPart)) : 0.0;
    }();
    static const std::int64_t base_qpc = [] {
        LARGE_INTEGER c{};
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<float>(static_cast<double>(now.QuadPart - base_qpc) * inv_freq);
}

float ease_sine_in_out(float t, float b, float c, float d) noexcept {
    if (d <= 0.0f)
        return b + c;
    const float u = std::clamp(t / d, 0.0f, 1.0f);
    constexpr float pi = 3.14159265358979323846f;
    return -c * 0.5f * (std::cos(pi * u) - 1.0f) + b;
}

std::uint32_t with_alpha(std::uint32_t abgr, std::uint8_t a) noexcept {
    return (abgr & 0x00FFFFFFu) | (static_cast<std::uint32_t>(a) << 24);
}

std::u16string utf8_to_u16(std::string_view text) {
    if (text.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                      nullptr, 0);
    if (n <= 0)
        return {};
    std::u16string out(static_cast<std::size_t>(n), u'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        reinterpret_cast<wchar_t*>(out.data()), n);
    return out;
}

std::string query_default_username() {
    wchar_t wname[128]{};
    DWORD bytes = sizeof(wname);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"LastGameNameUsed",
                     RRF_RT_REG_SZ, nullptr, wname, &bytes) == ERROR_SUCCESS &&
        wname[0] != L'\0') {
        char utf8[256]{};
        const int converted =
            WideCharToMultiByte(CP_UTF8, 0, wname, -1, utf8, sizeof(utf8), nullptr, nullptr);
        if (converted > 1)
            return std::string(utf8);
    }
    char win_user[128]{};
    DWORD size = sizeof(win_user);
    if (GetUserNameA(win_user, &size) && win_user[0] != '\0')
        return std::string(win_user);
    return "Player";
}

void* find_cvar(void* cvar, const char* name) noexcept {
    if (!cvar || !name)
        return nullptr;
    return engine::invoke<void*>(cvar, 15, name);
}
void set_cvar_int(void* var, std::int32_t value) noexcept {
    if (var)
        engine::invoke<void>(var, 16, value);
}
void set_cvar_float(void* var, float value) noexcept {
    if (var)
        engine::invoke<void>(var, 15, value);
}
void set_cvar_string(void* var, const char* value) noexcept {
    if (var && value)
        engine::invoke<void>(var, 14, value);
}

void draw_fade_banner(ui::RenderQueue& queue, std::int32_t x, std::int32_t y, std::int32_t w,
                      std::int32_t h, float alpha_scale) {
    const std::int32_t fade_width = std::clamp(w / 3, 1, 30);
    const std::int32_t mid_w = std::max(0, w - fade_width * 2);
    const std::uint8_t bg_a =
        static_cast<std::uint8_t>(std::clamp(90.0f * alpha_scale, 0.0f, 255.0f));
    const std::uint8_t line_a =
        static_cast<std::uint8_t>(std::clamp(150.0f * alpha_scale, 0.0f, 255.0f));
    const std::uint32_t bg = with_alpha(0x00050505u, bg_a);
    const std::uint32_t bg_clear = 0x00050505u;
    const std::uint32_t border = with_alpha(0x00050505u, line_a);
    const std::uint32_t border_clear = 0x00050505u;
    const std::int32_t body_h = std::max(1, h - 1);

    if (mid_w > 0)
        queue.rectangle({x + fade_width, y, mid_w, body_h, bg});
    queue.horizontal_gradient({x, y, fade_width, body_h, bg_clear, bg});
    queue.horizontal_gradient({x + w - fade_width, y, fade_width, body_h, bg, bg_clear});

    queue.horizontal_gradient({x, y - 1, fade_width, 1, border_clear, border});
    if (mid_w > 0)
        queue.rectangle({x + fade_width, y - 1, mid_w, 1, border});
    queue.horizontal_gradient({x + w - fade_width, y - 1, fade_width, 1, border, border_clear});

    queue.horizontal_gradient({x, y + h - 1, fade_width, 1, border_clear, border});
    if (mid_w > 0)
        queue.rectangle({x + fade_width, y + h - 1, mid_w, 1, border});
    queue.horizontal_gradient(
        {x + w - fade_width, y + h - 1, fade_width, 1, border, border_clear});
}

const char* hitgroup_label(std::int32_t hg) noexcept {
    static constexpr const char* table[]{
        "generic", "head", "chest", "stomach", "left arm", "right arm", "left leg", "right leg",
        "neck",    "?",    "gear"};
    return (hg >= 0 && hg < std::int32_t(std::size(table))) ? table[hg] : "?";
}

std::string format_weapon_purchase_name(std::string_view raw) {
    if (raw.starts_with("weapon_"))
        raw.remove_prefix(7);
    if (raw == "ssg08")
        return "SSG08";
    if (raw == "awp")
        return "AWP";
    if (raw == "scar20")
        return "SCAR20";
    if (raw == "g3sg1")
        return "G3SG1";
    if (raw == "deagle")
        return "Deagle";
    if (raw == "revolver")
        return "Revolver";
    if (raw == "flashbang")
        return "Flash";
    if (raw == "hegrenade")
        return "Grenade";
    if (raw == "smokegrenade")
        return "Smoke";
    if (raw == "molotov" || raw == "incgrenade")
        return "Molotov";
    if (raw == "decoy")
        return "Decoy";
    if (raw == "vest")
        return "Kevlar";
    if (raw == "vesthelm")
        return "Helmet";
    if (raw == "defuser")
        return "Defuser";
    if (raw == "taser")
        return "Taser";
    return std::string(raw);
}

struct BannerPart {
    std::u16string text;
    bool colored = false;
};

struct BannerMeta {
    enum class Kind { none, damage, gun_hurt, purchase } kind = Kind::none;
    std::int32_t entity = 0;
    std::string weapon_key;
    std::int32_t total_damage = 0;
    float updated = 0.0f;
    struct PurchaseItem {
        std::string label;
        std::int32_t count = 1;
    };
    std::vector<std::pair<std::string, PurchaseItem>> purchases;
};

struct HitlogBanner {
    std::vector<BannerPart> parts;
    std::vector<std::int32_t> part_widths;
    std::int32_t total_text_w = 0;
    std::int32_t measured_dpi_idx = -1;
    std::uint32_t accent_abgr = 0xFFFFFFFFu;
    float time = 0.0f;
    float reveal_from = 0.0f;
    float reveal_started = 0.0f;
    BannerMeta meta{};
};

struct AimFireRecord {
    std::int32_t target = 0;
    std::int32_t hitgroup = 0;
    std::int32_t damage = 0;
    std::int32_t hitchance = 0;
    std::int32_t history_ticks = 0;
};

struct AnimState {
    float value = 0.0f;
    float from = 0.0f;
    float target = 0.0f;
    float started = 0.0f;
    bool initialized = false;
};

float step_anim(AnimState& s, bool visible, float duration, float now) noexcept {
    const float desired = visible ? 1.0f : 0.0f;
    if (!s.initialized) {
        s.initialized = true;
        s.value = s.from = s.target = desired;
        s.started = now;
        return s.value;
    }
    if (s.target != desired) {
        s.from = s.value;
        s.target = desired;
        s.started = now;
    }
    const float elapsed = std::min(std::max(now - s.started, 0.0f), duration);
    s.value = ease_sine_in_out(elapsed, s.from, s.target - s.from, duration);
    return s.value;
}

} // namespace

struct EnhancedFeaturesRuntime::Impl {
    EnhancedFeatureBindings b;
    std::uint32_t team_offset = 0;
    std::uint32_t health_offset = 0;
    std::uint32_t velocity_offset = 0;
    std::uint32_t kills_offset = 0;
    std::uint32_t deaths_offset = 0;
    std::uint32_t scoped_offset = 0;
    std::uint32_t resume_zoom_offset = 0;
    std::uint32_t active_weapon_offset = 0;
    std::uint32_t zoom_level_offset = 0;
    std::uint32_t round_prestart_hash = 0;

    std::int32_t last_aspect_percent = -1;
    std::int32_t last_screen_w = 0;
    std::int32_t last_screen_h = 0;
    std::int32_t last_tp_distance = -1;
    bool vm_applied_once = false;
    bool last_vm_enabled = false;
    std::int32_t last_vm_fov = 680;
    std::int32_t last_vm_x = 25;
    std::int32_t last_vm_y = 0;
    std::int32_t last_vm_z = -15;
    float custom_scope_alpha = 0.0f;
    float last_scope_time = 0.0f;
    std::string cached_username;
    std::u16string cached_username_u16;
    std::int32_t cached_username_w = 0;
    std::int32_t cached_username_dpi = -1;
    float username_poll_timer = -10.0f;
    std::int32_t last_local_idx = -1;

    void* cvar_aspect = nullptr;
    void* cvar_tp_dist = nullptr;
    void* cvar_vm_fov = nullptr;
    void* cvar_vm_x = nullptr;
    void* cvar_vm_y = nullptr;
    void* cvar_vm_z = nullptr;

    std::atomic<bool> has_banners{false};
    std::mutex mutex;
    std::deque<HitlogBanner> banners;
    std::vector<HitlogBanner> banner_snapshot;
    std::int32_t cached_banner_text_h = 0;
    std::int32_t cached_banner_dpi = -1;
    std::unordered_map<std::int32_t, AimFireRecord> fire_cache;
    struct NativeHitStamp {
        std::int32_t target = 0;
        std::int32_t hitgroup = 0;
        std::int32_t damage = 0;
        float time = 0.0f;
    };
    std::deque<NativeHitStamp> recent_native_hits;

    // Watermark metrics & reusable caches (avoid per-frame heap/measure_text overhead)
    std::uint32_t fps_accum = 0;
    std::int32_t fps_value = 0;
    std::int32_t fps_prev = 0;
    float fps_timer = 0.0f;
    std::array<float, 64> frametimes{};
    std::size_t frametime_idx = 0;
    std::int32_t var_value = 0;
    std::int32_t var_prev = 0;
    float var_timer = 0.0f;
    std::int32_t ping_value = 0;
    std::int32_t ping_prev = 0;
    float ping_timer = 0.0f;
    AnimState var_anim{};
    AnimState speed_anim{};
    bool connected_state = false;

    std::int32_t wm_dpi = -1;
    std::int32_t wm_game_w = 0;
    std::int32_t wm_sense_w = 0;
    std::int32_t wm_fps_lbl_w = 0;
    std::int32_t wm_var_lbl_w = 0;
    std::int32_t wm_ping_lbl_w = 0;
    std::int32_t wm_speed_lbl_w = 0;
    std::int32_t wm_kd_lbl_w = 0;
    std::int32_t wm_text_h = 0;

    std::int32_t wm_fps_val = -1;
    std::u16string wm_fps_u16;
    std::int32_t wm_fps_w = 0;

    std::int32_t wm_var_val = -1;
    std::u16string wm_var_u16;
    std::int32_t wm_var_w = 0;

    std::int32_t wm_ping_val = -1;
    std::u16string wm_ping_u16;
    std::int32_t wm_ping_w = 0;

    std::int32_t wm_speed_val = -1;
    std::u16string wm_speed_u16;
    std::int32_t wm_speed_w = 0;

    std::string wm_server_raw;
    std::u16string wm_server_u16;
    std::int32_t wm_server_w = 0;
    std::int32_t wm_server_dpi = -1;

    std::int32_t wm_kd_kills = -1;
    std::int32_t wm_kd_deaths = -1;
    std::u16string wm_kd_u16;
    std::int32_t wm_kd_w = 0;

    WORD wm_clock_sec = 0xFFFFu;
    float wm_clock_timer = -10.0f;
    std::u16string wm_clock_u16;
    std::int32_t wm_clock_w = 0;

    struct WmElement {
        std::u16string_view text;
        std::uint32_t color = 0xDCFFFFFFu;
        std::uint32_t flags = 8;
        std::int32_t off_x = 0;
        std::int32_t off_y = 0;
        std::int32_t pad_after = 0;
        std::int32_t full_w = 0;
        std::int32_t clip_w = 0; // <=0 means full width
    };
    std::vector<WmElement> wm_elems;

    explicit Impl(EnhancedFeatureBindings bindings) : b(bindings) {
        const auto& g = b.offsets.globals;
        team_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x434688B0u, 0x4346A518u, 0x8B5586F6u, 0)));
        health_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x4346A438u, 0x4346701Cu, 0x9BB80163u, 0)));
        velocity_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x4346A8E4u, 0x434688CCu, 0x36375C23u, 0)));
        kills_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x43467168u, 0x43468DF4u, 0x6D5B1E34u, 0)));
        deaths_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x4346A6A0u, 0x43467DB8u, 0x0896CC62u, 0)));
        scoped_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x43468DA4u, 0x434684ECu, 0xCACAEECCu, 0)));
        resume_zoom_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x43467A90u, 0x434671FCu, 0xC07D1150u, 0)));
        active_weapon_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x43467A74u, 0x434678C0u, 0xCD812654u, 0)));
        zoom_level_offset = static_cast<std::uint32_t>(
            std::ptrdiff_t(engine::DecodedOffset(g, 0x43467D20u, 0x43468E8Cu, 0xD6A88AA5u, 0)));
        round_prestart_hash = events::event_hash("round_prestart");
        cached_username = query_default_username();
        cached_username_u16 = utf8_to_u16(cached_username);
        banner_snapshot.reserve(12);
        wm_elems.reserve(24);
        fps_timer = var_timer = ping_timer = last_scope_time = mono_seconds();
    }

    std::string resolve_player_name(std::int32_t ent_index) const {
        if (!b.engine || ent_index < 1 || ent_index > 64)
            return "?";
        std::array<std::byte, 0x158> info{};
        if (engine::invoke<bool>(b.engine, 8, ent_index, info.data())) {
            char buf[64]{};
            std::memcpy(buf, info.data() + 0x10, 63);
            for (char& ch : buf) {
                if (!ch)
                    break;
                if (ch == '\a' || ch == '\t' || ch == '\n' || ch == '\r')
                    ch = ' ';
            }
            if (buf[0])
                return std::string(buf);
        }
        return std::to_string(ent_index);
    }

    void push_banner(std::vector<BannerPart> parts, std::uint32_t abgr, BannerMeta meta = {}) {
        const float now = mono_seconds();
        if (banners.size() >= 12)
            banners.pop_front();
        banners.push_back(
            {std::move(parts), {}, 0, -1, abgr, now, 0.0f, now, std::move(meta)});
        has_banners.store(true, std::memory_order_release);
    }

    void update_console_filter(bool enable) {
        if (!b.cvar)
            return;
        if (auto* f_en = find_cvar(b.cvar, "con_filter_enable"))
            set_cvar_int(f_en, enable ? 1 : 0);
        if (auto* f_txt = find_cvar(b.cvar, "con_filter_text"))
            set_cvar_string(f_txt, enable ? "___" : "");
        DEBUG_LOG("CONSOLE_FILTER", "applied con_filter_enable=%d", enable ? 1 : 0);
    }

    void update_aspect_ratio(std::int32_t sw, std::int32_t sh) {
        const std::int32_t pct = std::clamp(b.effects.force_aspect_ratio.value, 0, 200);
        if (pct == last_aspect_percent && sw == last_screen_w && sh == last_screen_h)
            return;
        last_aspect_percent = pct;
        last_screen_w = sw;
        last_screen_h = sh;
        if (!b.cvar)
            return;
        if (!cvar_aspect)
            cvar_aspect = find_cvar(b.cvar, "r_aspectratio");
        if (!cvar_aspect)
            return;
        const float val =
            (pct == 100 || sh <= 0) ? 0.0f : (static_cast<float>(sw) * (2.0f - pct * 0.01f)) / sh;
        set_cvar_float(cvar_aspect, val);
        DEBUG_LOG("ASPECT", "force_aspect_ratio=%d%% (%dx%d) -> r_aspectratio=%.4f", pct, sw, sh,
                  double(val));
    }

    void update_thirdperson_distance() {
        const std::int32_t dist = std::clamp(b.effects.third_person_distance.value, 30, 200);
        const bool connected = b.engine && engine::invoke<bool>(b.engine, 26);
        if (!connected) {
            last_tp_distance = -1;
            return;
        }
        if (dist == last_tp_distance)
            return;
        last_tp_distance = dist;
        if (!cvar_tp_dist && b.cvar)
            cvar_tp_dist = find_cvar(b.cvar, "cam_idealdist");
        if (cvar_tp_dist)
            set_cvar_float(cvar_tp_dist, static_cast<float>(dist));
    }

    void apply_viewmodel_cvars(float fov, float x, float y, float z) noexcept {
        if (!b.cvar)
            return;
        const auto set_raw = [&](void*& cached, const char* name, float val) {
            if (!cached)
                cached = find_cvar(b.cvar, name);
            if (cached && !engine::invoke<bool>(cached, 1))
                script::set_cvar_raw_float(cached, val);
        };
        set_raw(cvar_vm_fov, "viewmodel_fov", fov);
        set_raw(cvar_vm_x, "viewmodel_offset_x", x);
        set_raw(cvar_vm_y, "viewmodel_offset_y", y);
        set_raw(cvar_vm_z, "viewmodel_offset_z", z);
    }

    void update_viewmodel() {
        const bool enabled = b.other_esp.viewmodel.enabled.value != 0;
        const auto fov = b.other_esp.viewmodel.fov.value;
        const auto vx = b.other_esp.viewmodel.offset_x.value;
        const auto vy = b.other_esp.viewmodel.offset_y.value;
        const auto vz = b.other_esp.viewmodel.offset_z.value;
        if (!enabled) {
            if (last_vm_enabled && vm_applied_once)
                apply_viewmodel_cvars(68.0f, 2.5f, 0.0f, -1.5f);
            last_vm_enabled = false;
            return;
        }
        if (!last_vm_enabled || fov != last_vm_fov || vx != last_vm_x || vy != last_vm_y ||
            vz != last_vm_z) {
            last_vm_enabled = true;
            vm_applied_once = true;
            last_vm_fov = fov;
            last_vm_x = vx;
            last_vm_y = vy;
            last_vm_z = vz;
            apply_viewmodel_cvars(static_cast<float>(fov) * 0.1f, static_cast<float>(vx) * 0.1f,
                                  static_cast<float>(vy) * 0.1f, static_cast<float>(vz) * 0.1f);
        }
    }

    void render_custom_scope(ui::RenderQueue& queue, std::int32_t sw, std::int32_t sh) {
        if (!b.other_esp.overlays.custom_scope.value && custom_scope_alpha <= 0.001f) {
            custom_scope_alpha = 0.0f;
            last_scope_time = 0.0f;
            return;
        }
        const float now = mono_seconds();
        const float dt =
            (last_scope_time > 0.0f) ? std::clamp(now - last_scope_time, 0.0f, 0.1f) : 0.0f;
        last_scope_time = now;

        if (!b.other_esp.overlays.custom_scope.value) {
            custom_scope_alpha = 0.0f;
            return;
        }

        bool act = false;
        if (b.engine && engine::invoke<bool>(b.engine, 26) && b.local_player && b.entity_list &&
            read_mem<std::int32_t>(b.local_player, health_offset) > 0) {
            const auto handle = read_mem<std::int32_t>(b.local_player, active_weapon_offset);
            void* weapon =
                (handle != -1) ? engine::invoke<void*>(b.entity_list, 4, handle) : nullptr;
            if (weapon && engine::invoke<std::int32_t>(weapon, 455) == 5) {
                const auto zoom_level = read_mem<std::int32_t>(weapon, zoom_level_offset);
                const bool scoped = read_mem<std::uint8_t>(b.local_player, scoped_offset) != 0;
                const bool resume_zoom =
                    read_mem<std::uint8_t>(b.local_player, resume_zoom_offset) != 0;
                act = zoom_level > 0 && scoped && !resume_zoom;
            }
        }

        const std::int32_t speed = b.other_esp.overlays.custom_scope_speed.value;
        const float step = (speed > 3) ? (dt * static_cast<float>(speed)) : 1.0f;
        custom_scope_alpha = std::clamp(custom_scope_alpha + (act ? step : -step), 0.0f, 1.0f);
        if (custom_scope_alpha <= 0.001f)
            return;

        const float scale_y = static_cast<float>(sh) / 1080.0f;
        const std::int32_t offset = static_cast<std::int32_t>(
            std::round(static_cast<float>(b.other_esp.overlays.custom_scope_offset.value) * scale_y));
        const std::int32_t initial_pos = static_cast<std::int32_t>(std::round(
            static_cast<float>(b.other_esp.overlays.custom_scope_position.value) * scale_y));
        const std::int32_t length = initial_pos - offset;
        if (length <= 0)
            return;

        const std::uint32_t base = b.other_esp.overlays.custom_scope_color.value;
        const std::uint8_t a = static_cast<std::uint8_t>(
            std::clamp(static_cast<float>(base >> 24) * custom_scope_alpha, 0.0f, 255.0f));
        const std::uint32_t solid = with_alpha(base, a);
        const std::uint32_t clear = base & 0x00FFFFFFu;
        const std::int32_t cx = sw / 2;
        const std::int32_t cy = sh / 2;

        queue.horizontal_gradient({cx - initial_pos + 2, cy, length, 1, clear, solid});
        queue.horizontal_gradient({cx + offset, cy, length, 1, solid, clear});
        queue.vertical_gradient({cx, cy - initial_pos + 2, 1, length, clear, solid});
        queue.vertical_gradient({cx, cy + offset, 1, length, solid, clear});
    }

    void render_unsafe_dt_warning(ui::RenderQueue& queue, std::int32_t sw, std::int32_t sh,
                                  float scale) const {
        if (!b.hitlogs.unsafe_dt_recharge.value ||
            !unsafe_dt_warning_flag.load(std::memory_order_acquire))
            return;
        const std::uint32_t clr = b.hitlogs.unsafe_dt_recharge_color.value;
        const std::int32_t y =
            sh / 2 + static_cast<std::int32_t>(std::round(46.0f * scale));
        queue.text(sw / 2, y, clr, 0x10 | 0x20 | 4, 0, u"UNSAFE DT RECHARGE");
    }

    void render_hitlogs(ui::RenderQueue& queue, float scale, std::int32_t dpi_idx) {
        if (!b.hitlogs.enabled.value || !has_banners.load(std::memory_order_acquire))
            return;

        const float now = mono_seconds();
        banner_snapshot.clear();
        {
            std::lock_guard lock(mutex);
            while (!banners.empty() && now - banners.front().time > 7.15f)
                banners.pop_front();
            // Prune stale middle items cleanly.
            std::erase_if(banners, [&](const HitlogBanner& item) {
                return (now - item.time) > 7.15f;
            });
            has_banners.store(!banners.empty(), std::memory_order_release);
            if (banners.empty())
                return;
            for (auto& item : banners) {
                if (item.measured_dpi_idx != dpi_idx) {
                    item.part_widths.clear();
                    item.part_widths.reserve(item.parts.size());
                    std::int32_t total_w = 0;
                    for (const auto& part : item.parts) {
                        const std::int32_t pw =
                            part.text.empty() ? 0 : ui::measure_text(queue, part.text, 8).x;
                        item.part_widths.push_back(pw);
                        total_w += pw;
                    }
                    item.total_text_w = total_w;
                    item.measured_dpi_idx = dpi_idx;
                }
            }
            banner_snapshot.assign(banners.begin(), banners.end());
        }

        if (cached_banner_dpi != dpi_idx) {
            cached_banner_text_h = ui::measure_text(queue, u"0", 8).y;
            cached_banner_dpi = dpi_idx;
        }
        const float factor = (dpi_idx == 0) ? 0.4f : 0.5f;
        const std::int32_t padding = static_cast<std::int32_t>(std::round(16.0f * scale));
        const std::int32_t height = static_cast<std::int32_t>(std::ceil(20.0f * scale));
        const std::int32_t text_h = cached_banner_text_h;
        const std::int32_t base_x = 10;
        std::int32_t base_y = 10;

        for (auto it = banner_snapshot.rbegin(); it != banner_snapshot.rend(); ++it) {
            const auto& item = *it;
            const std::int32_t text_w = item.total_text_w;
            const float rev_elapsed = std::max(now - item.reveal_started, 0.0f);
            const float visible_w_f =
                rev_elapsed >= 0.18f
                    ? static_cast<float>(text_w)
                    : ease_sine_in_out(rev_elapsed, item.reveal_from,
                                       static_cast<float>(text_w) - item.reveal_from, 0.18f);
            const std::int32_t visible_w =
                static_cast<std::int32_t>(std::clamp(std::ceil(visible_w_f), 0.0f, 4096.0f));

            const float age = std::max(now - item.time, 0.0f);
            const float alpha_norm =
                age <= 7.0f ? 1.0f : ease_sine_in_out(std::min(age - 7.0f, 0.15f), 1.0f, -1.0f, 0.15f);
            if (alpha_norm <= 0.01f)
                continue;
            const std::uint8_t text_a =
                static_cast<std::uint8_t>(std::clamp(alpha_norm * 255.0f, 0.0f, 255.0f));
            const std::uint8_t soft_a =
                static_cast<std::uint8_t>(std::clamp(alpha_norm * 220.0f, 0.0f, 255.0f));

            const std::int32_t box_w = text_w + padding * 2;
            const std::int32_t text_y =
                base_y + static_cast<std::int32_t>(std::floor(static_cast<float>(height - text_h) * factor));

            draw_fade_banner(queue, base_x, base_y, box_w, height, alpha_norm);

            queue.set_bounds({base_x + padding, base_y - 2, std::max(1, visible_w + 2), height + 4});
            queue.reset_bounds(true);
            std::int32_t pen_x = base_x + padding;
            for (std::size_t idx = 0; idx < item.parts.size(); ++idx) {
                const auto& part = item.parts[idx];
                if (!part.text.empty()) {
                    const std::uint32_t clr = part.colored
                                                  ? with_alpha(item.accent_abgr, text_a)
                                                  : with_alpha(0x00FFFFFFu, soft_a);
                    queue.text(pen_x, text_y, clr, 8, 0, part.text);
                }
                pen_x += item.part_widths[idx];
            }
            queue.reset_bounds(false);
            base_y += height + 6;
        }
    }

    static std::uint32_t stat_color(std::int32_t val, std::int32_t prev, std::int32_t warn,
                                    std::int32_t danger, std::int32_t spike_w,
                                    std::int32_t spike_d, bool inverse,
                                    std::uint32_t accent) noexcept {
        constexpr std::uint32_t warning_abgr = 0xFF3DD5FFu; // RGB(255,213,61)
        constexpr std::uint32_t danger_abgr = 0xFF5050FFu; // RGB(255,80,80)
        if (inverse) {
            if (val <= danger)
                return danger_abgr;
            if (val <= warn)
                return warning_abgr;
            if (prev > 0) {
                const std::int32_t drop = prev - val;
                if (drop >= spike_d)
                    return danger_abgr;
                if (drop >= spike_w)
                    return warning_abgr;
            }
        } else {
            if (val >= danger)
                return danger_abgr;
            if (val >= warn)
                return warning_abgr;
            if (prev > 0) {
                const std::int32_t spike = val - prev;
                if (spike >= spike_d)
                    return danger_abgr;
                if (spike >= spike_w)
                    return warning_abgr;
            }
        }
        return with_alpha(accent, 255);
    }

    void render_watermark(ui::RenderQueue& queue, std::int32_t screen_w, std::int32_t screen_h,
                          float scale, std::int32_t dpi_idx) {
        (void)screen_h;
        const float now = mono_seconds();
        const std::uint32_t accent = with_alpha(b.menu_color, 255);
        constexpr std::uint32_t white_text = 0xDCFFFFFFu;
        const std::int32_t small_y = (dpi_idx == 1) ? 1 : (dpi_idx == 2 || dpi_idx == 3) ? 4 : (dpi_idx >= 4) ? 5 : 2;

        if (wm_dpi != dpi_idx) {
            wm_dpi = dpi_idx;
            wm_game_w = ui::measure_text(queue, u"game", 8).x;
            wm_sense_w = ui::measure_text(queue, u"sense", 8).x;
            wm_fps_lbl_w = ui::measure_text(queue, u"FPS", 4).x;
            wm_var_lbl_w = ui::measure_text(queue, u"VAR", 4).x;
            wm_ping_lbl_w = ui::measure_text(queue, u"PING", 4).x;
            wm_speed_lbl_w = ui::measure_text(queue, u"SPEED", 4).x;
            wm_kd_lbl_w = ui::measure_text(queue, u"K/D", 4).x;
            wm_text_h = ui::measure_text(queue, u"0", 8).y;
            wm_fps_val = -1;
            wm_var_val = -1;
            wm_ping_val = -1;
            wm_speed_val = -1;
            wm_kd_kills = -1;
            wm_clock_sec = 0xFFFFu;
        }
        if (cached_username_dpi != dpi_idx) {
            cached_username_w =
                cached_username_u16.empty() ? 0 : ui::measure_text(queue, cached_username_u16, 8).x;
            cached_username_dpi = dpi_idx;
        }

        // Connection & net channel queries
        bool is_connected = false;
        bool is_local = false;
        const char* raw_addr = nullptr;
        float latency_sec = 0.0f;
        std::int32_t local_idx = 0;
        if (b.engine && engine::invoke<bool>(b.engine, 26)) {
            if (auto* chan = engine::invoke<void*>(b.engine, 78)) {
                const char* addr = engine::invoke<const char*>(chan, 1);
                if (addr && *addr) {
                    is_connected = true;
                    if (std::strcmp(addr, "loopback") == 0)
                        is_local = true;
                    else
                        raw_addr = addr;
                    latency_sec = std::max(0.0f, engine::invoke<float>(chan, 10, 0));
                }
            }
            local_idx = engine::invoke<std::int32_t>(b.engine, 12);
            if (local_idx >= 1 && local_idx <= 64 &&
                (local_idx != last_local_idx || now >= username_poll_timer + 2.0f ||
                 now < username_poll_timer)) {
                last_local_idx = local_idx;
                username_poll_timer = now;
                std::string live_name = resolve_player_name(local_idx);
                if (live_name != "?" && live_name != cached_username) {
                    cached_username = std::move(live_name);
                    cached_username_u16 = utf8_to_u16(cached_username);
                    cached_username_w = cached_username_u16.empty()
                                            ? 0
                                            : ui::measure_text(queue, cached_username_u16, 8).x;
                    cached_username_dpi = dpi_idx;
                }
            }
        }
        connected_state = is_connected;

        const std::string_view server_sv =
            !is_connected ? std::string_view{}
                          : (is_local ? std::string_view("Local Server")
                                      : std::string_view(raw_addr ? raw_addr : ""));
        if (!server_sv.empty() && (server_sv != wm_server_raw || wm_server_dpi != dpi_idx)) {
            wm_server_raw.assign(server_sv);
            wm_server_u16 =
                is_local ? std::u16string(u"Local Server") : utf8_to_u16(wm_server_raw);
            wm_server_w = ui::measure_text(queue, wm_server_u16, 8).x;
            wm_server_dpi = dpi_idx;
        }

        // FPS update (1s bucket)
        ++fps_accum;
        if (now >= fps_timer + 1.0f) {
            fps_timer = now;
            fps_prev = fps_value;
            fps_value = static_cast<std::int32_t>(fps_accum);
            fps_accum = 0;
        }

        // Frametime variance update
        if (b.globals && b.globals->absolute_frame_time > 0.0f) {
            frametimes[frametime_idx] = b.globals->absolute_frame_time;
            frametime_idx = (frametime_idx + 1) % frametimes.size();
        }
        if (now >= var_timer + 1.0f) {
            std::int32_t current_var = 0;
            std::array<float, 64> sample{};
            std::size_t count = 0;
            float acc = 0.0f;
            std::size_t idx = frametime_idx;
            for (std::size_t i = 0; i < 64; ++i) {
                idx = (idx == 0) ? 63 : idx - 1;
                const float ft = frametimes[idx];
                if (ft <= 0.0f)
                    break;
                sample[count++] = ft;
                acc += ft;
                if (acc >= 0.5f)
                    break;
            }
            if (count >= 2) {
                const float mean = acc / static_cast<float>(count);
                float variance = 0.0f;
                for (std::size_t i = 0; i < count; ++i) {
                    const float d = sample[i] - mean;
                    variance += d * d;
                }
                const float dev_ms = std::sqrt(variance / static_cast<float>(count)) * 1000.0f;
                if (dev_ms >= 0.5f)
                    current_var = static_cast<std::int32_t>(std::floor(dev_ms + 0.5f));
            }
            var_timer = now;
            var_prev = var_value;
            var_value = current_var;
        }

        // Ping update
        const std::int32_t current_ping = static_cast<std::int32_t>(std::floor(latency_sec * 1000.0f + 0.5f));
        if (now >= ping_timer + 1.0f) {
            ping_timer = now;
            ping_prev = ping_value;
            ping_value = current_ping;
        }

        // Build watermark elements
        wm_elems.clear();
        const std::int32_t brand_gap = static_cast<std::int32_t>(std::round(20.0f * scale));
        const std::int32_t stat_gap = static_cast<std::int32_t>(std::round(15.0f * scale));
        auto push = [&](std::u16string_view txt, std::uint32_t clr, std::int32_t w,
                        bool small = false, std::int32_t ox = 0, std::int32_t oy = 0,
                        std::int32_t clip_w = 0) {
            wm_elems.push_back({txt, clr, small ? 4u : 8u, ox, oy, 0, w, clip_w});
        };
        auto sep = [&](float frac = 1.0f) {
            if (!wm_elems.empty())
                wm_elems.back().pad_after +=
                    static_cast<std::int32_t>(std::round(static_cast<float>(stat_gap) * frac));
        };

        // Logo: game + sense (accent)
        push(u"game", white_text, wm_game_w);
        push(u"sense", accent, wm_sense_w);
        wm_elems.back().pad_after = brand_gap;

        // Username
        push(cached_username_u16, white_text, cached_username_w);

        // FPS
        if (wm_fps_val != fps_value) {
            wm_fps_val = fps_value;
            wm_fps_u16 = utf8_to_u16(std::to_string(fps_value));
            wm_fps_w = ui::measure_text(queue, wm_fps_u16, 8).x;
        }
        sep();
        push(wm_fps_u16, stat_color(fps_value, fps_prev, 64, 45, 20, 40, true, accent), wm_fps_w);
        push(u"FPS", white_text, wm_fps_lbl_w, true, 1, small_y);

        // Frametime variance (animated reveal when var > 0)
        {
            const float prog = step_anim(var_anim, var_value > 0, 0.15f, now);
            if (prog > 0.0f) {
                if (wm_var_val != var_value) {
                    wm_var_val = var_value;
                    wm_var_u16 = utf8_to_u16(std::to_string(var_value));
                    wm_var_w = ui::measure_text(queue, wm_var_u16, 8).x;
                }
                const std::int32_t vw = wm_var_w;
                const std::int32_t lw = wm_var_lbl_w;
                const float anim_w = static_cast<float>(vw + 1 + lw) * prog;
                if (anim_w > 1.0f) {
                    sep(prog);
                    const std::int32_t cw = static_cast<std::int32_t>(std::ceil(anim_w));
                    const std::int32_t vw_clip = prog >= 0.99f ? 0 : std::min(vw, cw) + 2;
                    const std::int32_t lw_clip =
                        prog >= 0.99f ? 0 : std::max(0, cw - vw - 1);
                    push(wm_var_u16, stat_color(var_value, var_prev, 3, 6, 2, 4, false, accent),
                         vw, false, 0, 0, vw_clip);
                    if (prog >= 0.99f || lw_clip > 1)
                        push(u"VAR", white_text, lw, true, 1, small_y, lw_clip > 0 ? lw_clip + 2 : 0);
                }
            }
        }

        // Ping
        if (is_connected && !is_local) {
            if (wm_ping_val != ping_value) {
                wm_ping_val = ping_value;
                wm_ping_u16 = utf8_to_u16(std::to_string(ping_value));
                wm_ping_w = ui::measure_text(queue, wm_ping_u16, 8).x;
            }
            sep();
            push(wm_ping_u16, stat_color(ping_value, ping_prev, 70, 120, 25, 50, false, accent),
                 wm_ping_w);
            push(u"PING", white_text, wm_ping_lbl_w, true, 1, small_y);
        }

        // Server IP
        if (is_connected && !wm_server_u16.empty()) {
            sep();
            push(wm_server_u16, white_text, wm_server_w);
        }

        // Speed
        if (is_connected && b.local_player) {
            const float vx = read_mem<float>(b.local_player, velocity_offset);
            const float vy = read_mem<float>(b.local_player, velocity_offset + 4);
            float spd = std::sqrt(vx * vx + vy * vy);
            if (spd < 1.2f)
                spd = 0.0f;
            const std::int32_t spd_i = static_cast<std::int32_t>(spd);
            const float prog = step_anim(speed_anim, spd_i != 0, 0.15f, now);
            if (prog > 0.0f) {
                if (wm_speed_val != spd_i) {
                    wm_speed_val = spd_i;
                    wm_speed_u16 = utf8_to_u16(std::to_string(spd_i));
                    wm_speed_w = ui::measure_text(queue, wm_speed_u16, 8).x;
                }
                const std::int32_t sw = wm_speed_w;
                const std::int32_t lw = wm_speed_lbl_w;
                const float anim_w = static_cast<float>(sw + 1 + lw) * prog;
                if (anim_w > 1.0f) {
                    sep(prog);
                    const std::int32_t cw = static_cast<std::int32_t>(std::ceil(anim_w));
                    const std::int32_t sw_clip = prog >= 0.99f ? 0 : std::min(sw, cw) + 2;
                    const std::int32_t lw_clip = prog >= 0.99f ? 0 : std::max(0, cw - sw - 1);
                    push(wm_speed_u16, white_text, sw, false, 0, 0, sw_clip);
                    if (prog >= 0.99f || lw_clip > 1)
                        push(u"SPEED", white_text, lw, true, 1, small_y, lw_clip > 0 ? lw_clip + 2 : 0);
                }
            }
        }

        // K/D Ratio
        if (is_connected && b.local_player && b.player_resource && *b.player_resource &&
            local_idx >= 1 && local_idx <= 64) {
            const auto* res = *b.player_resource;
            const auto kills =
                read_mem<std::int32_t>(res, kills_offset + static_cast<std::uint32_t>(local_idx) * 4u);
            const auto deaths =
                read_mem<std::int32_t>(res, deaths_offset + static_cast<std::uint32_t>(local_idx) * 4u);
            if (kills > 0 || deaths > 0) {
                if (wm_kd_kills != kills || wm_kd_deaths != deaths) {
                    wm_kd_kills = kills;
                    wm_kd_deaths = deaths;
                    const float kd =
                        deaths == 0 ? static_cast<float>(kills)
                                    : static_cast<float>(kills) / static_cast<float>(deaths);
                    char buf[32]{};
                    std::snprintf(buf, sizeof(buf), "%.1f", double(kd));
                    wm_kd_u16 = utf8_to_u16(buf);
                    wm_kd_w = ui::measure_text(queue, wm_kd_u16, 8).x;
                }
                sep();
                push(wm_kd_u16, white_text, wm_kd_w);
                push(u"K/D", white_text, wm_kd_lbl_w, true, 1, small_y);
            }
        }

        // System clock (24h HH:MM:SS, throttled polling)
        if (now >= wm_clock_timer + 0.5f || now < wm_clock_timer || wm_clock_sec == 0xFFFFu) {
            wm_clock_timer = now;
            SYSTEMTIME st{};
            GetLocalTime(&st);
            if (st.wSecond != wm_clock_sec || wm_clock_u16.empty()) {
                wm_clock_sec = st.wSecond;
                char clk[16]{};
                std::snprintf(clk, sizeof(clk), "%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
                wm_clock_u16 = utf8_to_u16(clk);
                wm_clock_w = ui::measure_text(queue, wm_clock_u16, 8).x;
            }
        }
        sep();
        push(wm_clock_u16, white_text, wm_clock_w);

        // Measure total content width
        std::int32_t content_w = 0;
        for (const auto& e : wm_elems) {
            const std::int32_t w = (e.clip_w > 0) ? std::min(e.clip_w, e.full_w) : e.full_w;
            content_w += e.off_x + w + e.pad_after;
        }
        const std::int32_t padding = static_cast<std::int32_t>(std::round(20.0f * scale));
        const std::int32_t width = content_w + padding * 2;
        const std::int32_t height = static_cast<std::int32_t>(std::ceil(20.0f * scale));
        const std::int32_t x = screen_w - static_cast<std::int32_t>(std::ceil(10.0f * scale)) - width;
        const std::int32_t y = static_cast<std::int32_t>(std::ceil(10.0f * scale));
        const std::int32_t text_h = wm_text_h;
        const float factor = (dpi_idx == 0) ? 0.4f : 0.5f;
        const std::int32_t base_text_y =
            y + static_cast<std::int32_t>(std::floor(static_cast<float>(height - text_h) * factor));

        draw_fade_banner(queue, x, y, width, height, 1.0f);

        std::int32_t pen_x = x + padding;
        for (const auto& e : wm_elems) {
            const std::int32_t w = (e.clip_w > 0) ? std::min(e.clip_w, e.full_w) : e.full_w;
            if (!e.text.empty() && w > 0) {
                queue.text(pen_x + e.off_x, base_text_y + e.off_y, e.color, e.flags, e.clip_w,
                           e.text);
            }
            pen_x += e.off_x + w + e.pad_after;
        }
    }
};

EnhancedFeaturesRuntime::EnhancedFeaturesRuntime(EnhancedFeatureBindings bindings)
    : impl_(new Impl(bindings)) {
    misc::initialize_auto_buy_config(impl_->b.configuration, impl_->b.auto_buy);
    esp::initialize_viewmodel_config(impl_->b.configuration.descriptors,
                                     impl_->b.other_esp.viewmodel);

    // Subscribe game events safely (disconnected flag false = adds to listener list; connected
    // flag triggers engine dirty notification via add(listener)).
    auto& reg = impl_->b.events;
    reg.add({player_spawn_hash, this, spawn_cb});
    reg.add({events::player_hurt_hash, this, hurt_cb});
    reg.add({item_purchase_hash, this, purchase_cb});
    reg.add({impl_->round_prestart_hash, this, round_prestart_cb});

    impl_->update_console_filter(true);
    active_instance.store(this, std::memory_order_release);
    DEBUG_LOG("ENHANCED",
              "EnhancedFeaturesRuntime initialized (team=0x%X hp=0x%X vel=0x%X kills=0x%X deaths=0x%X)",
              impl_->team_offset, impl_->health_offset, impl_->velocity_offset,
              impl_->kills_offset, impl_->deaths_offset);
}

EnhancedFeaturesRuntime::~EnhancedFeaturesRuntime() {
    if (active_instance.load(std::memory_order_acquire) == this)
        active_instance.store(nullptr, std::memory_order_release);
    unsafe_dt_warning_flag.store(false, std::memory_order_release);
    auto& reg = impl_->b.events;
    reg.remove({player_spawn_hash, this, spawn_cb}, false);
    reg.remove({events::player_hurt_hash, this, hurt_cb}, false);
    reg.remove({item_purchase_hash, this, purchase_cb}, false);
    reg.remove({impl_->round_prestart_hash, this, round_prestart_cb}, false);

    if (impl_->b.cvar) {
        if (auto* aspect = find_cvar(impl_->b.cvar, "r_aspectratio"))
            set_cvar_float(aspect, 0.0f);
        if (impl_->last_tp_distance != -1) {
            if (auto* dist = find_cvar(impl_->b.cvar, "cam_idealdist"))
                set_cvar_float(dist, 150.0f);
        }
        if (impl_->vm_applied_once)
            impl_->apply_viewmodel_cvars(68.0f, 2.5f, 0.0f, -1.5f);
        impl_->update_console_filter(false);
    }
    DEBUG_LOG("ENHANCED", "EnhancedFeaturesRuntime shut down cleanly");
    delete impl_;
}

void EnhancedFeaturesRuntime::on_player_spawn(events::GameEventReader& event,
                                              events::EventPlayerIndices& players) {
    if (!impl_->b.auto_buy.enabled.value || !impl_->b.engine)
        return;
    events::FieldValue uid{};
    if (event.read(userid_field_hash, events::FieldType::integer, uid))
        return;
    if (players.player_for_user_id(uid.integer) != players.local_player_index())
        return;

    std::string cmd;
    switch (impl_->b.auto_buy.primary.value) {
    case 1:
        cmd += "buy awp; ";
        break;
    case 2:
        cmd += "buy scar20; buy g3sg1; ";
        break;
    case 3:
        cmd += "buy ssg08; ";
        break;
    case 4:
        cmd += "buy negev; ";
        break;
    default:
        break;
    }
    switch (impl_->b.auto_buy.secondary.value) {
    case 1:
        cmd += "buy deagle; ";
        break;
    case 2:
        cmd += "buy elite; ";
        break;
    case 3:
        cmd += "buy fn57; ";
        break;
    default:
        break;
    }
    const std::uint32_t gear = static_cast<std::uint32_t>(impl_->b.auto_buy.gear.value);
    if (gear & 1u)
        cmd += "buy vest; ";
    if (gear & 2u)
        cmd += "buy vesthelm; ";
    if (gear & 4u)
        cmd += "buy defuser; ";
    if (gear & 8u)
        cmd += "buy hegrenade; ";
    if (gear & 16u)
        cmd += "buy incgrenade; ";
    if (gear & 32u)
        cmd += "buy smokegrenade; ";
    if (gear & 64u)
        cmd += "buy taser; ";

    if (cmd.empty())
        return;
    cmd += "use weapon_knife;";
    engine::invoke<void>(impl_->b.engine, 108, cmd.c_str());
    DEBUG_LOG("AUTOBUY", "player_spawn executed: %s", cmd.c_str());
}

void EnhancedFeaturesRuntime::on_player_hurt(events::GameEventReader& event,
                                             events::EventPlayerIndices& players) {
    if (!impl_->b.hitlogs.enabled.value || !(impl_->b.hitlogs.events.value & 1))
        return;
    events::FieldValue attacker{}, userid{}, dmg{}, health{}, weapon{}, hitgroup{};
    if (event.read(attacker_field_hash, events::FieldType::integer, attacker) ||
        event.read(userid_field_hash, events::FieldType::integer, userid) ||
        event.read(damage_field_hash, events::FieldType::integer, dmg) ||
        event.read(health_field_hash, events::FieldType::integer, health) ||
        event.read(weapon_field_hash, events::FieldType::string, weapon))
        return;
    if (players.player_for_user_id(attacker.integer) != players.local_player_index())
        return;
    const std::int32_t target = players.player_for_user_id(userid.integer);
    if (target < 1 || target > 64 || target == players.local_player_index() || !weapon.string)
        return;
    static const std::uint32_t hitgroup_field_hash = events::event_hash("hitgroup");
    (void)event.read(hitgroup_field_hash, events::FieldType::integer, hitgroup);

    const std::int32_t dmg_i = static_cast<std::int32_t>(dmg.integer);
    const std::int32_t hp_i = std::max<std::int32_t>(0, static_cast<std::int32_t>(health.integer));
    const std::int32_t hg_i = static_cast<std::int32_t>(hitgroup.integer);
    std::string_view w(weapon.string);
    bool is_firearm = false;
    std::string key;
    std::string label;
    if (w == "inferno" || w == "molotov" || w == "incgrenade") {
        key = "molotov";
        label = "Molotov";
    } else if (w == "hegrenade") {
        key = "hegrenade";
        label = "Grenade";
    } else if (w.starts_with("knife")) {
        key = "knife";
        label = "Knife";
    } else if (w == "flashbang" || w == "smokegrenade" || w == "decoy" || dmg_i <= 0) {
        return;
    } else {
        is_firearm = true;
    }

    const std::string name = impl_->resolve_player_name(target);
    const float now = mono_seconds();

    std::lock_guard lock(impl_->mutex);
    if (is_firearm) {
        for (auto it = impl_->recent_native_hits.begin(); it != impl_->recent_native_hits.end();
             ++it) {
            if (it->target == target && it->damage == dmg_i && (now - it->time) <= 0.5f) {
                impl_->recent_native_hits.erase(it);
                return;
            }
        }
        const char* hg = hitgroup_label(hg_i);
        BannerMeta meta{};
        meta.kind = BannerMeta::Kind::gun_hurt;
        meta.entity = target;
        meta.total_damage = dmg_i;
        meta.updated = now;
        DEBUG_LOG("HITLOG", "HURT %s (%s) dmg=%d hp=%d weapon=%s", name.c_str(), hg, dmg_i, hp_i,
                  weapon.string);
        impl_->push_banner({{u"Hit ", false},
                            {utf8_to_u16(name), true},
                            {u" in the ", false},
                            {utf8_to_u16(hg), true},
                            {u" for ", false},
                            {utf8_to_u16(std::to_string(dmg_i)), true},
                            {u" damage (", false},
                            {utf8_to_u16(std::to_string(hp_i)), true},
                            {u" health remaining)", false}},
                           impl_->b.hitlogs.hit_color.value, std::move(meta));
        return;
    }
    for (auto it = impl_->banners.rbegin(); it != impl_->banners.rend(); ++it) {
        if (it->meta.kind == BannerMeta::Kind::damage && it->meta.entity == target &&
            it->meta.weapon_key == key && (now - it->meta.updated) <= 1.5f) {
            it->meta.total_damage += dmg_i;
            it->meta.updated = now;
            it->parts = {
                {u"Hit ", false},
                {utf8_to_u16(name), true},
                {u" with ", false},
                {utf8_to_u16(label), true},
                {u" for ", false},
                {utf8_to_u16(std::to_string(it->meta.total_damage)), true},
                {u" damage (", false},
                {utf8_to_u16(std::to_string(hp_i)), true},
                {u" health remaining)", false},
            };
            it->measured_dpi_idx = -1;
            it->time = now;
            return;
        }
    }
    BannerMeta meta{};
    meta.kind = BannerMeta::Kind::damage;
    meta.entity = target;
    meta.weapon_key = key;
    meta.total_damage = dmg_i;
    meta.updated = now;
    impl_->push_banner({{u"Hit ", false},
                        {utf8_to_u16(name), true},
                        {u" with ", false},
                        {utf8_to_u16(label), true},
                        {u" for ", false},
                        {utf8_to_u16(std::to_string(dmg_i)), true},
                        {u" damage (", false},
                        {utf8_to_u16(std::to_string(hp_i)), true},
                        {u" health remaining)", false}},
                       impl_->b.hitlogs.hit_color.value, std::move(meta));
}

void EnhancedFeaturesRuntime::on_item_purchase(events::GameEventReader& event,
                                               events::EventPlayerIndices& players) {
    if (!impl_->b.hitlogs.enabled.value || !(impl_->b.hitlogs.events.value & 4))
        return;
    events::FieldValue uid{}, weapon{};
    if (event.read(userid_field_hash, events::FieldType::integer, uid) ||
        event.read(weapon_field_hash, events::FieldType::string, weapon) || !weapon.string)
        return;
    if (std::strcmp(weapon.string, "weapon_unknown") == 0)
        return;
    const std::int32_t idx = players.player_for_user_id(uid.integer);
    if (idx < 1 || idx > 64 || idx == players.local_player_index() || !impl_->b.entity_list ||
        !impl_->b.local_player)
        return;
    void* ent = engine::invoke<void*>(impl_->b.entity_list, 3, idx);
    if (!ent)
        return;
    const auto local_team = read_mem<std::int32_t>(impl_->b.local_player, impl_->team_offset);
    const auto ent_team = read_mem<std::int32_t>(ent, impl_->team_offset);
    if (local_team == ent_team)
        return;

    std::string_view key_sv(weapon.string);
    if (key_sv.starts_with("weapon_"))
        key_sv.remove_prefix(7);
    const std::string key(key_sv);
    const std::string player_name = impl_->resolve_player_name(idx);
    const float now = mono_seconds();

    auto build_parts = [&](const std::string& pname, const BannerMeta& m) {
        std::vector<BannerPart> parts{{utf8_to_u16(pname), true}, {u" bought ", false}};
        for (std::size_t i = 0; i < m.purchases.size(); ++i) {
            if (i > 0)
                parts.push_back({u", ", false});
            std::string entry = m.purchases[i].second.label;
            if (m.purchases[i].second.count > 1)
                entry += "[x" + std::to_string(m.purchases[i].second.count) + "]";
            parts.push_back({utf8_to_u16(entry), true});
        }
        return parts;
    };

    std::lock_guard lock(impl_->mutex);
    for (auto it = impl_->banners.rbegin(); it != impl_->banners.rend(); ++it) {
        if (it->meta.kind == BannerMeta::Kind::purchase && it->meta.entity == idx) {
            bool updated = false;
            for (auto& p : it->meta.purchases) {
                if (p.first == key) {
                    ++p.second.count;
                    updated = true;
                    break;
                }
            }
            if (!updated)
                it->meta.purchases.push_back({key, {format_weapon_purchase_name(key), 1}});
            it->parts = build_parts(player_name, it->meta);
            it->measured_dpi_idx = -1;
            it->time = now;
            it->reveal_started = now;
            DEBUG_LOG("HITLOG", "purchase %s bought %s", player_name.c_str(), key.c_str());
            return;
        }
    }
    BannerMeta meta{};
    meta.kind = BannerMeta::Kind::purchase;
    meta.entity = idx;
    meta.purchases.push_back({key, {format_weapon_purchase_name(key), 1}});
    auto parts = build_parts(player_name, meta);
    impl_->push_banner(std::move(parts), impl_->b.hitlogs.purchase_color.value, std::move(meta));
    DEBUG_LOG("HITLOG", "purchase %s bought %s", player_name.c_str(), key.c_str());
}

void EnhancedFeaturesRuntime::on_round_prestart() {
    std::lock_guard lock(impl_->mutex);
    impl_->banners.clear();
    impl_->has_banners.store(false, std::memory_order_release);
    impl_->fire_cache.clear();
    impl_->recent_native_hits.clear();
}

void EnhancedFeaturesRuntime::render_hud(ui::RenderQueue& queue) {
    std::int32_t sw = 0, sh = 0;
    if (impl_->b.engine)
        engine::invoke<void, std::int32_t&, std::int32_t&>(impl_->b.engine, 5, sw, sh);
    if (sw <= 0 || sh <= 0) {
        const auto pt = queue.screen();
        sw = pt.x;
        sh = pt.y;
    }
    impl_->update_aspect_ratio(sw, sh);
    impl_->update_thirdperson_distance();
    impl_->update_viewmodel();

    static constexpr float dpi_scales[]{1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
    const std::int32_t idx = std::clamp(impl_->b.dpi_scale_index, 0, 4);
    const float scale = dpi_scales[idx];

    if (sw > 0 && sh > 0)
        impl_->render_custom_scope(queue, sw, sh);
    impl_->render_hitlogs(queue, scale, idx);
    if (sw > 0 && sh > 0) {
        impl_->render_unsafe_dt_warning(queue, sw, sh, scale);
        impl_->render_watermark(queue, sw, sh, scale, idx);
    }
}

void record_native_aim_fire(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                            std::int32_t damage, float hit_chance,
                            std::int32_t shot_tick) noexcept {
    auto* rt = active_instance.load(std::memory_order_acquire);
    if (!rt)
        return;
    auto& impl = *rt->impl_;
    if (!impl.b.hitlogs.enabled.value || !(impl.b.hitlogs.events.value & 3))
        return;
    const std::int32_t tick_count = impl.b.globals ? impl.b.globals->tick_count : shot_tick;
    const std::int32_t hc = static_cast<std::int32_t>(std::floor(hit_chance * 100.0f));
    std::lock_guard lock(impl.mutex);
    impl.fire_cache[id] = {target, hitgroup, damage, hc, tick_count - shot_tick};
}

void record_native_aim_hit(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                           std::int32_t damage) noexcept {
    auto* rt = active_instance.load(std::memory_order_acquire);
    if (!rt)
        return;
    auto& impl = *rt->impl_;
    if (!impl.b.hitlogs.enabled.value || !(impl.b.hitlogs.events.value & 1))
        return;

    AimFireRecord fire{target, hitgroup, damage, 0, 0};
    bool had_fire = false;
    {
        std::lock_guard lock(impl.mutex);
        if (const auto it = impl.fire_cache.find(id); it != impl.fire_cache.end()) {
            fire = it->second;
            impl.fire_cache.erase(it);
            had_fire = true;
        }
    }

    const std::string name = impl.resolve_player_name(target);
    const char* hg = hitgroup_label(hitgroup);
    const char* aimed_hg = hitgroup_label(fire.hitgroup);
    std::int32_t remaining_hp = 0;
    if (impl.b.entity_list && target >= 1 && target <= 64) {
        if (void* ent = engine::invoke<void*>(impl.b.entity_list, 3, target))
            remaining_hp = std::max(0, read_mem<std::int32_t>(ent, impl.health_offset));
    }

    std::vector<BannerPart> parts{
        {u"Hit ", false},
        {utf8_to_u16(name), true},
        {u" in the ", false},
        {utf8_to_u16(hg), true},
        {u" for ", false},
        {utf8_to_u16(std::to_string(damage)), true},
        {u" damage (", false},
        {utf8_to_u16(std::to_string(remaining_hp)), true},
        {u" health remaining, ", false},
        {utf8_to_u16(std::to_string(fire.hitchance)), true},
        {u"% hitchance, ", false},
        {utf8_to_u16(std::to_string(fire.history_ticks)), true},
        {u"t history)", false},
    };
    if (had_fire && (fire.damage != damage || fire.hitgroup != hitgroup)) {
        parts.push_back({u" - ", false});
        parts.push_back({u"mismatch", true});
        parts.push_back({u" - (wanted: ", false});
        parts.push_back({utf8_to_u16(std::to_string(fire.damage)), true});
        parts.push_back({u" damage, ", false});
        parts.push_back({utf8_to_u16(aimed_hg), true});
        parts.push_back({u")", false});
    }

    DEBUG_LOG("HITLOG", "HIT %s (%s) dmg=%d hp=%d hc=%d%% hist=%dt", name.c_str(), hg, damage,
              remaining_hp, fire.hitchance, fire.history_ticks);

    const float now = mono_seconds();
    std::lock_guard lock(impl.mutex);
    for (auto it = impl.banners.rbegin(); it != impl.banners.rend(); ++it) {
        if (it->meta.kind == BannerMeta::Kind::gun_hurt && it->meta.entity == target &&
            it->meta.total_damage == damage && (now - it->meta.updated) <= 0.6f) {
            it->parts = std::move(parts);
            it->measured_dpi_idx = -1;
            it->accent_abgr = impl.b.hitlogs.hit_color.value;
            it->time = now;
            it->reveal_from = 0.0f;
            it->reveal_started = now;
            it->meta = {};
            return;
        }
    }
    while (!impl.recent_native_hits.empty() && (now - impl.recent_native_hits.front().time) > 1.0f)
        impl.recent_native_hits.pop_front();
    impl.recent_native_hits.push_back({target, hitgroup, damage, now});
    impl.push_banner(std::move(parts), impl.b.hitlogs.hit_color.value);
}

void record_native_aim_miss(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                            std::uint32_t reason) noexcept {
    auto* rt = active_instance.load(std::memory_order_acquire);
    if (!rt)
        return;
    auto& impl = *rt->impl_;
    if (!impl.b.hitlogs.enabled.value || !(impl.b.hitlogs.events.value & 2))
        return;

    AimFireRecord fire{target, hitgroup, 0, 0, 0};
    bool had_fire = false;
    {
        std::lock_guard lock(impl.mutex);
        if (const auto it = impl.fire_cache.find(id); it != impl.fire_cache.end()) {
            fire = it->second;
            impl.fire_cache.erase(it);
            had_fire = true;
        }
    }

    static constexpr const char* reasons[]{"?", "spread", "prediction error", "death",
                                           "unregistered"};
    const char* reason_str = reason < std::size(reasons) ? reasons[reason] : "?";
    const std::string name = impl.resolve_player_name(target);
    const char* hg = hitgroup_label(hitgroup);
    const char* aimed_hg = hitgroup_label(fire.hitgroup);

    std::vector<BannerPart> parts{
        {u"Missed ", false},
        {utf8_to_u16(name), true},
        {u" in the ", false},
        {utf8_to_u16(hg), true},
        {u" due to ", false},
        {utf8_to_u16(reason_str), true},
        {u" (wanted: ", false},
        {utf8_to_u16(std::to_string(fire.damage)), true},
        {u" damage, ", false},
        {utf8_to_u16(std::to_string(fire.hitchance)), true},
        {u"% hitchance, ", false},
        {utf8_to_u16(std::to_string(fire.history_ticks)), true},
        {u"t history)", false},
    };
    if (had_fire && fire.hitgroup != hitgroup) {
        parts.push_back({u" - ", false});
        parts.push_back({u"mismatch", true});
        parts.push_back({u" - (wanted: ", false});
        parts.push_back({utf8_to_u16(std::to_string(fire.damage)), true});
        parts.push_back({u" damage, ", false});
        parts.push_back({utf8_to_u16(aimed_hg), true});
        parts.push_back({u")", false});
    }

    DEBUG_LOG("HITLOG", "MISS %s (%s) reason=%s wanted_dmg=%d hc=%d%% hist=%dt", name.c_str(), hg,
              reason_str, fire.damage, fire.hitchance, fire.history_ticks);

    std::lock_guard lock(impl.mutex);
    impl.push_banner(std::move(parts), impl.b.hitlogs.miss_color.value);
}

void set_unsafe_dt_recharge_warning(bool active) noexcept {
    unsafe_dt_warning_flag.store(active, std::memory_order_release);
}

void render_native_hud_overlays(ui::RenderQueue& queue) noexcept {
    if (auto* rt = active_instance.load(std::memory_order_acquire))
        rt->render_hud(queue);
}

} // namespace skeet::client
