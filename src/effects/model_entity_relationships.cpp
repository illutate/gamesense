#include "skeet/effects/model_entity_relationships.hpp"
#include "skeet/effects/model_render.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <bit>

namespace skeet::effects {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::int32_t convar_integer(engine::NativeConVarReference reference) noexcept {
    const void* object = reference.get();
    for (;;) {
        const auto* parent = field<const void*>(object, 0x1C);
        if (parent == object)
            break;
        object = parent;
    }
    return std::bit_cast<std::int32_t>(field<std::uint32_t>(object, 0x30) ^
                                       std::uint32_t(reinterpret_cast<std::uintptr_t>(object)));
}
struct Live {
    const ModelEntityRelationships& value;
    void* engine() const noexcept {
        return value.engine;
    }
    void* entity_list() const noexcept {
        return value.entity_list;
    }
    void* local_player() const noexcept {
        return value.local_player;
    }
    const ModelRelationshipOffsets& offsets() const noexcept {
        return value.offsets;
    }
    std::int32_t game_type() const noexcept {
        return convar_integer(value.game_type);
    }
    bool teammates_are_enemies() const noexcept {
        return convar_integer(value.teammates_are_enemies) != 0;
    }
};
struct Snapshot {
    const ModelEntityBindings& value;
    void* engine() const noexcept {
        return value.engine;
    }
    void* entity_list() const noexcept {
        return value.entity_list;
    }
    void* local_player() const noexcept {
        return value.local_player;
    }
    ModelRelationshipOffsets offsets() const noexcept {
        return {value.life_state,      value.move_type, value.observer_mode,
                value.observer_target, value.team,      value.survival_team};
    }
    std::int32_t game_type() const noexcept {
        return value.game_type;
    }
    bool teammates_are_enemies() const noexcept {
        return value.teammates_are_enemies;
    }
};
template <class Inputs> bool teammate(void* entity, const Inputs& b) {
    const auto& offsets = b.offsets();
    if (engine::invoke<bool>(b.engine(), 93))
        return field<std::int32_t>(entity, offsets.team) == 3;
    auto* local = b.local_player();
    if (local && field<std::uint8_t>(local, offsets.life_state) != 0 &&
        field<std::uint8_t>(local, offsets.move_type) == 10) {
        const auto mode = field<std::int32_t>(local, offsets.observer_mode);
        const auto handle = field<std::uint32_t>(local, offsets.observer_target);
        if ((mode == 4 || mode == 5) && handle != UINT32_MAX) {
            auto* target = engine::invoke<void*>(b.entity_list(), 4, handle);
            if (target && engine::invoke<bool>(target, 158))
                local = target;
        }
    }
    if (!local)
        return false;
    const auto local_team = field<std::int32_t>(local, offsets.team);
    if (local_team == 1)
        return field<std::int32_t>(entity, offsets.team) == 3;
    bool enemy = false;
    if (entity && local != entity) {
        if (b.game_type() == 6) {
            const auto survival = field<std::int32_t>(local, offsets.survival_team);
            enemy =
                survival == -1 || survival != field<std::int32_t>(entity, offsets.survival_team);
        } else if (local_team == 2 || local_team == 3) {
            const auto team = field<std::int32_t>(entity, offsets.team);
            enemy = (local_team == 2 && team == 3) || (local_team == 3 && team == 2) ||
                    b.teammates_are_enemies();
        }
    }
    return !enemy;
}
}
bool model_teammate(void* entity, const ModelEntityRelationships& bindings) {
    return teammate(entity, Live{bindings});
}
bool model_teammate(void* entity, const ModelEntityBindings& bindings) {
    return teammate(entity, Snapshot{bindings});
}
} //namespace skeet::effects
