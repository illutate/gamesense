#include "skeet/client/entity_command_owner.hpp"
#include "skeet/client/restricted_player_command.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::client {
namespace {
using Vector = std::array<float, 3>;
template <class T> T read(const void* object, std::ptrdiff_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
void copy_vector_words(void* destination, std::ptrdiff_t output, const void* source,
                       std::ptrdiff_t input) noexcept {
    for (std::ptrdiff_t word = 0; word != 3; ++word)
        write(destination, output + word * 4, read<std::uint32_t>(source, input + word * 4));
}
std::int32_t previous(std::int32_t number) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(number) - 1u);
}
float recoil_scale(const void* variable) noexcept {
    for (;;) {
        const auto* parent = read<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return std::bit_cast<float>(read<std::uint32_t>(variable, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(variable)));
}
float clamped_view_height(float height) noexcept {
    // Native MAXSS/MINSS choose the second operand for unordered inputs.
    const float lower = height > 46.0f ? height : 46.0f;
    return lower < 64.0f ? lower : 64.0f;
}
void* resolve_handle(const EntityCommandBindings& bindings, std::uint32_t handle) {
    return handle == UINT32_MAX ? nullptr : engine::invoke<void*>(bindings.entity_list, 4, handle);
}
// Complete43342DD7. Resolve the indexed view model first, then follow a live
// observer target only in mode four. The recursion retains the original index.
void* view_model(void* player, std::int32_t index, const EntityCommandBindings& bindings) {
    const auto& offsets = bindings.offsets;
    void* result =
        resolve_handle(bindings, read<std::uint32_t>(player, offsets.view_models_71dcee42 +
                                                                 std::ptrdiff_t(index) * 4));
    if (read<std::int32_t>(player, offsets.observer_mode_2f4e97c4) != 4)
        return result;
    auto* target =
        resolve_handle(bindings, read<std::uint32_t>(player, offsets.observer_target_3b704b41));
    if (target && target != player && read<std::uint8_t>(target, offsets.rejected_9d920e35) == 0 &&
        engine::invoke<std::int32_t>(target, 122) > 0 &&
        !engine::invoke<bool>(static_cast<std::byte*>(target) + 8, 9) &&
        read<std::uint8_t>(target, offsets.rejected_cde290d2) == 0)
        result = view_model(target, index, bindings);
    return result;
}
}

void* entity_command_view_model(EntityCommandOwner& owner, void* player, std::int32_t index) {
    return view_model(player, index, owner.bindings);
}
void capture_entity_prediction_history(EntityCommandOwner& owner, void* player,
                                       std::int32_t number) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    auto& entry = owner.state.history[number % 150];
    const bool first_capture = entry.command_number != number;
    if (first_capture)
        entry.state.flags &= 0xFFFFFFF1u;
    entry.command_number = number;
    write(&entry, 0x04, read<std::uint32_t>(player, offsets.tick_base));
    copy_vector_words(&entry, 0x08, player, offsets.vector_5ae32976);
    copy_vector_words(&entry, 0x14, player, offsets.vector_1bae1168);
    copy_vector_words(&entry, 0x20, player, offsets.vector_48442721);
    write(&entry, 0x2C, clamped_view_height(read<float>(player, offsets.view_offset + 8)));
    copy_vector_words(&entry, 0x30, player, offsets.network_origin);
    write(&entry, 0x3C, read<std::uint32_t>(player, offsets.scalar_2f33c681));
    const auto movement = read<std::uint32_t>(player, offsets.field_b5a66556);
    auto flags = (entry.state.flags & ~0x70u) | ((movement << 4u) & 0x70u);
    entry.state.flags = flags;
    write(&entry, 0x40, read<std::uint32_t>(player, offsets.shots_fired_8cfc633a));
    write(&entry, 0x44, read<std::uint32_t>(player, offsets.field_7f5ed2d0));
    write(&entry, 0x4C, read<std::uint8_t>(player, offsets.field_bcb263a5));
    write(&entry, 0x50, read<std::uint32_t>(player, offsets.field_66146802));
    flags &= ~1u;
    write(&entry, 0x54, read<std::uint32_t>(player, offsets.field_fb7e3404 + 4));
    const auto second_weapon_value = read<std::uint32_t>(player, offsets.field_fb7e3404 + 8);
    write(&entry, 0x5C, UINT32_MAX);
    write(&entry, 0x6C, std::uint32_t(0));
    write(&entry, 0x74, std::uint32_t(0));
    write(&entry, 0x58, second_weapon_value);
    write(&entry, 0x70, std::uint16_t(0));
    entry.state.flags = flags;

    auto* weapon = resolve_handle(bindings, read<std::uint32_t>(player, offsets.active_weapon));
    if (weapon) {
        // The lookup can change the player's handle; native rereads it for
        // publication instead of publishing the handle used by the lookup.
        write(&entry, 0x5C, read<std::uint32_t>(player, offsets.active_weapon));
        write(&entry, 0x60, read<std::uint32_t>(weapon, offsets.weapon_ammunition_ca5ec698));
        write(&entry, 0x64, read<std::uint32_t>(weapon, offsets.weapon_field_0176a381));
        write(&entry, 0x48, read<std::uint32_t>(weapon, offsets.weapon_field_6777c9ce));
        write(&entry, 0x68, read<std::uint32_t>(weapon, offsets.weapon_field_d2563274));
        write(&entry, 0x6C, read<std::uint32_t>(weapon, offsets.weapon_field_15335aac));
        if (engine::invoke<std::int32_t>(weapon, 455) == 9) {
            write(&entry, 0x70, read<std::uint8_t>(weapon, offsets.weapon_field_4013fc03));
            write(&entry, 0x71, read<std::uint8_t>(weapon, offsets.weapon_field_e865a877));
            const auto state = read<std::uint32_t>(weapon, offsets.weapon_field_3b93667b);
            entry.state.flags |= 1u;
            write(&entry, 0x74, state);
        }
        auto* model = view_model(
            player, read<std::int32_t>(weapon, offsets.weapon_viewmodel_index_c6c78c82), bindings);
        if (model)
            write(&entry, 0x78, read<std::uint32_t>(model, offsets.viewmodel_695ff325));
    }
    if (first_capture) {
        write(&entry, 0x7C, read<std::uint32_t>(player, offsets.player_field_6e9852d6));
        write(&entry, 0x80, read<std::uint32_t>(player, offsets.player_field_432b1f5b));
        write(&entry, 0x84, read<std::uint32_t>(player, offsets.player_field_0d560fac));
        auto* collision = engine::invoke<void*>(player, 3);
        const auto* maximum = engine::invoke<const Vector*>(collision, 2);
        write(&entry, 0x88, read<std::uint32_t>(maximum, 8));
    }
}

