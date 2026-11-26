#include "grenade_live_detail.hpp"

namespace skeet::esp {
using namespace grenade_live_detail;
GrenadeLiveScene::Storage::Storage(const GrenadeLiveBindings& b)
    : bindings(b), raw(b.queries.grenades()), offsets(b.queries.offsets()),
      auxiliary_context{b.player_adjustments,
                        b.convars.game_type,
                        b.convars.teammates_are_enemies,
                        b.local_player,
                        raw.bindings().entity_list,
                        raw,
                        b.math.trigonometry,
                        b.arc_tangent,
                        b.maximum_players,
                        raw.bindings().smoke_projectile_class,
                        GrenadeAuxiliaryLiveOffsets(b.queries.offsets()),
                        b.trace_flag} {}
bool GrenadeLiveScene::Storage::enemy(void* entity) const {
    auto* local = bindings.local_player;
    if (!local || !entity || local == entity)
        return false;
    const auto& offsets = raw.bindings().offsets;
    if (convar_integer(bindings.convars.game_type) == 6) {
        const auto offset = std::ptrdiff_t(offsets.survival_team);
        const auto team = field<std::int32_t>(local, offset);
        return team == -1 || team != field<std::int32_t>(entity, offset);
    }
    const auto offset = std::ptrdiff_t(offsets.team);
    const auto team = field<std::int32_t>(local, offset);
    const auto other = field<std::int32_t>(entity, offset);
    if (team == 2) {
        if (other == 3)
            return true;
    } else if (team == 3) {
        if (other == 2)
            return true;
    } else
        return false;
    return convar_integer(bindings.convars.teammates_are_enemies) != 0;
}
bool GrenadeLiveScene::Storage::game_flag(std::ptrdiff_t offset) const {
    auto* slot = bindings.game_rules_slot;
    if (!slot)
        return false;
    auto* rules = field<void*>(slot, 0);
    return rules && field<std::uint8_t>(rules, offset) != 0;
}
std::int32_t GrenadeLiveScene::Storage::team(void* entity) {
    const auto& o = raw.bindings().offsets;
    if (!raw.dormant(entity)) {
        if (convar_integer(bindings.convars.game_type) == 6)
            return field<std::int32_t>(entity, o.survival_team);
    } else {
        const auto index = raw.index(entity);
        auto* slot = raw.bindings().player_resource_slot;
        if (slot) {
            auto* resource = field<void*>(slot, 0);
            if (resource && field<std::uint8_t>(resource, o.cached_team_valid + index))
                return field<std::int32_t>(resource, o.cached_team + index * 4);
        }
    }
    return field<std::int32_t>(entity, o.team);
}
void* GrenadeLiveScene::Storage::thrower(void* projectile, std::uint32_t type) {
    const auto& o = raw.bindings().offsets;
    if (type > 2)
        return nullptr;
    const auto handle =
        field<std::uint32_t>(projectile, type < 2 ? o.thrower_handle : o.owner_handle);
    if (handle == 0xffffffffu)
        return nullptr;
    auto* owner = raw.by_handle(handle);
    return owner && raw.player(owner) ? owner : nullptr;
}
bool GrenadeLiveScene::Storage::admitted(std::uint32_t type, void* owner, void* selected) {
    if (!owner || !raw.player(owner))
        return true;
    const auto& c = bindings.convars;
    if (owner == selected)
        return true;
    const auto owner_team = team(owner);
    const auto local_team = team(selected);
    if (owner_team != local_team || convar_integer(bindings.convars.teammates_are_enemies))
        return true;
    if (!convar_integer(c.friendly_fire))
        return false;
    return convar_float(type == 0 ? c.teammate_he_damage : c.teammate_fire_damage) != 0.0f;
}
float GrenadeLiveScene::Storage::damage_scale(std::uint32_t handle, std::uint32_t type) {
    auto* projectile = handle == 0xffffffffu ? nullptr : raw.by_handle(handle);
    auto* local = bindings.local_player;
    if (!projectile || !local)
        return 1.0f;
    auto* owner = thrower(projectile, type);
    const auto friendly = convar_integer(bindings.convars.friendly_fire) != 0;
    const auto& o = raw.bindings().offsets;
    bool teammate = false;
    if (owner && owner != local &&
        field<std::int32_t>(local, o.team) == field<std::int32_t>(owner, o.team)) {
        const auto owner_index = raw.index(owner);
        const auto local_index = raw.index(local);
        if (owner_index == local_index)
            teammate = true;
        else {
            const auto owner_team = field<std::int32_t>(owner, o.team);
            const auto local_team = field<std::int32_t>(local, o.team);
            teammate = (!convar_integer(bindings.convars.teammates_are_enemies) &&
                        owner_team == local_team);
        }
    }
    if (teammate && (!friendly || game_flag(offsets.game_player_override)))
        return 0.0f;
    if (owner) {
        const auto owner_index = raw.index(owner);
        const auto local_index = raw.index(local);
        if (owner_index != local_index)
            (void)convar_integer(bindings.convars.teammates_are_enemies);
        if (owner == local && type == 0)
            return convar_float(bindings.convars.self_he_damage);
    }
    if (!teammate)
        return 1.0f;
    const auto scale = convar_float(type == 0 ? bindings.convars.teammate_he_damage
                                              : bindings.convars.teammate_fire_damage);
    return game_flag(offsets.teammate_damage_disabled) ? 0.0f : scale;
}
GrenadeLiveScene::GrenadeLiveScene(const GrenadeLiveBindings& bindings)
    : storage_(std::make_unique<Storage>(bindings)) {}
GrenadeLiveScene::~GrenadeLiveScene() = default;
GrenadeTraceProvider GrenadeLiveScene::trace() noexcept {
    return storage_->raw.provider(storage_->bindings.trace_flag);
}
const GrenadeEnvironmentBindings& GrenadeLiveScene::environment() const noexcept {
    return storage_->bindings.queries.environment();
}
GrenadeWarningEngine& GrenadeLiveScene::engine() noexcept {
    return storage_->raw;
}
MovementCollisionEngine& GrenadeLiveScene::movement() noexcept {
    return storage_->bindings.queries.movement();
}
const GrenadeDamageMath& GrenadeLiveScene::damage_math() const noexcept {
    return storage_->bindings.math;
}
const GrenadeAuxiliaryLiveContext& GrenadeLiveScene::auxiliary() {
    return storage_->auxiliary_context;
}
GrenadeDamageSelection GrenadeLiveScene::select_damage(Vector3 position,
                                                       const IntegerConfig& threshold) {
    return storage_->select_damage(position, threshold);
}
void GrenadeLiveScene::assess_warning(GrenadeWarningRecord& record, void* player,
                                      std::uint32_t& buttons) {
    storage_->assess(record, player, buttons);
}
void GrenadeLiveScene::update_remote_warnings(GrenadeWarningOwner& owner,
                                              const GrenadeEspConfig& config, void* player,
                                              std::uintptr_t incoming) {
    storage_->update(owner, config, player, incoming);
}
} // namespace skeet::esp
