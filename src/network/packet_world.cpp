#include "skeet/network/packet_world.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::network {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T out;
    std::memcpy(&out, static_cast<const std::byte*>(object) + offset, sizeof out);
    return out;
}
const void* convar_parent(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1c);
        if (parent == object)
            return object;
        object = parent;
    }
}
std::uint32_t convar_bits(const void* object, std::ptrdiff_t offset) noexcept {
    object = convar_parent(object);
    return field<std::uint32_t>(object, offset) ^
           std::uint32_t(reinterpret_cast<std::uintptr_t>(object));
}
float convar_float(const void* object) noexcept {
    return std::bit_cast<float>(convar_bits(object, 0x2c));
}
std::int32_t convar_integer(const void* object) noexcept {
    return std::bit_cast<std::int32_t>(convar_bits(object, 0x30));
}
float absolute(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffU);
}
Vector shifted(Vector a, Vector b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
}
Bounds transformed_bounds(const Matrix3x4& matrix, Vector minimum, Vector maximum) noexcept {
    const Vector center{(minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F,
                        (minimum.z + maximum.z) * 0.5F};
    const Vector extent{maximum.x - center.x, maximum.y - center.y, maximum.z - center.z};
    Vector out_center{}, out_extent{};
    float* c = &out_center.x;
    float* e = &out_extent.x;
    for (unsigned i = 0; i < 3; ++i) {
        const auto& row = matrix.rows[i];
        c[i] = ((row[0] * center.x + row[1] * center.y) + row[2] * center.z) + row[3];
        e[i] = (absolute(row[0] * extent.x) + absolute(row[1] * extent.y)) +
               absolute(row[2] * extent.z);
    }
    return {
        {out_center.x - out_extent.x, out_center.y - out_extent.y, out_center.z - out_extent.z},
        {out_extent.x + out_center.x, out_extent.y + out_center.y, out_extent.z + out_center.z}};
}
Bounds entity_bounds(void* entity) {
    const auto collision = engine::invoke<void*>(entity, 3);
    if (!(engine::invoke<std::uint32_t>(collision, 12) & 0x40U) &&
        engine::invoke<std::int32_t>(collision, 11) != 2 &&
        engine::invoke<std::int32_t>(collision, 11) != 0) {
        const auto angles = engine::invoke<const Vector*>(collision, 9);
        if (angles->x != 0.0F || angles->y != 0.0F || angles->z != 0.0F) {
            const auto maximum = engine::invoke<const Vector*>(collision, 2);
            const auto minimum = engine::invoke<const Vector*>(collision, 1);
            const auto matrix = engine::invoke<const Matrix3x4*>(collision, 10);
            return transformed_bounds(*matrix, *minimum, *maximum);
        }
    }
    const auto origin = engine::invoke<const Vector*>(entity, 10);
    const auto minimum = engine::invoke<const Vector*>(collision, 1);
    Bounds out;
    out.minimum = shifted(*origin, *minimum);
    const auto maximum = engine::invoke<const Vector*>(collision, 2);
    out.maximum = shifted(*origin, *maximum);
    return out;
}
Bounds standing_bounds(Vector origin, float radius, float height) noexcept {
    return {{(origin.x + 0.0F) - radius, (origin.y + 0.0F) - radius, (origin.z + 46.0F) - 0.0F},
            {(origin.x + 0.0F) + radius, (origin.y + 0.0F) + radius, (origin.z + 64.0F) + height}};
}
bool target_collision_trigger(FakeLagState& state, const PacketFrame& frame,
                              Vector predicted_origin, std::int32_t handle, PacketWorld& world,
                              bool& result) {
    if (handle == -1)
        return false;
    const auto target = engine::invoke<void*, std::int32_t>(world.entity_list, 4, handle);
    if (!target || field<std::uint8_t>(target, world.rejection_byte) != 0)
        return false;
    const auto networkable = static_cast<std::byte*>(target) + 8;
    if (engine::invoke<bool>(networkable, 9)) {
        const auto index = engine::invoke<std::int32_t>(networkable, 10);
        if (world.player_geometry[std::uint32_t(index) & 63U].alpha == 0.0F)
            return false;
    }
    const auto current = entity_bounds(world.local_player);
    const Vector displacement{predicted_origin.x - frame.origin.x,
                              predicted_origin.y - frame.origin.y,
                              predicted_origin.z - frame.origin.z};
    Bounds predicted{shifted(current.minimum, displacement),
                     shifted(current.maximum, displacement)};
    const auto local =
        standing_bounds(field<Vector>(target, world.origin), convar_float(world.hull_radius.get()),
                        convar_float(world.standing_height.get()));
    void* wind = nullptr;
    if (state.value_8c != -1)
        wind = engine::invoke<void*, std::int32_t>(world.entity_list, 4, state.value_8c);
    if (!wind && !(state.flags & 8U)) {
        state.value_8c = -1;
        state.flags |= 8U;
        const auto end = field<std::int32_t>(world.entity_list, 0x24);
        for (std::uint32_t i = 65; std::bit_cast<std::int32_t>(i) < end; ++i) {
            if (i & 0xffff0000U)
                continue;
            const auto candidate = engine::invoke<void*, std::uint32_t>(world.entity_list, 3, i);
            if (candidate && engine::invoke<const void*>(static_cast<std::byte*>(candidate) + 8,
                                                         2) == world.wind_class) {
                state.value_8c = *engine::invoke<const std::int32_t*>(candidate, 2);
                wind = candidate;
                break;
            }
        }
    }
    Vector movement{};
    if (wind) {
        const auto velocity = field<Vector>(wind, world.wind_velocity);
        const auto scale = convar_float(world.wind_scale.get());
        movement = {scale * velocity.x, velocity.y * scale, velocity.z * scale};
    }
    const auto type = convar_integer(world.game_type.get());
    const auto mode = convar_integer(world.game_mode.get());
    const float expansion = (type == 0 && mode == 0) || (type == 1 && mode == 2) ? 8.25F : 0.0F;
    bool first = false;
    if (world.box_query.function) {
        first = world.box_query.function(world.box_query.context, predicted, local, movement,
                                         expansion);
        if (world.box_query.function) {
            result = world.box_query.function(world.box_query.context, current, local, movement,
                                              expansion);
            return first && !result;
        }
    }
    result = false;
    return first;
}
} // namespace skeet::network