void finish_entity_prediction(EntityCommandOwner& owner, void* player, const Command& command) {
    const auto& bindings = owner.bindings;
    const auto& offsets = bindings.offsets;
    const float scale = recoil_scale(bindings.recoil_scale_convar);
    write(player, offsets.field_66146802, read<float>(player, offsets.vector_1bae1168) * scale);
    // Complete4340310B: this is the canonical434759B4 snapshot array, whose
    // unsigned modulo differs from the entity-history producer's signed one.
    const auto* animation_owner = prediction_history_shared(bindings.shared_animation_owner);
    if (animation_owner) {
        const auto& snapshot = animation_owner->snapshots[std::uint32_t(command.number) % 150u];
        if (snapshot.command_number == command.number)
            write(player, offsets.field_00a5bd68, read<std::uint8_t>(&snapshot, 4));
    }
    if (read<std::uint8_t>(player, offsets.rejected_9d920e35) == 0) {
        if (!(read<std::uint32_t>(player, offsets.ground_flags) & 2u) &&
            read<std::uint8_t>(player, offsets.field_6b1a59b5) == 0 &&
            read<std::uint8_t>(player, offsets.field_50f9ccc8) == 0) {
            if (read<float>(player, offsets.view_offset + 8) != 64.0f)
                write(player, offsets.view_offset + 8, 64.0f);
        } else if (read<std::uint8_t>(player, offsets.field_50f9ccc8) != 0 &&
                   read<std::uint8_t>(player, offsets.field_6b1a59b5) == 0) {
            if (read<float>(player, offsets.view_offset + 8) != 46.0f)
                write(player, offsets.view_offset + 8, 46.0f);
        }
    }
    const auto number = command.number;
    auto* weapon = resolve_handle(bindings, read<std::uint32_t>(player, offsets.active_weapon));
    if (weapon && read<std::uint16_t>(weapon, offsets.weapon_definition) == 64) {
        const auto preceding = previous(number);
        const auto& prior = owner.state.revolver[preceding % 150];
        const auto state = read<std::uint32_t>(weapon, offsets.weapon_state_9e2078a1);
        if (prior.command_number == preceding && read<std::uint32_t>(&prior, 4) != state &&
            state == 0xD0u) {
            const auto tick = previous(read<std::int32_t>(player, offsets.tick_base));
            const float postpone = float(tick) * *bindings.interval + owner.state.revolver_delay;
            write(weapon, offsets.postpone_fire, postpone);
        }
        auto& predicted = owner.state.revolver[number % 150];
        predicted.command_number = number;
        write(&predicted, 8, read<std::uint32_t>(weapon, offsets.postpone_fire));
        write(&predicted, 4, state);
    }
    capture_entity_prediction_history(owner, player, command.number);
}
} // namespace skeet::client
