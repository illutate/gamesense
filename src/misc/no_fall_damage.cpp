#include "skeet/misc/no_fall_damage.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <limits>
#include <new>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
float convar(const void* object) noexcept {
    for (;;) {
        const auto* next = field<const void*>(object, 0x1c);
        if (next == object)
            break;
        object = next;
    }
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2c) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
MovementVector origin(void* player) {
    return *engine::invoke<const MovementVector*>(player, 10);
}
MovementCollisionTrace empty_trace() noexcept {
    MovementCollisionTrace trace{};
    trace.fraction = 1.0f;
    trace.hitgroup = -1;
    trace.hitbox = -1;
    return trace;
}
MovementCollisionTrace trace_down(const NoFallDamageBindings& b, void* player,
                                  MovementVector position) {
    const auto maximum = *engine::invoke<const MovementVector*>(player, 77);
    const auto minimum = *engine::invoke<const MovementVector*>(player, 76);
    const MovementVector end{position.x, position.y, position.z - 1000.0f};
    auto trace = empty_trace();
    b.collision.trace_ray(
        make_movement_hull_ray(position, end, minimum, {maximum.x, maximum.y, 54.0f}), 1u,
        {MovementCollisionFilterKind::reject_entities, 0, 0, 0}, trace);
    return trace;
}
bool acceptable_floor(const MovementCollisionTrace& trace) noexcept {
    return trace.fraction < 1.0f && !trace.start_solid && !trace.all_solid &&
           trace.plane_normal.z >= 0.699999988079071f;
}
void accelerate_forecast(const NoFallDamageRuntime& runtime, MovementVector& velocity, float yaw,
                         bool& alternating) {
    const auto speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    float forward = 450.0f, side = 0.0f, heading = yaw;
    if (speed != 0.0f) {
        forward = 0.0f;
        const auto change = 845.5f / speed;
        if (alternating) {
            heading = yaw - change;
            side = -450.0f;
        } else {
            heading = change + yaw;
            side = 450.0f;
        }
        alternating = !alternating;
    }
    MovementVector axis, right;
    movement_angle_vectors({0.0f, heading, 0.0f}, &axis, &right, nullptr);
    MovementVector direction{right.x * side + axis.x * forward, right.y * side + axis.y * forward,
                             0.0f};
    auto wish_speed = std::sqrt((direction.x * direction.x + direction.y * direction.y) +
                                direction.z * direction.z);
    const auto scale = 1.0f / (wish_speed + std::numeric_limits<float>::epsilon());
    direction.x *= scale;
    direction.y *= scale;
    direction.z *= scale;
    if (wish_speed != 0.0f && wish_speed > 250.0f)
        wish_speed = 250.0f;
    const auto additional =
        minimum_number(30.0f, wish_speed) -
        ((velocity.x * direction.x + velocity.y * direction.y) + velocity.z * direction.z);
    if (additional > 0.0f) {
        const auto amount =
            minimum_number(additional, (convar(runtime.bindings.air_accelerate) * wish_speed) *
                                           runtime.bindings.interval);
        velocity.x = direction.x * amount + velocity.x;
        velocity.y = direction.y * amount + velocity.y;
    }
}
bool clear_landing_segment(const NoFallDamageBindings& b, void* player, MovementVector first,
                           MovementVector last) {
    auto trace = empty_trace();
    b.collision.trace_ray(
        make_movement_hull_ray(first, last, {-16.0f, -16.0f, 0.0f}, {16.0f, 16.0f, 54.0f}), 1u,
        {MovementCollisionFilterKind::skip_player,
         std::uint32_t(reinterpret_cast<std::uintptr_t>(player)), 0, 0},
        trace);
    return !trace.start_solid && !trace.all_solid && trace.fraction >= 0.8999999761581421f;
}
// Both complete ballistic loops in433953A3, followed by their two clearance
// traces. The caller passes velocity after the first half-gravity update.
bool landing_forecast(const NoFallDamageRuntime& runtime, void* player, MovementVector position,
                      MovementVector velocity, float floor, float yaw) {
    const auto& b = runtime.bindings;
    const auto half_gravity = (convar(b.gravity) * b.interval) * 0.5f;
    position.z += 9.0f;
    auto apex = position;
    MovementVector steep_position{};
    bool has_steep_position = false, alternating = false;
    const auto threshold = floor + 11.0f;
    if (position.z > threshold) {
        do {
            velocity.z -= half_gravity;
            if (*runtime.air_strafe_enabled)
                accelerate_forecast(runtime, velocity, yaw, alternating);
            position.x = b.interval * velocity.x + position.x;
            position.y = b.interval * velocity.y + position.y;
            position.z = position.z + b.interval * velocity.z;
            if (position.z > apex.z)
                apex = position;
            if (!has_steep_position && velocity.z < 0.0f &&
                -velocity.z > std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y)) {
                steep_position = position;
                has_steep_position = true;
            }
            const auto again = position.z > threshold;
            velocity.z -= half_gravity;
            if (!again)
                break;
        } while (true);
    }
    if (velocity.z > -580.0f || 9.0f > position.z - floor)
        return false;
    MovementTraceFlagGuard guard(b.movement_environment.engine_trace_flag);
    if (has_steep_position) {
        if (!clear_landing_segment(b, player, apex, steep_position))
            return false;
        apex = steep_position;
    }
    return clear_landing_segment(b, player, apex, position);
}

