#include "skeet/lagcomp/history_velocity_capture.hpp"
#include "skeet/lagcomp/receive_timing.hpp"
#include "skeet/lagcomp/packet_capture.hpp"
#include "skeet/lagcomp/velocity.hpp"
#include "skeet/lagcomp/velocity_validation.hpp"
#include "skeet/lagcomp/capture_speed.hpp"
#include "skeet/lagcomp/prediction_scope.hpp"
#include "skeet/animation/layer_state.hpp"
#include <bit>
#include <cstring>

namespace skeet::lagcomp {
namespace {
template <class T> T read(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
// Actual433AC5BD/4341873D guard. The saved pointer is retained even if an
// engine call changes the global which originally supplied the binding.
class TransformOverrideScope {
public:
    explicit TransformOverrideScope(std::uint8_t* value) noexcept : target(value) {
        if (target) {
            saved = *target;
            *target = 0;
        }
    }
    ~TransformOverrideScope() {
        if (target)
            *target = saved;
    }
    TransformOverrideScope(const TransformOverrideScope&) = delete;

private:
    std::uint8_t* target;
    std::uint8_t saved;
};
float convar_float(const void* object) noexcept {
    return std::bit_cast<float>(read_prediction_convar(const_cast<void*>(object)));
}
}
void capture_history_velocity(client::PlayerState& owner, void* player,
                              const HistoryVelocityCaptureBindings& bindings) {
    const auto& entity = bindings.entity;
    const auto& physics = bindings.reconstruction;
    auto velocity = read<Vector>(player, entity.offsets.velocity);
    const auto receive = sample_receive_tick(entity.engine, entity.receive_interval);
    bool publish = true;
    if (read<std::int32_t>(&owner.native, 0x1DC) != receive &&
        read<float>(&owner.native, 0x524) != 0.0f) {
        if (physics.update_flags & 1u)
            velocity = {};
        else {
            TransformOverrideScope guard(entity.transform_override);
            const auto timeline = history_timeline(owner);
            if (timeline.packet_count >= 3) {
                const auto& newest = timeline.packets[timeline.packet_head % 6u];
                const auto& previous = timeline.packets[(timeline.packet_head + 1u) % 6u];
                const float x = std::bit_cast<float>(newest.fields_04[0]) -
                                std::bit_cast<float>(previous.fields_04[0]);
                const float y = std::bit_cast<float>(newest.fields_04[1]) -
                                std::bit_cast<float>(previous.fields_04[1]);
                if (y * y + x * x != 0.0f)
                    publish = reconstruct_history_velocity(owner, player, velocity, physics) ==
                              HistoryVelocityReconstructionExit::publish;
                else
                    velocity = {};
            }
        }
    }
    if (publish)
        write(player, entity.offsets.velocity, velocity);
    const auto next_receive = sample_receive_tick(entity.engine, entity.receive_interval);
    if (read<std::int32_t>(&owner.native, 0x1DC) == next_receive)
        return;
    velocity = read<Vector>(player, entity.offsets.velocity);
    validate_velocity(velocity, bindings.maximum_velocity_convar);
    write(player, entity.offsets.velocity, velocity);
    if (read<float>(&owner.native, 0x5B0) == read<float>(&owner.native, 0x444)) {
        const auto blend = read<float>(&owner.native, 0x5A8);
        const float factor = ((1.0f - blend) - 0.0f) * std::bit_cast<float>(0x3EB33332u) +
                             std::bit_cast<float>(0x3F0CCCCDu);
        float maximum = 260.0f;
        const auto handle = read<std::uint32_t>(player, entity.offsets.weapon_handle);
        if (handle != UINT32_MAX) {
            if (auto* weapon = engine::invoke<void*>(entity.entity_list, 4, handle))
                maximum =
                    maximum_number(engine::invoke<float>(weapon, 442), 0.0010000000474974513f);
        }
        velocity = read<Vector>(player, entity.offsets.velocity);
        adjust_capture_velocity(velocity, read<float>(&owner.native, 0x5A8), maximum, factor);
        write(player, entity.offsets.velocity, velocity);
    }
    apply_history_capture_speed_limit(owner.history, player,
                                      {entity, physics.fields.player_flags,
                                       physics.fields.speed_limit, physics.fields.walking_override,
                                       bindings.landing_limit_convar});
    const auto jump = convar_float(physics.cvars.jump_impulse);
    const auto gravity = convar_float(physics.cvars.gravity);
    const float limit = jump - (entity.interval * gravity) * 0.5f;
    if (read<float>(player, entity.offsets.velocity + 8) > limit &&
        animation::layer_is_active(reinterpret_cast<const std::byte*>(&owner.native) + 0x4D8) &&
        physics.classify &&
        physics.classify(player, read<std::int32_t>(&owner.native, 0x4DC)) == 0x3D9)
        write(player, entity.offsets.velocity + 8, limit);
}
} // namespace skeet::lagcomp
