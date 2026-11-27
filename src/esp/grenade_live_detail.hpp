#pragma once
#include "skeet/esp/grenade_live.hpp"
#include "skeet/esp/grenade_live_bindings.hpp"
#include "skeet/esp/grenade_engine.hpp"
#include "skeet/engine/scene_queries.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <immintrin.h>
#include <vector>

namespace skeet::esp::grenade_live_detail {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
inline std::uint32_t identity(const void* value) noexcept {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(value));
}
inline std::int32_t truncate(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
inline Vector3 difference(Vector3 a, Vector3 b) noexcept {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline float squared(Vector3 v) noexcept {
    return (v.y * v.y + v.x * v.x) + v.z * v.z;
}
inline float convar_float(const void* value) noexcept {
    while (value != field<const void*>(value, 0x1c))
        value = field<const void*>(value, 0x1c);
    return std::bit_cast<float>(field<std::uint32_t>(value, 0x2c) ^ identity(value));
}
inline std::int32_t convar_integer(const void* value) noexcept {
    while (value != field<const void*>(value, 0x1c))
        value = field<const void*>(value, 0x1c);
    return std::bit_cast<std::int32_t>(field<std::uint32_t>(value, 0x30) ^ identity(value));
}
inline float clamp(float value, float low, float high) noexcept {
    return _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(value), _mm_set_ss(low)), _mm_set_ss(high)));
}
}
namespace skeet::esp {
struct GrenadeLiveScene::Storage {
    GrenadeLiveBindings bindings;
    EngineGrenades& raw;
    GrenadeLiveOffsets offsets;
    GrenadeAuxiliaryLiveContext auxiliary_context;
    std::vector<std::uint32_t> smoke_handles;
    std::vector<Vector3> molotov_cells;
    std::int32_t last_entity_scan_tick = -1000;
    std::int32_t last_smoke_scan_tick = -1000;
    explicit Storage(const GrenadeLiveBindings&);
    bool enemy(void*) const;
    bool game_flag(std::ptrdiff_t) const;
    std::int32_t team(void*);
    bool admitted(std::uint32_t, void* thrower, void* selected);
    void* thrower(void* projectile, std::uint32_t type);
    float damage_scale(std::uint32_t handle, std::uint32_t type);
    GrenadeDamageResult damage(void*, Vector3 explosion, Vector3 distance_origin);
    GrenadeDamageSelection select_damage(Vector3, const IntegerConfig&);
    void assess(GrenadeWarningRecord&, void*, std::uint32_t&);
    void update(GrenadeWarningOwner&, const GrenadeEspConfig&, void*, std::uintptr_t);
    void draw(ui::RenderQueue&, const misc::GrenadePredictionConfig&, const GrenadeEspConfig&,
              GrenadeTrajectoryOwner&, misc::GrenadeCommandState&, GrenadeWarningOwner&);
    void draw_warnings(ui::RenderQueue&, const GrenadeEspConfig&, GrenadeWarningOwner&);
};
}