struct Vertex {
    float x, y;
    std::uint32_t color;
};
void triangles(ui::RenderQueue& queue, std::span<const Vertex> vertices) {
    const auto count = static_cast<std::uint32_t>(vertices.size());
    auto* payload = queue.append(7, count * sizeof(Vertex) + 16);
    std::memcpy(payload, &count, 4);
    std::memcpy(payload + 4, vertices.data(), vertices.size_bytes());
}
bool disc_project(const esp::Projection& p, MovementVector v, Vertex& output) noexcept {
    const auto& m = p.matrix;
    const auto w = ((v.y * m[13] + v.x * m[12]) + v.z * m[14]) + m[15];
    if (0.001f > w)
        return false;
    const auto inverse = 1.0f / w;
    const auto x = ((v.y * m[1] + v.x * m[0]) + v.z * m[2]) + m[3];
    const auto y = ((v.y * m[5] + v.x * m[4]) + v.z * m[6]) + m[7];
    output.x = (x * inverse) * p.horizontal_scale + p.horizontal_scale;
    output.y = p.vertical_scale - (y * inverse) * p.vertical_scale;
    return true;
}
//4337DCA0/43397246, fixed pitch-90/radius24 used by this owner.
void draw_disc(ui::RenderQueue& queue, const esp::Projection& projection, MovementVector position,
               std::uint32_t color) {
    MovementVector first, second;
    movement_angle_vectors({-90.0f, 0.0f, 0.0f}, nullptr, &second, &first);
    second = {-second.x, -second.y, -second.z};
    auto previous = MovementVector{first.x * 24.0f + position.x, first.y * 24.0f + position.y,
                                   first.z * 24.0f + position.z};
    Vertex center{0, 0, color};
    if (!disc_project(projection, position, center))
        return;
    std::array<Vertex, 48> vertices;
    for (int i = 1; i <= 16; ++i) {
        const auto angle = float(i) * 0.3926990926265716552734375f;
        const auto sine = std::sin(angle), cosine = std::cos(angle);
        const MovementVector next{
            ((first.x * cosine) * 24.0f + position.x) + (second.x * sine) * 24.0f,
            ((first.y * cosine) * 24.0f + position.y) + (second.y * sine) * 24.0f,
            ((first.z * cosine) * 24.0f + position.z) + (second.z * sine) * 24.0f};
        auto* triangle = vertices.data() + 3 * (i - 1);
        triangle[0] = center;
        triangle[1].color = color & 0x00ffffffu;
        triangle[2].color = color & 0x00ffffffu;
        if (!disc_project(projection, previous, triangle[1]) ||
            !disc_project(projection, next, triangle[2]))
            return;
        previous = next;
    }
    triangles(queue, vertices);
}
//Complete43394A38. Seven world points, six triangles, black/alpha40.
void draw_arrow(ui::RenderQueue& queue, const esp::Projection& projection, MovementVector start,
                MovementVector end) {
    const MovementVector delta{end.x - start.x, end.y - start.y, end.z - start.z};
    const auto scale =
        1.0f / (std::sqrt((delta.x * delta.x + delta.y * delta.y) + delta.z * delta.z) +
                std::numeric_limits<float>::epsilon());
    const MovementVector direction{delta.x * scale, delta.y * scale, delta.z * scale};
    const MovementVector across{direction.y - direction.z * 0.0f, direction.z * 0.0f - direction.x,
                                direction.x * 0.0f - direction.y * 0.0f};
    constexpr float width = 6.0f;
    const auto half = static_cast<float>(double(width) * 0.5);
    const MovementVector neck{end.x - direction.x * width, end.y - direction.y * width,
                              end.z - direction.z * width};
    const std::array<MovementVector, 7> points{
        {{start.x - across.x * half, start.y - across.y * half, start.z - across.z * half},
         {neck.x - across.x * half, neck.y - across.y * half, neck.z - across.z * half},
         {neck.x - across.x * width, neck.y - across.y * width, neck.z - across.z * width},
         end,
         {neck.x + across.x * width, neck.y + across.y * width, neck.z + across.z * width},
         {neck.x + across.x * half, neck.y + across.y * half, neck.z + across.z * half},
         {start.x + across.x * half, start.y + across.y * half, start.z + across.z * half}}};
    for (const auto& point : points) {
        Point screen;
        if (!esp::project_point(projection, {point.x, point.y, point.z}, screen))
            return;
    }
    constexpr std::array<unsigned, 18> order{4, 3, 2, 0, 6, 5, 5, 1, 0, 2, 3, 4, 5, 6, 0, 0, 1, 5};
    std::array<Vertex, 18> vertices;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const auto point = points[order[i]];
        Point screen;
        if (!esp::project_point(projection, {point.x, point.y, point.z}, screen))
            return;
        vertices[i] = {float(screen.x), float(screen.y), 0x28000000u};
    }
    triangles(queue, vertices);
}
} //namespace

