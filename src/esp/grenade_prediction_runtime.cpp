#include "skeet/esp/grenade_prediction.hpp"
#include "skeet/esp/grenade_auxiliary.hpp"
#include "skeet/movement/simulation_tick.hpp"
#include "detail.hpp"
#include <bit>
#include <cmath>
#include <immintrin.h>

namespace skeet::esp {
bool predict_grenade_trajectory(GrenadeTrajectoryOwner& owner, const GrenadeEspConfig& config,
                                const GrenadeTrajectoryFrame& frame,
                                const GrenadeMovementContext& movement,
                                const GrenadeEnvironment& environment, GrenadeTraceProvider trace,
                                const Projection& projection) {
    if (!begin_grenade_trajectory(owner, config, frame))
        return false;
    owner.path.points.clear();
    owner.path.bounces.clear();
    auto input = frame.throw_input;
    input.owner_entity = frame.player_entity;
    input.compensate_movement = frame.cached_motion && input.compensate_movement;
    if (frame.cached_motion) {
        input.player_velocity = owner.smoothed_velocity;
        const auto speed_squared = (input.player_velocity.x * input.player_velocity.x +
                                    input.player_velocity.y * input.player_velocity.y) +
                                   input.player_velocity.z * input.player_velocity.z;
        const auto prediction_tick = std::bit_cast<std::int32_t>(
            std::uint32_t(frame.prediction_tick) + std::uint32_t(frame.prediction_shift));
        if (speed_squared >= 80.0f && frame.motion_tick == prediction_tick) {
            MovementSimulation simulation{};
            initialize_movement_simulation(
                simulation, frame.player_entity,
                {movement.origin.x, movement.origin.y, movement.origin.z},
                {input.player_velocity.x, input.player_velocity.y, input.player_velocity.z},
                movement.player_flags, 0, *movement.player, *movement.rules);
            auto ticks =
                frame.two_tick_preroll
                    ? 2
                    : static_cast<std::int32_t>(std::ceil(0.1f / environment.tick_interval));
            while (ticks-- > 0)
                simulate_movement_tick(simulation, *movement.collision, *movement.environment,
                                       frame.player_entity, *movement.command,
                                       *movement.command_angles);
            input.eye_position = {simulation.position.x, simulation.position.y,
                                  simulation.position.z + simulation.view_offset_z};
            input.player_velocity = {simulation.velocity.x, simulation.velocity.y,
                                     simulation.velocity.z};
        }
    }
    owner.launch = prepare_grenade_throw(input, trace);
    owner.state.owner_entity = frame.player_entity;
    simulate_grenade(owner.state, frame.kind, owner.launch.position, owner.launch.velocity,
                     frame.cached_motion ? environment.tick_interval : 0.0f, 0.0f, environment,
                     trace, &projection, &owner.path);
    finalize_grenade(owner.state, trace);
    owner.drawing_position = owner.state.position;
    if (frame.kind == GrenadeKind::explosive || frame.kind == GrenadeKind::flash)
        owner.state.position.z += 1.0f;
    return true;
}
GrenadePredictionDrawResult render_grenade_prediction(ui::RenderQueue& queue,
                                                      const GrenadeEspConfig& config,
                                                      GrenadeTrajectoryOwner& owner,
                                                      const GrenadeTrajectoryFrame& frame,
                                                      const GrenadePredictionScene& scene) {
    GrenadePredictionDrawResult result;
    result.trajectory = predict_grenade_trajectory(
        owner, config, frame, scene.movement, scene.environment, scene.trace, scene.projection);
    if (frame.engine_reject_148 || frame.engine_reject_174 || frame.game_player_override)
        return result;
    if (result.trajectory) {
        GrenadeAuxiliaryResult presentation;
        if (frame.kind == GrenadeKind::explosive) {
            result.damage = select_grenade_damage(
                owner.state.position, scene.damage_candidates, scene.maximum_players,
                scene.damage_threshold, scene.targeting, scene.damage_engine, scene.damage_math);
            presentation.magnitude = _mm_cvttss_si32(_mm_set_ss(result.damage.damage));
            presentation.highlight = result.damage.reached_threshold;
            if (presentation.magnitude > 0)
                presentation.annotation = detail::decimal(presentation.magnitude) + u" dmg";
        } else
            presentation = evaluate_grenade_auxiliary(owner.state, scene.auxiliary);
        GrenadeScreenPoint endpoint;
        if (presentation.magnitude > 0 && !presentation.annotation.empty() &&
            project_grenade_point(scene.projection, owner.drawing_position, endpoint)) {
            queue.text(_mm_cvttss_si32(_mm_set_ss(endpoint.x - 4.0f)),
                       _mm_cvttss_si32(_mm_set_ss(endpoint.y - 8.0f)),
                       presentation.highlight ? 0xff15c28fu : 0xc8ffffffu, 0x18, 0,
                       presentation.annotation);
        }
        if (presentation.draw_path)
            draw_grenade_path(queue, config, owner.path, scene.projection, owner.drawing_position,
                              presentation.magnitude);
    }
    draw_grenade_warnings(queue, config, scene.projection, scene.warning_records,
                          scene.warning_entities, scene.viewer, scene.explosive_texture,
                          scene.fire_texture);
    return result;
}
} // namespace skeet::esp
