#include "skeet/esp/tablet_upgrade.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

#pragma STDC FENV_ACCESS ON

namespace skeet::esp {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void field(void* object, std::ptrdiff_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void* tablet(const TabletUpgradeBindings& b) {
    auto* local = b.local_player;
    if (!local)
        return nullptr;
    const auto handle = field<std::int32_t>(local, b.offsets.active_weapon);
    if (handle == -1)
        return nullptr;
    auto* weapon = engine::invoke<void*>(b.entity_list, 4, handle);
    return weapon && field<std::uint16_t>(weapon, b.offsets.definition) == 72 ? weapon : nullptr;
}
}
void prepare_tablet_upgrade(TabletUpgradeState& state, const TabletUpgradeBindings& b) {
    if (!b.settings.upgrade_tablet.value)
        return;
    auto* weapon = tablet(b);
    if (!weapon)
        return;
    state.status = field<std::uint8_t>(weapon, b.offsets.status);
    state.value_bits = field<decltype(state.value_bits)>(weapon, b.offsets.values);
    state.identifiers = field<decltype(state.identifiers)>(weapon, b.offsets.identifiers);
    state.time_bits = field<decltype(state.time_bits)>(weapon, b.offsets.times);
    const auto frame = *b.frame_count;
    const auto* time = b.current_time;
    state.frame = frame;
    field(weapon, b.offsets.status, std::uint8_t{0});
    for (std::int32_t desired = 1; desired <= 3; ++desired) {
        field(weapon, b.offsets.values + (desired - 1) * 4, std::uint32_t{0xBF800000u});
        std::int32_t slot = 0;
        for (; slot < 8; ++slot)
            if (field<std::int32_t>(weapon, b.offsets.identifiers + slot * 4) == desired)
                break;
        if (slot == 8) {
            for (slot = 0; slot < 8; ++slot)
                if (field<std::int32_t>(weapon, b.offsets.identifiers + slot * 4) == 0)
                    break;
        }
        if (slot == 8)
            continue;
        field(weapon, b.offsets.identifiers + slot * 4, desired);
        field(weapon, b.offsets.times + slot * 4, *time - 1.100000023841858f);
    }
}
void restore_tablet_upgrade(TabletUpgradeState& state, const TabletUpgradeBindings& b) {
    if (!b.settings.upgrade_tablet.value || state.frame != *b.frame_count)
        return;
    state.frame = -1;
    auto* weapon = tablet(b);
    if (!weapon)
        return;
    field(weapon, b.offsets.status, state.status);
    field(weapon, b.offsets.values, state.value_bits);
    field(weapon, b.offsets.identifiers, state.identifiers);
    field(weapon, b.offsets.times, state.time_bits);
}

PlayerEspUpdateHandler::PlayerEspUpdateHandler(engine::OwnerRegistryBinding registry,
                                               TabletUpgradeState& state,
                                               const TabletUpgradeBindings& bindings,
                                               effects::GlowRuntime& glow)
    : state_(state), bindings_(bindings), glow_(glow) {
    network::register_update_observer(registry, *this);
    vtable = &entries_;
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_ESP_MEMBER __thiscall
#define SKEET_ESP_FAST __fastcall
#else
#define SKEET_ESP_MEMBER
#define SKEET_ESP_FAST
#endif
void SKEET_ESP_MEMBER PlayerEspUpdateHandler::prepare(void* object) {
    auto& self =
        *static_cast<PlayerEspUpdateHandler*>(static_cast<network::UpdateObserver*>(object));
    prepare_tablet_upgrade(self.state_, self.bindings_);
}
void SKEET_ESP_MEMBER PlayerEspUpdateHandler::restore(void* object) {
    auto& self =
        *static_cast<PlayerEspUpdateHandler*>(static_cast<network::UpdateObserver*>(object));
    restore_tablet_upgrade(self.state_, self.bindings_);
}
void SKEET_ESP_MEMBER PlayerEspUpdateHandler::glow(void* object) {
    static_cast<PlayerEspUpdateHandler*>(static_cast<network::UpdateObserver*>(object))
        ->glow_.update();
}
void SKEET_ESP_MEMBER PlayerEspUpdateHandler::unchanged(void*) {} //433CCE8E
void SKEET_ESP_FAST PlayerEspUpdateHandler::unchanged_player(void*, void*, std::uintptr_t,
                                                             std::uintptr_t, std::uintptr_t) {
} //4337D653
void SKEET_ESP_FAST PlayerEspUpdateHandler::unchanged_view(void*, float*, float*, float*) {
} //4334A269
void SKEET_ESP_FAST PlayerEspUpdateHandler::unchanged_post(void*, void*) {} //433CCE8E
#undef SKEET_ESP_MEMBER
#undef SKEET_ESP_FAST
} // namespace skeet::esp