void initialize_no_fall_damage_config(ConfigRegistries& r, NoFallDamageConfig& config) noexcept {
    initialize_boolean_config(r.descriptors, config.enabled, 0x8E38F1F3u);
    initialize_color_config(r.descriptors, config.color, 0x6E4E5CC7u, 0x00FF8E00u);
}
void NoFallDamageRuntime::bind_configuration(ConfigLink& descriptors) noexcept {
    air_strafe_enabled = &static_cast<BooleanConfig*>(find_config(descriptors, 0xF2EB89E9u))->value;
}
void NoFallDamageRuntime::phase2(void* player, client::Command& command) {
    if (!config.enabled.value || field<std::uint8_t>(player, bindings.offsets.movement_type) != 2 ||
        (field<std::uint32_t>(player, bindings.offsets.flags) & 1u))
        return;
    auto position = origin(player);
    if (!(field<std::uint32_t>(player, bindings.offsets.flags) & 2u))
        position.z += 9.0f;
    const auto trace = trace_down(bindings, player, position);
    if (!acceptable_floor(trace))
        return;
    auto distance = trace.fraction * 1000.0f;
    auto vertical = field<MovementVector>(player, bindings.offsets.velocity).z;
    if (vertical < 0.0f && distance < 11.0f) {
        if (-580.0f > vertical && distance > 9.0f)
            command.movement.buttons = (command.movement.buttons & ~4u) | 2u;
        return;
    }
    const auto half_gravity = (bindings.interval * convar(bindings.gravity)) * 0.5f;
    static_cast<void>(engine::invoke<const MovementVector*>(player, 10));
    if (distance > 11.0f) {
        do {
            vertical -= half_gravity;
            distance += bindings.interval * vertical;
            vertical -= half_gravity;
        } while (distance > 11.0f);
    }
    if (vertical > -580.0f || !(distance >= 9.0f))
        return;
    command.movement.buttons = (command.movement.buttons | 4u) & ~2u;
}
void NoFallDamageRuntime::draw(ui::RenderQueue& queue, void* player) {
    if (!config.enabled.value || !(config.color.value & 0xff000000u) ||
        !(field<std::uint32_t>(player, bindings.offsets.flags) & 1u) ||
        field<std::uint8_t>(player, bindings.offsets.movement_type) != 2)
        return;
    MovementAngles view;
    engine::invoke<void>(bindings.engine, 18, &view);
    view.pitch = 0.0f;
    MovementVector forward, right;
    movement_angle_vectors(view, &forward, &right, nullptr);
    auto trace_origin = origin(player);
    trace_origin = {forward.x * 100.0f + trace_origin.x, forward.y * 100.0f + trace_origin.y,
                    forward.z * 100.0f + trace_origin.z + 9.0f};
    const auto trace = trace_down(bindings, player, trace_origin);
    if (!acceptable_floor(trace) || 250.0f > trace.fraction * 1000.0f)
        return;
    auto velocity = field<MovementVector>(player, bindings.offsets.velocity);
    velocity.z = convar(bindings.jump_impulse) + velocity.z -
                 (bindings.interval * convar(bindings.gravity)) * 0.5f;
    const auto first_origin = origin(player);
    std::uint32_t alternatives =
        landing_forecast(*this, player, first_origin, velocity, trace.end.z, view.yaw) ? 1u : 0u;
    client::Command command{};
    command.angles = {view.pitch, view.yaw, view.roll};
    command.movement.forward_move = 450.0f;
    command.movement.buttons = (command.movement.buttons & ~2u) | 0x400000u;
    const auto ticks = _mm_cvttss_si32(_mm_set_ss(0.75f / bindings.interval + 0.5f));
    const auto flags = field<std::uint32_t>(player, bindings.offsets.flags);
    const auto live_velocity = field<MovementVector>(player, bindings.offsets.velocity);
    const auto current_origin = origin(player);
    MovementSimulation simulation{};
    initialize_live_movement_simulation(simulation, player, current_origin, live_velocity, flags, 0,
                                        bindings.simulation_capture);
    for (std::int32_t i = 0; i < ticks; ++i) {
        simulate_live_movement_tick(simulation, bindings.collision, bindings.movement_environment,
                                    player, command);
        if (!(simulation.player_flags & 1u) ||
            16.0f > simulation.velocity.y * simulation.velocity.y +
                        simulation.velocity.x * simulation.velocity.x)
            break;
    }
    if (!(simulation.player_flags & 1u) &&
        landing_forecast(*this, player, simulation.position, simulation.velocity, trace.end.z,
                         view.yaw))
        alternatives |= 2u;
    if (!alternatives)
        return;
    draw_disc(queue, bindings.projection, origin(player), config.color.value);
    const MovementVector length{forward.x * 10.0f, forward.y * 10.0f, forward.z * 10.0f};
    const auto draw_at = [&](MovementVector center, bool reverse) {
        const MovementVector low{center.x - length.x, center.y - length.y, center.z - length.z};
        const MovementVector high{length.x + center.x, length.y + center.y, length.z + center.z};
        if (reverse)
            draw_arrow(queue, bindings.projection, high, low);
        else
            draw_arrow(queue, bindings.projection, low, high);
    };
    if (alternatives == 1u)
        draw_at(origin(player), false);
    else if (alternatives == 2u)
        draw_at(origin(player), true);
    else {
        auto first = origin(player);
        first = {first.x - right.x * 8.0f, first.y - right.y * 8.0f, first.z - right.z * 8.0f};
        draw_at(first, false);
        auto second = origin(player);
        second = {right.x * 8.0f + second.x, right.y * 8.0f + second.y, right.z * 8.0f + second.z};
        draw_at(second, true);
    }
}

