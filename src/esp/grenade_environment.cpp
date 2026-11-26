#include "skeet/esp/grenade_environment.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::esp {
namespace {
float convar_float(const void* object) noexcept {
    for (;;) {
        const void* parent;
        std::memcpy(&parent, static_cast<const std::byte*>(object) + 0x1C, sizeof(parent));
        if (parent == object)
            break;
        object = parent;
    }
    std::uint32_t encoded;
    std::memcpy(&encoded, static_cast<const std::byte*>(object) + 0x2C, sizeof(encoded));
    return std::bit_cast<float>(encoded ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
}
GrenadeEnvironmentOffsets::GrenadeEnvironmentOffsets(const engine::NativeOffsetGlobals& g)
    : fire_origin_a9ee6062{g, 0x434670CC, 0x43468BCC, 0xA9EE6062, 0},
      fire_count_3584e3a5{g, 0x43468E28, 0x4346893C, 0x3584E3A5, 0},
      fire_active_fc1ef3a1{g, 0x43467D4C, 0x4346711C, 0xFC1EF3A1, 0},
      fire_x_24a16ff6{g, 0x43468A58, 0x43467574, 0x24A16FF6, 0},
      fire_y_94e8c7f7{g, 0x43469058, 0x4346892C, 0x94E8C7F7, 0},
      fire_z_d7600aac{g, 0x434671E8, 0x43468DDC, 0xD7600AAC, 0},
      collision_flags_b7e9c94e{g, 0x43467044, 0x4346A3BC, 0xB7E9C94E, 0},
      movement_type_7501cb91{g, 0x43468BD4, 0x4346A678, 0x7501CB91, 0},
      state_f0374295{g, 0x4346A360, 0x43468314, 0xF0374295, 0},
      owner_8d1bd8d0{g, 0x43467FCC, 0x43467938, 0x8D1BD8D0, 0},
      activity_8dbc9874{g, 0x43468BD0, 0x43469278, 0x8DBC9874, 0},
      activity_68140cd8{g, 0x43467948, 0x4346919C, 0x68140CD8, 0} {}

GrenadeEnvironmentOffsets
capture_grenade_environment_offsets(const engine::NativeOffsetGlobals& globals) {
    return GrenadeEnvironmentOffsets(globals);
}
float GrenadeEnvironmentBindings::gravity() const noexcept {
    return convar_float(gravity_convar.get());
}
float GrenadeEnvironmentBindings::incendiary_duration() const noexcept {
    return convar_float(incendiary_duration_convar.get());
}
float GrenadeEnvironmentBindings::incendiary_max_slope() const noexcept {
    return convar_float(incendiary_max_slope_convar.get());
}
std::uint32_t GrenadeEnvironmentBindings::world_entity() const {
    return std::uint32_t(
        reinterpret_cast<std::uintptr_t>(engine::invoke<void*>(entity_list, 3, std::int32_t(0))));
}
} // namespace skeet::esp
