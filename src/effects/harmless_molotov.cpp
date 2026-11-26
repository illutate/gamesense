#include "skeet/effects/harmless_molotov.hpp"
#include "skeet/events/game_events.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <emmintrin.h>

namespace skeet::effects {
namespace {
template <class T> T read(const void* p, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(p) + offset, sizeof value);
    return value;
}
template <class T> void write(void* p, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(p) + offset, &value, sizeof value);
}
const void* parent(const void* p) {
    for (;;) {
        const auto next = read<const void*>(p, 0x1c);
        if (next == p)
            return p;
        p = next;
    }
}
std::int32_t integer(const void* p) {
    p = parent(p);
    return std::bit_cast<std::int32_t>(read<std::uint32_t>(p, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(p)));
}
float floating(const void* p) {
    p = parent(p);
    return std::bit_cast<float>(read<std::uint32_t>(p, 0x2c) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(p)));
}
void** original(void* receiver) {
    const auto table = *static_cast<void***>(receiver);
    return static_cast<engine::VtableHookMetadata*>(table[-2])->original_table;
}
std::array<float, 3> to_hsv(const std::array<float, 3>& rgb) { //433EDB18
    const auto high = maximum_number(maximum_number(rgb[0], rgb[1]), rgb[2]);
    const auto low = minimum_number(minimum_number(rgb[0], rgb[1]), rgb[2]);
    const auto delta = high - low;
    const auto saturation = high == 0.0f ? 0.0f : delta / high;
    if (saturation == 0.0f)
        return {-1.0f, saturation, high};
    float hue;
    if (rgb[0] == high)
        hue = (rgb[1] - rgb[2]) / delta;
    else if (rgb[1] == high)
        hue = (rgb[2] - rgb[0]) / delta + 2.0f;
    else
        hue = (rgb[0] - rgb[1]) / delta + 4.0f;
    hue *= 60.0f;
    if (hue < 0.0f)
        hue += 360.0f;
    return {hue, saturation, high};
}
void from_hsv(const std::array<float, 3>& hsv, std::array<float, 3>& rgb) { //43406FDD
    const auto s = hsv[1], v = hsv[2];
    if (s == 0.0f) {
        rgb = {v, v, v};
        return;
    }
    const auto h = (hsv[0] == 360.0f ? 0.0f : hsv[0]) / 60.0f;
    const auto sector = _mm_cvttss_si32(_mm_set_ss(h));
    const auto f = h - static_cast<float>(sector);
    const auto low = (1.0f - s) * v, falling = (1.0f - s * f) * v,
               rising = (1.0f - (1.0f - f) * s) * v;
    switch (sector) {
    case 0:
        rgb = {v, rising, low};
        break;
    case 1:
        rgb = {falling, v, low};
        break;
    case 2:
        rgb = {low, v, rising};
        break;
    case 3:
        rgb = {low, falling, v};
        break;
    case 4:
        rgb = {rising, low, v};
        break;
    case 5:
        rgb = {v, low, falling};
        break;
    }
}
bool teammates(void* first, void* second, const rage::TargetBindings& b) { //433F93D2
    if (!first || !second)
        return false;
    if (first == second)
        return true;
    if (integer(b.game_type) == 6) {
        const auto a = read<std::int32_t>(first, std::uint32_t(b.offsets.survival_team));
        const auto c = read<std::int32_t>(second, std::uint32_t(b.offsets.survival_team));
        return a != -1 && c != -1 && a == c;
    }
    const auto a = read<std::int32_t>(first, std::uint32_t(b.offsets.team));
    return (a == 2 || a == 3) && a == read<std::int32_t>(second, std::uint32_t(b.offsets.team)) &&
           integer(b.teammates_are_enemies) == 0;
}
HarmlessMolotovRuntime** active_slot = nullptr;
engine::VtableHookRegistry* entity_hooks =
    nullptr; //same global hook registry outlives the effect owner
