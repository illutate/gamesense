#include "skeet/client/player_physics_hooks.hpp"
#include "skeet/engine/offset_storage.hpp"
#include "player_physics_hooks_detail.hpp"
#include <intrin.h>
#include <stdexcept>

namespace skeet::client {
namespace {
PlayerPhysicsHooks* active_runtime;
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_PLAYER_PHYSICS_ABI __fastcall
#else
#define SKEET_PLAYER_PHYSICS_ABI
#endif
void* SKEET_PLAYER_PHYSICS_ABI studio_header_entry(void* player, void*) {
    return active_runtime->studio_header(player);
}
__declspec(noinline) void SKEET_PLAYER_PHYSICS_ABI
absolute_velocity_entry(void* player, void*, std::array<float, 3>* output) {
    auto* return_slot = static_cast<std::uintptr_t*>(_AddressOfReturnAddress());
    active_runtime->estimate_absolute_velocity(player, *output, *return_slot);
}
bool SKEET_PLAYER_PHYSICS_ABI collision_entry(void* player, void*, std::int32_t group,
                                              std::uint32_t mask) {
    return active_runtime->should_collide(player, group, mask);
}
void SKEET_PLAYER_PHYSICS_ABI interpolation_update_entry(void* player, void*, std::uint32_t a,
                                                         std::uint32_t b, std::uint32_t c,
                                                         std::uint32_t d) {
    active_runtime->update_interpolated_state(player, {a, b, c, d});
}
std::uint8_t SKEET_PLAYER_PHYSICS_ABI interpolate_entry(void* player, void*,
                                                        std::uint32_t time_bits) {
    return active_runtime->interpolate(player, time_bits);
}
std::array<float, 3>* SKEET_PLAYER_PHYSICS_ABI eye_position_entry(void* player, void*,
                                                                  std::array<float, 3>* output) {
    active_runtime->eye_position(player, *output);
    return output;
}
#undef SKEET_PLAYER_PHYSICS_ABI
}
PlayerPhysicsHookFields
native_player_physics_hook_fields(const engine::NativeOffsetGlobals& globals) {
    const auto decode = [&](std::uint32_t pointer, std::uint32_t mask, std::uint32_t key) {
        return std::ptrdiff_t(*globals.pointers.at(pointer) ^ globals.keys.at(mask) ^ key);
    };
    return {decode(0x4346A8E4, 0x434688CC, 0x36375C23), decode(0x434679F8, 0x434675F8, 0x3309C0CF)};
}
void* PlayerPhysicsHooks::native_replacement_entry(std::size_t slot, std::uint32_t native_entry) {
    void* entry = nullptr;
    if (slot == 109 && native_entry == 0x43402307U)
        entry = reinterpret_cast<void*>(&studio_header_entry);
    else if (slot == 145 && native_entry == 0x4333181EU)
        entry = reinterpret_cast<void*>(&absolute_velocity_entry);
    else if (slot == 173 && native_entry == 0x4333C562U)
        entry = reinterpret_cast<void*>(&collision_entry);
    else if (slot == 78 && native_entry == 0x433ABBF0U)
        entry = reinterpret_cast<void*>(&interpolation_update_entry);
    else if (slot == 113 && native_entry == 0x433875BEU)
        entry = reinterpret_cast<void*>(&interpolate_entry);
    else if (slot == 169 && native_entry == 0x43382222U)
        entry = reinterpret_cast<void*>(&eye_position_entry);
    else
        throw std::invalid_argument("Unknown native player physics hook replacement");
    return entry;
}
void PlayerPhysicsHooks::bind_native_replacement(engine::VtableHookMetadata& metadata,
                                                 std::size_t slot, std::uint32_t native_entry) {
    auto* entry = native_replacement_entry(slot, native_entry);
    active_runtime = this;
    player_physics_detail::bind_velocity_continuation(bindings.velocity_continuation_434757c4);
    metadata.replacement_table[slot] = entry;
}
} //namespace skeet::client
