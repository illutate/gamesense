#include "skeet/legit/runtime.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace skeet::legit {
namespace {
template <class T> T field(const void* object, std::size_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float convar_float(const void* object) noexcept {
    for (;;) {
        const auto parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<float>(field<std::uint32_t>(object, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
}
void decay_recoil(Vector& velocity, Vector& angle, const RecoilDecayBindings& bindings) {
    if (!bindings.exponential)
        throw std::invalid_argument("Native recoil decay requires its exponential boundary");
    const auto linear = convar_float(bindings.linear_decay) * bindings.interval;
    const auto decay =
        bindings.exponential(-(bindings.interval * convar_float(bindings.angle_decay)));
    auto x = angle[0] * decay, y = angle[1] * decay, z = angle[2] * decay;
    angle = {x, y, z};
    const auto length = std::sqrt((x * x + y * y) + z * z);
    if (length == 0.0f || !(length > linear))
        x = y = z = 0.0f;
    else {
        const auto fraction = 1.0f - linear / length;
        z *= fraction;
        y *= fraction;
        x *= fraction;
    }
    angle = {x, y, z};
    const auto interval = bindings.interval;
    angle[0] = (velocity[0] * interval) * 0.5f + x;
    angle[1] = (velocity[1] * interval) * 0.5f + y;
    angle[2] = (velocity[2] * interval) * 0.5f + z;
    const auto velocity_decay =
        bindings.exponential((-convar_float(bindings.velocity_decay)) * bindings.interval);
    const auto vx = velocity[0] * velocity_decay;
    const auto vy = velocity[1] * velocity_decay;
    const auto vz = velocity[2] * velocity_decay;
    velocity = {vx, vy, vz};
    angle[0] = (vx * interval) * 0.5f + angle[0];
    angle[1] = (vy * interval) * 0.5f + angle[1];
    angle[2] = (vz * interval) * 0.5f + angle[2];
}
} // namespace skeet::legit