std::uint32_t destroy_particle_primary(engine::VtableHookRegistry& hooks, void* receiver,
                                       std::uintptr_t flags) {
    auto* bytes = static_cast<std::byte*>(receiver);
    engine::uninstall_vtable_hook(hooks, bytes + 4);
    engine::uninstall_vtable_hook(hooks, bytes + 8);
    engine::uninstall_vtable_hook(hooks, receiver);
    return engine::invoke<std::uint32_t>(receiver, 0, flags);
}
std::uint32_t destroy_particle_secondary(engine::VtableHookRegistry& hooks, void* receiver,
                                         std::uintptr_t flags) {
    auto* bytes = static_cast<std::byte*>(receiver);
    engine::uninstall_vtable_hook(hooks, receiver);
    engine::uninstall_vtable_hook(hooks, bytes + 4);
    engine::uninstall_vtable_hook(hooks, bytes - 4);
    return engine::invoke<std::uint32_t>(receiver, 0, flags);
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_MOLOTOV_ENTRY __fastcall
#else
#define SKEET_MOLOTOV_ENTRY
#endif
std::uint32_t SKEET_MOLOTOV_ENTRY render_entry(void* receiver, void*, std::uintptr_t a,
                                               std::uintptr_t b) {
    if (auto* runtime = *active_slot)
        return runtime->render(receiver, a, b);
    return reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uintptr_t, std::uintptr_t>>(
        original(receiver)[9])(receiver, a, b);
}
std::uint32_t SKEET_MOLOTOV_ENTRY create_entry(void* receiver, void*, const char* name,
                                               void* created) {
    if (auto* runtime = *active_slot)
        return runtime->created(receiver, name, created);
    return reinterpret_cast<engine::MemberFunction<std::uint32_t, const char*, void*>>(
        original(receiver)[110])(receiver, name, created);
}
std::uint32_t SKEET_MOLOTOV_ENTRY primary_entry(void* receiver, void*, std::uintptr_t flags) {
    return destroy_particle_primary(*entity_hooks, receiver, flags);
}
std::uint32_t SKEET_MOLOTOV_ENTRY secondary_entry(void* receiver, void*, std::uintptr_t flags) {
    return destroy_particle_secondary(*entity_hooks, receiver, flags);
}
#undef SKEET_MOLOTOV_ENTRY
}
HarmlessMolotovState::HarmlessMolotovState(bool initialize) {
    if (initialize)
        initialize_native_storage();
}
void HarmlessMolotovState::initialize_native_storage() {
    definition_hashes = {0xCE18A28F, 0x084260DE, 0x347C6739, 0x9C63D7D8, 0x91410D5F,
                         0xD3DD5603, 0xB96369ED, 0xC9490DC3, 0x20EFCF5C, 0x0A3CC8AF};
    colors.reserve(1024);
    alphas.reserve(1024);
}
void HarmlessMolotovState::release_native_storage() noexcept {
    std::vector<ParticleAlphaBackup>().swap(alphas);
    std::vector<ParticleColorBackup>().swap(colors);
    std::vector<std::uint32_t>().swap(definition_hashes);
}
void HarmlessMolotovRuntime::apply_collection(void* collection) {
    std::uint32_t hash = 0;
    if (const auto owner = read<void*>(collection, 0x48))
        if (const auto definition = read<void*>(owner, 0x50))
            hash = events::event_hash(engine::invoke<const char*>(definition, 0));
    const bool change_color =
        std::find(state.definition_hashes.begin(), state.definition_hashes.end(), hash) !=
        state.definition_hashes.end();
    const bool change_alpha = state.alpha < 1.0f;
    const auto count = read<std::int32_t>(collection, 0x30);
    if ((change_color || change_alpha) && count <= read<std::int32_t>(collection, 0x50) &&
        count > 0) {
        for (std::int32_t i = 0; i < read<std::int32_t>(collection, 0x30); ++i) {
            if (change_color) {
                auto* values = read<float*>(collection, 0xf8) +
                               (i / 4 * read<std::int32_t>(collection, 0x158) + (i & 3));
                const std::array<float, 3> before{values[0], values[4], values[8]};
                state.colors.push_back({values,
                                        {std::bit_cast<std::uint32_t>(before[0]),
                                         std::bit_cast<std::uint32_t>(before[1]),
                                         std::bit_cast<std::uint32_t>(before[2])}});
                const auto hsv = to_hsv(before);
                auto amount = _mm_set_ss(float(settings.molotov_percent.value) * 0.01f);
                amount = _mm_min_ss(_mm_max_ss(amount, _mm_setzero_ps()), _mm_set_ss(1.0f));
                const auto saturation = (state.hsv[1] - hsv[1]) * _mm_cvtss_f32(amount) + hsv[1];
                std::array<float, 3> changed;
                from_hsv({state.hsv[0], saturation, hsv[2]}, changed);
                values[0] = changed[0];
                values[4] = changed[1];
                values[8] = changed[2];
            }
            if (change_alpha) {
                auto* alpha = read<float*>(collection, 0xfc) +
                              (i / 4 * read<std::int32_t>(collection, 0x15c) + (i & 3));
                state.alphas.push_back({alpha, std::bit_cast<std::uint32_t>(*alpha)});
                *alpha = state.alpha * (*alpha);
            }
        }
    }
    for (void* child = read<void*>(collection, 0x8c); child; child = read<void*>(child, 0x70))
        apply_collection(child);
}
std::uint32_t HarmlessMolotovRuntime::render(void* receiver, std::uintptr_t first,
                                             std::uintptr_t second) {
    if (settings.harmless_molotovs.value) {
        state.colors.clear();
        state.alphas.clear();
        for (unsigned i = 0; i < 3; ++i)
            state.color[i] = float(std::uint8_t(settings.harmless_molotov_color.value >> (i * 8))) *
                             0.003921568859368563f;
        state.hsv = to_hsv(state.color);
        state.alpha = float(std::uint8_t(settings.harmless_molotov_color.value >> 24)) *
                      0.003921568859368563f;
        apply_collection(static_cast<std::byte*>(receiver) + 8);
    }
    const auto result =
        reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uintptr_t, std::uintptr_t>>(
            original(receiver)[9])(receiver, first, second);
    if (settings.harmless_molotovs.value) {
        for (const auto& saved : state.colors) {
            write(saved.address, 0, saved.values[0]);
            write(saved.address, 0x10, saved.values[1]);
            write(saved.address, 0x20, saved.values[2]);
        }
        state.colors.clear();
        for (const auto& saved : state.alphas)
            write(saved.address, 0, saved.value);
        state.alphas.clear();
    }
    return result;
}
std::uint32_t HarmlessMolotovRuntime::created(void* receiver, const char* name, void* created) {
    if (settings.harmless_molotovs.value && events::event_hash(name) == 0x19E08E80u &&
        !engine::invoke<bool>(bindings.engine, 82) && !engine::invoke<bool>(bindings.engine, 93)) {
        auto* local = *bindings.local_player;
        void* owner = nullptr;
        const auto handle = read<std::uint32_t>(receiver, bindings.owner_handle);
        if (handle != UINT32_MAX) {
            auto* entity = engine::invoke<void*>(bindings.entity_list, 4, handle);
            if (entity && engine::invoke<bool>(entity, 158))
                owner = entity;
        }
        if (local && owner && local != owner &&
            teammates(local, owner, bindings.relationships.capture(local)) &&
            (integer(reinterpret_cast<void*>(
                 std::uintptr_t(bindings.encoded_friendly_fire ^ 0x211963A6u))) == 0 ||
             floating(reinterpret_cast<void*>(std::uintptr_t(
                 bindings.encoded_grenade_damage_reduction ^ 0xD5F0540Cu))) == 0.0f)) {
            auto* first = engine::invoke<void*>(static_cast<std::byte*>(created) + 8, 0);
            auto* second = engine::invoke<void*>(static_cast<std::byte*>(created) + 4, 5);
            if (first && second) {
                engine::install_vtable_hook(bindings.hook_registry, created,
                                            *bindings.entity_hooks[0]);
                engine::install_vtable_hook(bindings.hook_registry, first,
                                            *bindings.entity_hooks[1]);
                engine::install_vtable_hook(bindings.hook_registry, second,
                                            *bindings.entity_hooks[2]);
            }
        }
    }
    return reinterpret_cast<engine::MemberFunction<std::uint32_t, const char*, void*>>(
        original(receiver)[110])(receiver, name, created);
}
std::uint32_t HarmlessMolotovRuntime::destroy_primary(void* receiver, std::uintptr_t flags) {
    return destroy_particle_primary(bindings.hook_registry, receiver, flags);
}
std::uint32_t HarmlessMolotovRuntime::destroy_secondary(void* receiver, std::uintptr_t flags) {
    return destroy_particle_secondary(bindings.hook_registry, receiver, flags);
}
void HarmlessMolotovRuntime::bind_tables(engine::VtableHookMetadata& creation) {
    publish();
    bind_table(HarmlessMolotovHook::creation, creation);
    bind_table(HarmlessMolotovHook::primary, *bindings.entity_hooks[0]);
    bind_table(HarmlessMolotovHook::secondary, *bindings.entity_hooks[1]);
    bind_table(HarmlessMolotovHook::renderable, *bindings.entity_hooks[2]);
}
void HarmlessMolotovRuntime::bind_table(HarmlessMolotovHook kind,
                                        engine::VtableHookMetadata& metadata) {
    active_slot = &bindings.active;
    entity_hooks = &bindings.hook_registry;
    bind_static_table(kind, metadata);
}
void HarmlessMolotovRuntime::bind_static_table(HarmlessMolotovHook kind,
                                               engine::VtableHookMetadata& metadata) {
    switch (kind) {
    case HarmlessMolotovHook::creation:
        metadata.replacement_table[110] = reinterpret_cast<void*>(create_entry);
        break;
    case HarmlessMolotovHook::primary:
        metadata.replacement_table[0] = reinterpret_cast<void*>(primary_entry);
        break;
    case HarmlessMolotovHook::secondary:
        metadata.replacement_table[0] = reinterpret_cast<void*>(secondary_entry);
        break;
    case HarmlessMolotovHook::renderable:
        metadata.replacement_table[9] = reinterpret_cast<void*>(render_entry);
        break;
    }
}
void HarmlessMolotovRuntime::publish() noexcept {
    active_slot = &bindings.active;
    bindings.active = this;
}
void unbind_harmless_molotov_runtime() noexcept {
    *active_slot = nullptr;
}
void HarmlessMolotovRuntime::unbind() noexcept {
    unbind_harmless_molotov_runtime();
}
} // namespace skeet::effects
