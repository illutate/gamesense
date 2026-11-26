#include "skeet/misc/zeus.hpp"
#include "skeet/rage/targeting_commit.hpp"
#include <cstring>
#include <xmmintrin.h>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}

// Native table 4346DCEC contains only tail forwards to the five actual engine
// methods at 43475B90. No client should-hit predicate replaces slot 0 here.
struct ZeusTraceFilter {
    void* const* vtable;
    std::array<std::uint32_t, 5> fields_04{};
    std::uint32_t mask = 0x4600400Bu;
    std::uint32_t exit_mask = 0x4600400Bu;
    float minimum_damage = 1.0f;

    BallisticFilter binding() noexcept {
        return {this, 0, mask, exit_mask, minimum_damage, BallisticFilterKind::supplied};
    }
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(ZeusTraceFilter) == 0x24);
static_assert(offsetof(ZeusTraceFilter, mask) == 0x18);
static_assert(offsetof(ZeusTraceFilter, minimum_damage) == 0x20);
#endif
}

void bind_zeus_configuration(ZeusState& state, ConfigLink& descriptors) noexcept {
    const auto* descriptor = static_cast<BooleanConfig*>(find_config(descriptors, 0x23060EC5u));
    state.rage_enabled = &descriptor->value;
}

void process_zeus(ZeusState& state, const ZeusConfig& config, const ZeusBindings& bindings,
                  const client::CommandFrame& frame, void* player, client::Command& command) {
    if (!config.enabled.value || *state.rage_enabled != 0 || frame.weapon_id_d8 != 31 ||
        !(frame.flags & 0x400u) || (command.movement.buttons & 1u))
        return;

    auto* weapon = reinterpret_cast<void*>(std::uintptr_t(frame.weapon_address));
    auto query = rage::prepare_target_bullet(weapon, bindings.weapon_definition, false);
    ZeusTraceFilter native_filter{bindings.trace_filter_vtable};
    const auto filter = native_filter.binding();
    const auto direction = field<RageVector>(&frame, 0x174);
    const auto source = static_cast<BallisticEntity>(reinterpret_cast<std::uintptr_t>(player));
    const auto result = rage::trace_target_direction(bindings.ballistics, source, frame.origin_dc,
                                                     direction, filter, query);
    if (result == 0 || _mm_cvttss_si32(_mm_set_ss(query.damage)) < 110)
        return;

    // A nonzero 43364CEC return already guarantees a nonnull hit entity.
    // Native's contradictory-null arm is the overlapped null-write trap at
    // 433A13A5; it is not an alternate no-target decision.
    const auto* target = reinterpret_cast<const void*>(std::uintptr_t(query.trace.entity));
    if (field<std::uint8_t>(target, bindings.target_immunity) == 0)
        command.movement.buttons |= 1u;
}

} // namespace skeet::misc