namespace {
#if defined(_M_IX86) || defined(__i386__)
#define NOFALL_MEMBER __thiscall
#define NOFALL_FAST __fastcall
#else
#define NOFALL_MEMBER
#define NOFALL_FAST
#endif
void NOFALL_FAST draw_no_fall(void* endpoint, ui::RenderQueue* queue, void* player, std::int32_t*) {
    static_cast<NoFallDamageHudHandler*>(endpoint)->runtime.draw(*queue, player);
}
const hud::HudFrameEntry hud_table[]{draw_no_fall};
using OwnerEntry = void(NOFALL_MEMBER*)(void*);
struct NoFallDamageOwner;
void NOFALL_MEMBER destroy(void*);
void NOFALL_MEMBER bind(void*);
void NOFALL_MEMBER unchanged(void*) {} //433CCE8E
const OwnerEntry lifecycle_table[]{destroy,   bind,      unchanged, unchanged, unchanged,
                                   unchanged, unchanged, unchanged, unchanged};
struct NoFallDamageOwner {
    const OwnerEntry* vtable = nullptr;
    ConfigRegistries& registries;
    NoFallDamageRuntime runtime;
    client::NativeCommandPhaseReceiver command_receiver{};
    NoFallDamageHudHandler hud;
    engine::JobMemory memory;
    NoFallDamageOwner(engine::OwnerRegistryBinding owners, client::CommandPhaseRegistry& commands,
                      engine::OwnerRegistryBinding hud_owners, ConfigRegistries& r,
                      NoFallDamageConfig& config, NoFallDamageBindings bindings)
        : registries(r), runtime(config, bindings), hud(runtime), memory(owners.memory) {
        owners.append(this); //4348DCBC
        commands.append(command_receiver, &runtime); //4348DCC4
        hud::register_hud_frame(hud_owners, &hud); //4348DCCC
        vtable = lifecycle_table; //4348DCD4
        commands.publish(command_receiver, client::CommandOwnerId::owner_43472998); //4348DCDC
        hud.vtable = hud_table; //4348DCE4
        initialize_no_fall_damage_config(r, config);
        runtime.air_strafe_enabled = nullptr;
    }
};
void NOFALL_MEMBER bind(void* object) {
    auto& owner = *static_cast<NoFallDamageOwner*>(object);
    owner.runtime.bind_configuration(owner.registries.descriptors);
}
void NOFALL_MEMBER destroy(void* object) {
    auto* owner = static_cast<NoFallDamageOwner*>(object);
    const auto memory = owner->memory;
    owner->~NoFallDamageOwner();
    auto* bytes = static_cast<std::byte*>(object);
    const auto displacement = field<std::uint32_t>(bytes, -8);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
} //namespace
NoFallDamageHudHandler::NoFallDamageHudHandler(NoFallDamageRuntime& owner) : runtime(owner) {}
std::size_t no_fall_damage_owner_size() noexcept {
    return sizeof(NoFallDamageOwner);
}
NoFallDamageRuntime& construct_no_fall_damage(void* storage, engine::OwnerRegistryBinding owners,
                                              client::CommandPhaseRegistry& commands,
                                              engine::OwnerRegistryBinding hud_owners,
                                              ConfigRegistries& r, NoFallDamageConfig& config,
                                              NoFallDamageBindings bindings) {
    return (::new (storage) NoFallDamageOwner(owners, commands, hud_owners, r, config, bindings))
        ->runtime;
}
#undef NOFALL_MEMBER
#undef NOFALL_FAST
} //namespace skeet::misc
