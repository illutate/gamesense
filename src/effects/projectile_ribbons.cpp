#include "skeet/effects/projectile_ribbons.hpp"
#include "skeet/esp/grenade_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/render/beam_geometry.hpp"
#include <cstdio>
#include <cstring>
#include <x86intrin.h>

namespace skeet::effects {
using engine::invoke;
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t entry_type(const ProjectileRibbonEntry& entry) noexcept {
    return entry.state;
}
std::uint32_t entry_type(const esp::GrenadeWarningRecord& entry) noexcept {
    return entry.type;
}
bool entry_suppressed(const ProjectileRibbonEntry& entry) noexcept {
    return entry.rejected != 0;
}
bool entry_suppressed(const esp::GrenadeWarningRecord& entry) noexcept {
    return entry.suppressed;
}

// Native KeyValues allocation/constructor 433F1094 and symbol setter 43328E4B.
// The final word is deliberately not initialized by the native constructor.
struct KeyValues {
    std::uint32_t symbol;
    char* string_value;
    wchar_t* wide_value;
    std::uint32_t value;
    std::uint8_t type, escape;
    std::uint16_t symbol_high;
    void* system;
    bool case_sensitive;
    KeyValues *peer, *sub, *chain;
    std::uint32_t untouched;
};
#if defined(_M_IX86) || defined(__i386__)
static_assert(sizeof(KeyValues) == 0x2c);
#endif

void* create_material(const ProjectileRibbonBindings& bindings, const char* shader) {
    auto* values =
        static_cast<KeyValues*>(invoke<void*>(bindings.key_values_system, 2, std::int32_t(0x2c)));
    if (values) {
        values->symbol = 0;
        values->string_value = nullptr;
        values->wide_value = nullptr;
        values->value = 0;
        values->type = values->escape = 0;
        values->symbol_high = 0;
        values->system = bindings.key_values_system;
        values->case_sensitive = false;
        values->peer = values->sub = values->chain = nullptr;
        std::uint32_t insensitive = UINT32_MAX;
        const auto sensitive =
            invoke<std::uint32_t>(values->system, 10, &insensitive, shader, true);
        values->symbol = (insensitive & 0x00ffffffu) | ((sensitive & 255u) << 24);
        values->symbol_high = std::uint16_t(sensitive >> 8);
    }
    char name[64]{};
    // Native format at 433806A0 is m%I64u; this is the same unsigned value.
    std::snprintf(name, sizeof name, "m%llu", static_cast<unsigned long long>(__rdtsc()));
    return invoke<void*>(bindings.material_system, 83, static_cast<const char*>(name), values);
}
}

ProjectileRibbonOffsets::ProjectileRibbonOffsets(const engine::NativeOffsetGlobals& globals)
    : inactive{globals, 0x43467210, 0x43467630, 0x01E80986, 0},
      effects{globals, 0x43468B50, 0x43467B48, 0x38CA5228, 0},
      thrower_handle{globals, 0x43468C18, 0x4346A888, 0xDC743513, 0},
      velocity{globals, 0x4346A8E4, 0x434688CC, 0x36375C23, 0},
      previous_velocity{globals, 0x434683E8, 0x434693C0, 0x434E9E95, 0},
      simulation_time{globals, 0x43469198, 0x4346A538, 0xD5FD2E2C, 0},
      origin{globals, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0},
      collision_group{globals, 0x434683C8, 0x4346758C, 0xE4E2D1A7, 0},
      projectile_flags{globals, 0x43467488, 0x43467DEC, 0x7418868F, 0} {}

void ProjectileRibbonOwner::prepare_material(const ProjectileRibbonBindings& bindings) {
    if (!material)
        material = create_material(bindings, material_names.shader.c_str());
    if (!material)
        return; // native continues drawing with the null auto-bind material

    auto* real = invoke<void*>(material, 99);
    if (invoke<bool>(real, 71) && invoke<void*>(real, 69) &&
        invoke<bool>(real, 30, std::int32_t(0x2000)))
        return;

    const auto lock = invoke<std::uint32_t>(bindings.material_system, 118);
    invoke<void>(real, 37);
    for (const auto flag : {0x10, 0x20, 0x2000, 0x200000})
        invoke<void>(real, 29, std::int32_t(flag), true);
    if (auto* variable =
            invoke<void*>(real, 47, material_names.render_mode.c_str(), &render_mode_token))
        invoke<void>(variable, 5, std::int32_t(5));
    auto* texture = invoke<void*>(bindings.material_system, 91, material_names.texture.c_str(),
                                  "Particle textures", true, std::int32_t(0));
    if (texture) {
        if (auto* variable =
                invoke<void*>(real, 47, material_names.base_texture.c_str(), &base_texture_token))
            invoke<void>(variable, 15, texture);
    }
    invoke<void>(real, 53);
    invoke<void>(bindings.material_system, 119, lock);
}

