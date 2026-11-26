#include "skeet/effects/view.hpp"
#include "skeet/movement/command_runtime.hpp"
#include "skeet/effects/view_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/hooks.hpp"
#include <bit>
#include <cstring>
#include <limits>

namespace skeet::effects {
ViewBindings::ViewBindings(engine::NativeDataImage& data)
    : entity_list(data.native_cell<void*>(0x4346A890u)),
      hud_panels(data.native_cell<void*>(0x4346A44Cu)), input(data.native_cell<void*>(0x4346A894u)),
      prediction(data.native_cell<void*>(0x43469264u)),
      recoil_scale(data.native_cell<std::uint32_t>(0x4346A534u), 0xB739EB42u),
      suspended(&data.native_cell<std::uint8_t>(0x43468E58u)),
      calculating_view(&data.native_cell<std::uint8_t>(0x43475BD1u)),
      panel_surface(data.native_cell<void*>(0x4346712Cu)) {}
namespace {
template <class T> T read(const void* p, std::uint32_t at) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + at, sizeof(value));
    return value;
}
template <class T> void write(void* p, std::uint32_t at, T value) {
    std::memcpy(static_cast<std::byte*>(p) + at, &value, sizeof(value));
}
void* parent(void* p) {
    for (;;) {
        void* next = read<void*>(p, 0x1c);
        if (next == p)
            return p;
        p = next;
    }
}
std::int32_t find_scope_panel(void* panels, std::int32_t id) {
    const auto* name = engine::invoke<const char*>(panels, 36, id);
    std::uint32_t hash = 0x811C9DC5u;
    if (name)
        for (; *name; ++name)
            hash = (hash ^ std::uint8_t(*name)) * 0x01000193u;
    if ((hash ^ 0x92CA5DF1u) == 0x13DD15B4u)
        return id; // HudZoom
    const auto count = engine::invoke<std::int32_t>(panels, 17, id);
    for (std::int32_t child = 0; child < count; ++child) {
        const auto child_id = engine::invoke<std::int32_t>(panels, 18, id, child);
        const auto found = find_scope_panel(panels, child_id);
        if (found != -1)
            return found;
    }
    return -1;
}
}
void ViewRuntime::initialize_scope_panel() {
    // Native scope owner slot 5 is the recovered no-op 0x433CCE8E.
    const auto root = engine::invoke<std::int32_t>(interfaces.panel_surface, 1, std::int32_t(2));
    if (root != -1)
        scope_panel = find_scope_panel(interfaces.hud_panels, root);
}
void ViewRuntime::hide_scope_panel(void* player) {
    const bool hide = settings.remove_scope.value ||
                      (custom_scope_enabled != nullptr && *custom_scope_enabled != 0);
    if (!hide || scope_panel == -1)
        return;
    const auto handle = read<std::int32_t>(player, offsets.active_weapon);
    if (handle == -1)
        return;
    void* weapon = engine::invoke<void*>(interfaces.entity_list, 4, handle);
    if (weapon && engine::invoke<std::int32_t>(weapon, 455) == 5)
        engine::invoke<void>(interfaces.hud_panels, 14, scope_panel, false);
}
bool ViewRuntime::scope_visible(void* player) {
    const bool custom_active = custom_scope_enabled != nullptr && *custom_scope_enabled != 0;
    if (!settings.remove_scope.value || custom_active || scope_panel == -1 || !player ||
        read<std::uint8_t>(player, offsets.life_state) ||
        !read<std::uint8_t>(player, offsets.scoped))
        return false;
    const auto handle = read<std::int32_t>(player, offsets.active_weapon);
    if (handle == -1)
        return false;
    void* weapon = engine::invoke<void*>(interfaces.entity_list, 4, handle);
    return weapon && engine::invoke<std::int32_t>(weapon, 455) == 5;
}
namespace {
void scope_rectangles(ui::RenderQueue& queue, Point viewport) {
    queue.rectangle({viewport.x / 2, 0, 1, viewport.y, 0xFF000000u});
    queue.rectangle({0, viewport.y / 2, viewport.x, 1, 0xFF000000u});
}
}
void ViewRuntime::draw_scope(ui::RenderQueue& queue, Point viewport, void* player) {
    if (scope_visible(player))
        scope_rectangles(queue, viewport);
}
void ViewRuntime::draw_scope_live(ui::RenderQueue& queue, void* player) {
    if (!scope_visible(player))
        return;
    scope_rectangles(queue, queue.screen());
}
void ViewRuntime::instant_scope(void* player) {
    if (!player || *interfaces.suspended || !settings.instant_scope.value)
        return;
    const auto fov = read<std::uint32_t>(player, offsets.fov);
    if (fov != read<std::uint32_t>(player, offsets.scope_transition_35b5b2ec))
        write(player, offsets.scope_transition_35b5b2ec, fov);
}
SavedRecoilAngles ViewRuntime::begin_recoil_angles(void* player) {
    SavedRecoilAngles state;
    if (!settings.visual_recoil.value || *interfaces.calculating_view ||
        engine::invoke<void*>(interfaces.input, 32, std::int32_t(-1)) ||
        read<std::uint8_t>(player, offsets.life_state) ||
        read<std::uint8_t>(interfaces.prediction, 8))
        return state;
    state.shake = static_cast<std::byte*>(player) + offsets.shake_angles_5ae32976;
    state.previous_shake = read<std::array<std::uint32_t, 3>>(state.shake, 0);
    write(state.shake, 0, std::array<std::uint32_t, 3>{});
    if (settings.visual_recoil.value == 2) {
        state.all = static_cast<std::byte*>(player) + offsets.recoil_angles_1bae1168;
        state.previous_all = read<std::array<std::uint32_t, 3>>(state.all, 0);
        write(state.all, 0, std::array<std::uint32_t, 3>{});
    }
    return state;
}
void ViewRuntime::restore_recoil_angles(const SavedRecoilAngles& state) {
    if (state.shake)
        write(state.shake, 0, state.previous_shake);
    if (state.all)
        write(state.all, 0, state.previous_all);
}
const float* ViewDuckTarget::get() const noexcept {
    if (!owner_)
        return target_;
    auto* const owner = *owner_;
    return owner ? &owner->duck_peek.target : nullptr;
}
SavedViewHeight ViewRuntime::begin_view_height(void* player, ViewDuckBindings& c) {
    const auto reset = [&]() {
        c.previous_duck = std::numeric_limits<float>::max();
        return SavedViewHeight{};
    };
    const auto* const duck_target = c.duck_target.get();
    if (!duck_target || !c.enabled || read<std::uint8_t>(player, offsets.life_state))
        return reset();
    const auto duck = read<float>(player, c.duck_amount);
    bool active = (c.command_flags & 0x40u) != 0;
    if (!active)
        active = hotkey_active(c.duck_key, c.input) || (c.command_flags & 0x100u) != 0;
    if (!(active && !(c.command_flags & 2u))) {
        if (c.previous_duck == std::numeric_limits<float>::max())
            return {};
        if (!(duck > 0.0f && duck < 1.0f && c.previous_duck >= duck))
            return reset();
    }
    c.previous_duck = duck;
    const auto target = *duck_target;
    const auto square = target * target;
    const auto doubled = (square + square) * target;
    const auto interpolation = 1.0f - (square * 3.0f - doubled);
    auto* address = static_cast<std::byte*>(player) + c.view_offset + 8u;
    SavedViewHeight saved{address, read<std::uint32_t>(address, 0)};
    write(address, 0, interpolation * 18.0f + 46.0f);
    return saved;
}
void ViewRuntime::restore_view_height(const SavedViewHeight& saved) {
    if (saved.address)
        write(saved.address, 0, saved.value);
}
template <bool hooked>
void ViewRuntime::calculate_view_transaction(void* player, void** original_table,
                                             ViewDuckBindings& duck,
                                             const engine::OwnerRegistry& registry,
                                             std::uintptr_t first, std::uintptr_t second,
                                             std::uintptr_t third) {
    const auto recoil = begin_recoil_angles(player);
    SavedViewHeight height;
    if (!read<std::uint8_t>(interfaces.prediction, 8) && !*interfaces.calculating_view) {
        dispatch_player_view(registry, player, first, second, third);
        height = begin_view_height(player, duck);
    }
    if constexpr (hooked) {
        const auto table = *static_cast<void***>(player);
        original_table = static_cast<engine::VtableHookMetadata*>(table[-2])->original_table;
    }
    reinterpret_cast<engine::MemberFunction<void, std::uintptr_t, std::uintptr_t, std::uintptr_t>>(
        original_table[366])(player, first, second, third);
    restore_view_height(height);
    restore_recoil_angles(recoil);
}
void ViewRuntime::calculate_view_hook(void* player, void** original_table, ViewDuckBindings& duck,
                                      const engine::OwnerRegistry& registry, std::uintptr_t first,
                                      std::uintptr_t second, std::uintptr_t third) {
    calculate_view_transaction<false>(player, original_table, duck, registry, first, second, third);
}
void ViewRuntime::calculate_hooked_view(void* player, ViewDuckBindings& duck,
                                        const engine::OwnerRegistry& registry, std::uintptr_t first,
                                        std::uintptr_t second, std::uintptr_t third) {
    calculate_view_transaction<true>(player, nullptr, duck, registry, first, second, third);
}
template <bool hooked>
void ViewRuntime::recoil_scale_transaction(void* player, void** original_table,
                                           const std::uint32_t& encoded_recoil_scale,
                                           std::uintptr_t first, std::uintptr_t second,
                                           std::uintptr_t third) {
    float previous = std::bit_cast<float>(0x7F7FFFFFu);
    if (settings.visual_recoil.value) {
        void* variable = interfaces.recoil_scale;
        if constexpr (hooked)
            variable = reinterpret_cast<void*>(std::uintptr_t(encoded_recoil_scale ^ 0xB739EB42u));
        previous = engine::invoke<float>(variable, 12);
        variable = interfaces.recoil_scale;
        if constexpr (hooked)
            variable = reinterpret_cast<void*>(std::uintptr_t(encoded_recoil_scale ^ 0xB739EB42u));
        void* p = parent(variable);
        const auto encoded = std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
        write(p, 0x2c, encoded);
        write(p, 0x3c, encoded);
    }
    if constexpr (hooked) {
        const auto table = *static_cast<void***>(player);
        original_table = static_cast<engine::VtableHookMetadata*>(table[-2])->original_table;
    }
    reinterpret_cast<engine::MemberFunction<void, std::uintptr_t, std::uintptr_t, std::uintptr_t>>(
        original_table[250])(player, first, second, third);
    if (previous != std::bit_cast<float>(0x7F7FFFFFu)) {
        void* variable = interfaces.recoil_scale;
        if constexpr (hooked)
            variable = reinterpret_cast<void*>(std::uintptr_t(encoded_recoil_scale ^ 0xB739EB42u));
        void* p = parent(variable);
        const auto encoded = std::bit_cast<std::uint32_t>(previous) ^
                             std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
        write(p, 0x2c, encoded);
        write(p, 0x3c, encoded);
    }
}
void ViewRuntime::recoil_scale_hook(void* player, void** original_table, std::uintptr_t first,
                                    std::uintptr_t second, std::uintptr_t third) {
    recoil_scale_transaction<false>(player, original_table, 0, first, second, third);
}
void ViewRuntime::recoil_scale_hooked(void* player, const std::uint32_t& encoded_recoil_scale,
                                      std::uintptr_t first, std::uintptr_t second,
                                      std::uintptr_t third) {
    recoil_scale_transaction<true>(player, nullptr, encoded_recoil_scale, first, second, third);
}
} // namespace skeet::effects
