#include "skeet/effects/glow_engine.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/rage/targeting.hpp"
#include <cstring>

namespace skeet::effects {
namespace {
template <class T> T field(const void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
}
std::uint32_t GlowEntityEngine::offset(std::uint32_t pointer, std::uint32_t key,
                                       std::uint32_t consumer) const {
    const auto& globals = bindings_.offsets.globals;
    return engine::decode_receive_offset(globals.pointers.at(pointer), globals.keys.at(key),
                                         consumer);
}
void* GlowEntityEngine::by_index(std::int32_t index) const {
    return engine::invoke<void*>(bindings_.entity_list, 3, index);
}
void* GlowEntityEngine::by_handle(std::int32_t handle) const {
    return engine::invoke<void*>(bindings_.entity_list, 4, handle);
}
std::int32_t GlowEntityEngine::entity_limit() const {
    return field<std::int32_t>(bindings_.entity_list, 0x24);
}
bool GlowEntityEngine::life_state(void* entity) const {
    return field<std::uint8_t>(entity, offset(0x43468E78u, 0x434684E4u, 0x9D920E35u)) != 0;
}
bool GlowEntityEngine::suppressed(void* entity) const {
    return field<std::uint8_t>(entity, offset(0x4346A5DCu, 0x43468430u, 0xCDE290D2u)) != 0;
}
bool GlowEntityEngine::eligibility_state(void* entity) const {
    return field<std::uint8_t>(entity, offset(0x43468BF8u, 0x43467B8Cu, 0xD070684Cu)) != 0;
}
std::uint32_t GlowEntityEngine::player_glow_index(void* entity) const {
    return field<std::uint32_t>(entity, offset(0x434670F4u, 0x43467B94u, 0x442A38AEu));
}
std::uint32_t GlowEntityEngine::weapon_glow_index(void* entity) const {
    return field<std::uint32_t>(entity, offset(0x4346A62Cu, 0x43467D24u, 0x442A38AEu));
}
bool GlowEntityEngine::bomb_active(void* entity) const {
    return field<std::uint8_t>(entity, offset(0x4346A6C4u, 0x43468EF4u, 0x0E5BECE5u)) != 0;
}
std::uint32_t GlowEntityEngine::bomb_glow_offset() const {
    return offset(0x43467E54u, 0x4346A72Cu, 0xA7B0989Eu);
}
bool GlowEntityEngine::enemy(void* local, void* target) const {
    // Reuse the complete4339EF8C predicate. Its two property operands and
    // decoded ConVar objects are bound at this actual call, after eligibility.
    rage::TargetOffsets offsets{};
    offsets.team = offset(0x434688B0u, 0x4346A518u, 0x8B5586F6u);
    offsets.survival_team = offset(0x434678ACu, 0x43467B7Cu, 0x430C8993u);
    const rage::TargetBindings targeting{local,
                                         bindings_.entity_list,
                                         bindings_.game_type,
                                         bindings_.teammates_are_enemies,
                                         bindings_.player_adjustments,
                                         offsets};
    return rage::target_is_enemy(targeting, target);
}
bool GlowEntityEngine::disable_visuals(std::uint32_t index) const noexcept {
    return bindings_.player_adjustments.records[index].adjustments.disable_visuals != 0;
}
} //namespace skeet::effects