void ProjectileRibbonOwner::release_material() {
    if (material) {
        invoke<void>(material, 13);
        material = nullptr;
    }
}

template <class Frame, class Entry, class Environment>
void ProjectileRibbonOwner::draw_impl(const Frame& frame, std::span<const Entry> entries,
                                      const ProjectileRibbonBindings& bindings,
                                      const Environment& environment,
                                      esp::GrenadeTraceProvider trace,
                                      std::vector<esp::Vector3>& world_path) {
    if (frame.suppressed || !frame.active)
        return;
    const auto& offsets = bindings.offsets;
    for (const auto& entry : entries) {
        if (entry.handle == UINT32_MAX)
            continue;
        auto* entity = invoke<void*>(bindings.entity_list, 4, entry.handle);
        if (!entity || (entry_type(entry) != 0 && entry_type(entry) != 1) ||
            entry_suppressed(entry))
            continue;
        if (field<std::int32_t>(entity, offsets.inactive) != 0 ||
            (field<std::uint32_t>(entity, offsets.effects) & 0x20u))
            continue;
        const auto thrower = field<std::uint32_t>(entity, offsets.thrower_handle);
        if (thrower == UINT32_MAX || !invoke<void*>(bindings.entity_list, 4, thrower))
            continue;

        world_path.clear();
        world_path.push_back(*invoke<const esp::Vector3*>(entity, 10));
        const auto velocity = field<esp::Vector3>(entity, offsets.velocity);
        const auto previous = field<esp::Vector3>(entity, offsets.previous_velocity);
        const auto dx = velocity.x - previous.x;
        const auto dy = velocity.y - previous.y;
        const auto dz = velocity.z - previous.z;
        const auto speed_delta_squared = (dy * dy + dx * dx) + dz * dz;
        const auto current_time = speed_delta_squared < 1.0f
                                      ? float(frame.current_tick) * environment.tick_interval
                                      : field<float>(entity, offsets.simulation_time);

        esp::GrenadeState prediction{};
        prediction.owner_entity = std::uint32_t(reinterpret_cast<std::uintptr_t>(entity));
        esp::simulate_grenade(prediction, entry.kind, field<esp::Vector3>(entity, offsets.origin),
                              velocity, current_time, entry.creation_time, environment, trace,
                              nullptr, nullptr,
                              field<std::int32_t>(entity, offsets.collision_group),
                              field<std::uint16_t>(entity, offsets.projectile_flags), &world_path);
        if (world_path.size() <= 1)
            continue;

        if (entry.kind == esp::GrenadeKind::explosive) {
            esp::finalize_grenade(prediction, trace);
            prediction.position.z += 1.0f;
            world_path.push_back(prediction.position);
        } else if (entry.kind == esp::GrenadeKind::molotov) {
            esp::finalize_grenade(prediction, trace);
            const auto endpoint = prediction.position;
            const auto last = world_path.back();
            if (last.x != endpoint.x || last.y != endpoint.y || last.z != endpoint.z)
                world_path.push_back(endpoint);
        }

        const auto color = frame.color;
        auto* context = invoke<void*>(bindings.material_system, 115);
        if (context)
            invoke<void>(context, 2);
        prepare_material(bindings);
        esp::Vector3 camera;
        invoke<void>(context, 152, &camera);
        auto* mesh = invoke<void*>(context, 62, true, static_cast<void*>(nullptr),
                                   static_cast<void*>(nullptr), material);
        std::vector<render::BeamSegment> segments;
        segments.reserve(world_path.size());
        for (const auto position : world_path)
            segments.push_back({position, 0.0f, color, 0.0f, 10.0f});
        render::BeamGeometry geometry;
        render::build_beam_geometry(segments, camera, geometry);
        render::draw_beam_geometry(mesh, geometry);
        invoke<void>(context, 3);
        invoke<void>(context, 1);
    }
}

void ProjectileRibbonOwner::draw(const ProjectileRibbonFrame& frame,
                                 std::span<const ProjectileRibbonEntry> entries,
                                 const ProjectileRibbonBindings& bindings,
                                 const esp::GrenadeEnvironment& environment,
                                 esp::GrenadeTraceProvider trace) {
    draw_impl(frame, entries, bindings, environment, trace, path_);
}
void ProjectileRibbonOwner::draw(const ProjectileRibbonLiveFrame& frame,
                                 std::span<const esp::GrenadeWarningRecord> entries,
                                 const ProjectileRibbonBindings& bindings,
                                 const esp::GrenadeEnvironmentBindings& environment,
                                 esp::GrenadeTraceProvider trace,
                                 std::vector<esp::Vector3>& world_path) {
    draw_impl(frame, entries, bindings, environment, trace, world_path);
}

} // namespace skeet::effects
