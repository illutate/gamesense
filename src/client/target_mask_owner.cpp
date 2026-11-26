#include "skeet/client/target_mask_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/lagcomp/records.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>
#include <immintrin.h>

namespace skeet::client {
namespace {
template <class T> T field(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void field(void* object, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
const std::uint8_t& feature(ConfigRegistries& registries) {
    auto* link = find_config(registries.descriptors, target_mask_feature_hash);
    if (!link)
        throw std::invalid_argument("Target-mask owner requires native config347A4FA6");
    auto& descriptor = *static_cast<ConfigDescriptor*>(link);
    if (descriptor.type != ConfigType::boolean || descriptor.serialized_size != 1)
        throw std::invalid_argument("Target-mask config347A4FA6 must be native Boolean");
    return static_cast<BooleanConfig&>(descriptor).value;
}
bool lag_compensation_enabled(const void* variable) noexcept {
    for (;;) {
        const auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return field<std::uint32_t>(variable, 0x30) !=
           std::uint32_t(reinterpret_cast<std::uintptr_t>(variable));
}
bool low_latency(const TargetMaskBindings& bindings) noexcept {
    const auto total = _mm_add_sd(_mm_set_sd(*bindings.real_latency_outgoing),
                                  _mm_set_sd(*bindings.real_latency_incoming));
    const auto rounded = _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), total));
    return 0.5f > rounded;
}
void clear_history(TargetMaskState& state) noexcept {
    state.count = 0;
    state.first = 0;
    if (state.has_accepted_tick)
        state.has_accepted_tick = 0;
}
}

TargetMaskOwner::TargetMaskOwner(TargetMaskSettings& settings, TargetMaskBindings bindings) noexcept
    : enabled(settings.enabled.value), bindings(bindings) {}
TargetMaskOwner::TargetMaskOwner(ConfigRegistries& registries, TargetMaskBindings bindings)
    : enabled(feature(registries)), bindings(bindings) {}

void reset_target_mask_owner(TargetMaskOwner& owner) noexcept { // 4332F1D9
    clear_history(owner.state);
    owner.state.attack_pending = 0;
}
void observe_target_mask_update_status(TargetMaskOwner& owner,
                                       const void* status) noexcept { // 4336AC2E
    if ((field<std::uint32_t>(status, 0x24) & 4u) && !field<std::uint8_t>(status, 0x10))
        clear_history(owner.state);
}
void capture_target_mask_update(TargetMaskOwner& owner, std::uintptr_t incoming) { // 4336526D
    if (!owner.enabled)
        return;
    const auto* visibility = field<const void*>(reinterpret_cast<const void*>(incoming), 8);
    if (!visibility)
        return;
    auto& state = owner.state;
    if (state.count != 128)
        ++state.count;
    state.first = (state.first - 1u) & 127u;
    auto& record = state.records[state.first];
    const auto time = engine::invoke<float>(owner.bindings.engine_client, 14);
    record.receive_tick = lagcomp::receive_time_to_tick(time, *owner.bindings.receive_interval);
    // Native4334194D replaces every destination bit. Bit0 comes from entity64,
    // not the unused entity0 bit in the first source word.
    record.targets[0] = (field<std::uint32_t>(visibility, 0x10) & 0xFFFFFFFEu) |
                        (field<std::uint32_t>(visibility, 0x18) & 1u);
    record.targets[1] = field<std::uint32_t>(visibility, 0x14);
}
void process_target_mask_message(TargetMaskOwner& owner, void* message) { // 4332CC6A
    if (engine::invoke<std::int32_t>(message, 7) != 4)
        return;
    auto& state = owner.state;
    const auto& bindings = owner.bindings;
    if (owner.enabled && !engine::invoke<bool>(bindings.engine_client, 82) &&
        !engine::invoke<bool>(bindings.engine_client, 93) &&
        (field<std::uint8_t>(message, 0x24) & 1u)) {
        const auto incoming_tick = field<std::int32_t>(message, 0xC);
        if (incoming_tick == -1) {
            clear_history(state);
            return;
        }
        if (state.has_accepted_tick && incoming_tick >= state.accepted_tick &&
            *bindings.rage_enabled && lag_compensation_enabled(bindings.lag_compensation) &&
            low_latency(bindings)) {
            if (state.attack_pending) {
                state.attack_pending = 0;
            } else {
                const auto gap = subtract(incoming_tick, state.accepted_tick);
                if (gap >= 0 && std::uint32_t(gap) <= 80u) {
                    field(message, 0xC, state.accepted_tick);
                } else {
                    const auto oldest_allowed = subtract(incoming_tick, 80);
                    auto tick = incoming_tick;
                    for (std::uint32_t i = 0; i != state.count; ++i) {
                        const auto& record = state.records[(state.first + i) & 127u];
                        if (record.receive_tick < oldest_allowed)
                            break;
                        tick = record.receive_tick;
                    }
                    field(message, 0xC, tick);
                }
            }
        }
    }
    const auto tick = field<std::int32_t>(message, 0xC);
    if (tick == -1) {
        clear_history(state);
        return;
    }
    while (state.count) {
        const auto oldest = (state.first + state.count - 1u) & 127u;
        if (state.records[oldest].receive_tick >= tick)
            break;
        --state.count;
    }
    if (!state.count && state.has_accepted_tick)
        state.has_accepted_tick = 0;
    state.accepted_tick = tick;
    if (!state.has_accepted_tick)
        state.has_accepted_tick = 1;
}
void process_target_mask_message_context(TargetMaskOwner& owner, const void* context) {
    process_target_mask_message(owner, field<void*>(context, 4));
}
void build_command_target_mask(TargetMaskOwner& owner, CommandFrame& frame) noexcept { // 4340029E
    frame.field_1ec = 0;
    frame.field_1f0 = 0;
    const auto& state = owner.state;
    if (!owner.enabled || state.count < 2 || !state.has_accepted_tick)
        return;
    const auto& latest = state.records[state.first & 127u];
    for (std::uint32_t i = 0; i != state.count; ++i) {
        const auto& record = state.records[(state.first + i) & 127u];
        if (record.receive_tick != state.accepted_tick)
            continue;
        if (&record == &latest)
            return;
        frame.field_1ec = latest.targets[0] & ~record.targets[0];
        frame.field_1f0 = latest.targets[1] & ~record.targets[1];
        return;
    }
}
void mark_target_mask_attack(TargetMaskOwner& owner, const CommandFrame& frame,
                             const Command& command) noexcept { // 43386E3E
    if (!owner.enabled || !(frame.field_1ec || frame.field_1f0) || frame.weapon_kind_124 == 9 ||
        frame.weapon_kind_124 == 7)
        return;
    const auto flags = frame.flags;
    const auto buttons = command.movement.buttons;
    if (((flags & 0x100000u) || ((flags & 0x400u) && (buttons & 1u)) ||
         ((flags & 2u) && (buttons & 0x800u) &&
          (frame.weapon_id_d8 == 0x40 || frame.weapon_kind_124 == 0))) &&
        !(flags & 0x2000000u))
        owner.state.attack_pending = 1;
}

} // namespace skeet::client
