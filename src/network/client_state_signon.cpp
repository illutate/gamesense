#include "skeet/network/client_state_hook_bindings.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/script/cvar_api.hpp"
#include <cstring>
#include <xmmintrin.h>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void store(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void set_integer(void* variable, std::int32_t value, std::uint32_t clear_flags) { //43400F0D
    const auto callbacks = field<std::uint32_t>(variable, 0x60);
    store(variable, 0x60, std::uint32_t(0));
    auto* const parent = field<void*>(variable, 0x1C);
    const auto flags = field<std::uint32_t>(parent, 0x14);
    const auto lower = field<std::uint32_t>(parent, 8);
    store(parent, 0x14, flags & ~clear_flags);
    auto* const current_parent = field<void*>(variable, 0x1C);
    store(current_parent, 8, field<std::uint32_t>(current_parent, 8) & ~clear_flags);
    engine::invoke<void>(variable, 16, value);
    store(parent, 8, lower);
    store(parent, 0x14, flags);
    store(variable, 0x60, callbacks);
}
void set_interval(void* variable, std::uint32_t clear_flags, bool clear_lower) {
    const auto callbacks = field<std::uint32_t>(variable, 0x60);
    store(variable, 0x60, std::uint32_t(0));
    auto* const parent = field<void*>(variable, 0x1C);
    const auto flags = field<std::uint32_t>(parent, 0x14);
    const auto lower = field<std::uint32_t>(parent, 8);
    store(parent, 0x14, flags & ~clear_flags);
    if (clear_lower) {
        auto* const current_parent = field<void*>(variable, 0x1C);
        store(current_parent, 8, field<std::uint32_t>(current_parent, 8) & ~clear_flags);
    }
    engine::invoke<void>(variable, 15, 1.0f / 64.0f);
    store(parent, 8, lower);
    store(parent, 0x14, flags);
    store(variable, 0x60, callbacks);
}
std::int32_t tick_rate(const ClientStateHooksBindings& b) {
    return _mm_cvtt_ss2si(
        _mm_add_ss(_mm_div_ss(_mm_set_ss(1.0f), _mm_set_ss(*b.interval)), _mm_set_ss(0.5f)));
}
}
bool ClientStateHooksRuntime::slot47(void* object, std::int32_t state, std::uintptr_t second,
                                     std::uintptr_t third) {
    auto& b = bindings;
    b.transition_status = 0;
    if (script::cvar_int(b.convar_43469238) != 0)
        engine::invoke<void>(b.convar_43469238, 16, std::int32_t(0));
    if (state == 2) {
        client::restore_channel_hook(b.engine_client, b.channel);
        set_integer(b.variables.at_43467fbc, 0xC0000, 0x200);
        set_integer(b.variables.at_434671ac, 64, 0x200);
        set_integer(b.variables.at_4346747c, 64, 0x200);
        set_interval(b.variables.at_4346a748, 0x200, true);
        set_integer(b.variables.at_4346a330, 2, 0x200);
        set_integer(b.variables.at_43467130, 1200, 0x200);
        b.interpolation_floor = 1.0f / 64.0f;
    } else if (state == 3) {
        set_integer(b.variables.at_434671ac, tick_rate(b), 0);
        set_integer(b.variables.at_4346747c, tick_rate(b), 0);
        set_interval(b.variables.at_4346a748, 0, false);
        b.interpolation_floor = 1.0f / 64.0f;
    }
    auto** table = *static_cast<void***>(object);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    const auto result = reinterpret_cast<
        engine::MemberFunction<bool, std::int32_t, std::uintptr_t, std::uintptr_t>>(
        metadata->original_table[47])(object, state, second, third);
    if (state == 6) {
        client::reset_startup_acknowledgements(b.acknowledgements);
        client::reset_command_state(b.command_reset);
        // These are the reconstructed primary owner factories' real source
        // vtables, shared with PostInitializationRuntime; no payload body is
        // supplied as a replacement callback.
        const auto owners = b.owners.entries();
        for (auto* owner : owners)
            engine::invoke<void>(owner, 7);
        if (!b.allow_untrusted && b.shared_player && *b.shared_player &&
            field<std::uint8_t>(*b.shared_player, b.valve_matchmaking_bf567619)) {
            // The canonical manager owns the actual registry/baseline and
            // implements hash0 ->4337ECFE ->433971CD in this exact order.
            b.presets.reset();
            b.errors.format("%s", "Attempted to use untrusted config in a valve server.");
        }
    }
    return result;
}
} //namespace skeet::network
