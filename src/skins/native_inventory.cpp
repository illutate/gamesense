#include "skeet/skins/native_inventory.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace skeet::skins {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void assign(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
struct Identity {
    std::uint32_t low, high;
};
bool local_identity(void* const& engine_client, Identity& value) {
    std::array<std::byte, 0x158> info{};
    const auto index = engine::invoke<std::int32_t>(engine_client, 12);
    const bool result = engine::invoke<bool>(engine_client, 8, index, info.data());
    value = {field<std::uint32_t>(info.data(), 8), field<std::uint32_t>(info.data(), 12)};
    return result;
}
}
NativeInventoryRuntime::NativeInventoryRuntime(const WeaponSkinsConfig& settings,
                                               NativeInventoryBindings bindings,
                                               CounterRandom& random, WeaponRefresh& refresh)
    : settings_(settings), bindings_(bindings), random_(random), refresh_(refresh) {}
std::int32_t NativeInventoryRuntime::stattrak_counter(void* item) {
    if (bindings_.counter_rows && bindings_.counters) {
        const auto* name = engine::invoke<const char*>(item, 143);
        if (name) {
            for (auto* row = bindings_.counter_rows - 4;; row += 0x1C) {
                const auto index = field<std::int32_t>(row, 0xC);
                if (index == -1)
                    break;
                if (field<std::int32_t>(row, 0x10) == -1 || index > 0x133)
                    continue;
                const auto* candidate = field<const char*>(row, 0);
                if (candidate && std::strcmp(name, candidate) == 0) {
                    const auto value = bindings_.counters[index];
                    if (value != -1)
                        return value;
                    break;
                }
            }
        }
    }
    return random_.random_integer(1, 10000);
}
void NativeInventoryRuntime::process(void* local_player, std::uintptr_t incoming) {
    if (!local_player)
        return;
    if (std::none_of(settings_.value.begin(), settings_.value.end(),
                     [](const WeaponSkin& value) { return value.enabled != 0; }))
        return;
    const auto& globals = bindings_.offsets;
    const auto offset = [&](std::uint32_t pointer, std::uint32_t mask, std::uint32_t key) {
        return std::ptrdiff_t(*globals.pointers.at(pointer) ^ globals.keys.at(mask) ^ key);
    };
    if (field<std::uint8_t>(local_player, offset(0x43468E78, 0x434684E4, 0x9D920E35)) != 0)
        return;
    const auto* snapshot = reinterpret_cast<const std::byte*>(incoming);
    const auto count = field<std::int32_t>(snapshot, 0x4044);
    if (count <= 0)
        return;
    const auto* entry = snapshot + 0x44;
    for (std::int32_t i = 0; i < count; ++i, entry += 8) {
        auto* networkable =
            engine::invoke<void*>(bindings_.resolver, 0, field<std::uint32_t>(entry, 0));
        if (!networkable)
            continue;
        auto* unknown = engine::invoke<void*>(networkable, 0);
        if (!unknown)
            continue;
        auto* base = engine::invoke<void*>(unknown, 7);
        if (!base || !engine::invoke<bool>(base, 166))
            continue;
        const auto tag = field<std::uint32_t>(entry, 4);
        auto* item = static_cast<std::byte*>(networkable) - 8;
        const auto owner_high =
            field<std::uint32_t>(item, offset(0x43468390, 0x434683E4, 0xA1493792));
        const auto owner_low =
            field<std::uint32_t>(item, offset(0x43469248, 0x4346798C, 0x3C1D8AA6));
        Identity identity{};
        const bool identity_valid = local_identity(bindings_.engine_client, identity);
        if (!identity_valid || (!identity.low && !identity.high) || owner_low != identity.low ||
            owner_high != identity.high) {
            if (field<std::uint32_t>(item, offset(0x43469248, 0x4346798C, 0x3C1D8AA6)) != 0 ||
                field<std::uint32_t>(item, offset(0x43468E5C, 0x43469334, 0x05FB21BA)) != 0 ||
                tag != 0)
                continue;
            const auto handle =
                field<std::uint32_t>(item, offset(0x4346A868, 0x43468998, 0x4A3412EC));
            const auto owner_index =
                handle == 0xFFFFFFFFu ? 0x1FFFu : std::uint32_t(static_cast<std::uint16_t>(handle));
            if (owner_index !=
                std::uint32_t(engine::invoke<std::int32_t>(bindings_.engine_client, 12)))
                continue;
        }
        const auto definition =
            field<std::uint16_t>(item, offset(0x4346908C, 0x43468BA0, 0x5C9F0FBB));
        std::int32_t team = -1;
        if (std::uint32_t(definition) - 500u < 26u)
            team = field<std::int32_t>(local_player, offset(0x434688B0, 0x4346A518, 0x8B5586F6));
        const auto slot = weapon_config_slot(definition, team);
        if (slot < 0)
            continue;
        const auto& skin = settings_.value[std::size_t(slot)];
        if (!skin.enabled)
            continue;
        assign(item, offset(0x43468C1C, 0x43468F28, 0x3C5CCD57), std::int32_t(-1));
        assign(item, offset(0x43467F9C, 0x43467188, 0xB4056E61), std::int32_t(-1));
        Identity account{};
        local_identity(bindings_.engine_client, account);
        if (account.low)
            assign(item, offset(0x4346A358, 0x43468B78, 0x1136BA97), account.low);
        const auto paintkit = offset(0x4346A8A8, 0x43468568, 0xC6D22EE2);
        const auto previous = field<std::int32_t>(item, paintkit);
        assign(item, paintkit, skin.paintkit);
        assign(item, offset(0x43468DC8, 0x43467A44, 0x2A595D03), skin.seed);
        assign(item, offset(0x43468950, 0x43469200, 0x67FC28B7), quality_to_wear(skin.quality));
        if (previous == skin.paintkit)
            continue;
        if (skin.stattrak) {
            const auto count = stattrak_counter(item);
            assign(item, offset(0x434682E8, 0x4346A6C8, 0xF2DC2C8A), count);
        }
        refresh_.schedule_full_update(refresh_.current_time() + 0.25f);
    }
}
} // namespace skeet::skins
