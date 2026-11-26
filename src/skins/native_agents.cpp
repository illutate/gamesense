#include "skeet/skins/native_agents.hpp"
#include "skeet/network/data_update_observers.hpp"
#include <bit>
#include <cstring>

namespace skeet::skins {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
void set_model(void* object, std::ptrdiff_t offset, std::int32_t model) {
    const auto value = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(model));
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
}
NativeAgentRuntime::NativeAgentRuntime(const Settings& settings, NativeCatalog& catalog,
                                       NativeAgentBindings bindings, ModelInfo& models,
                                       StringTables& tables,
                                       network::DataUpdateObserverRegistry& observers)
    : settings_(settings), catalog_(catalog), bindings_(bindings), models_(models),
      tables_(tables) {
    observers.append(*this); // 4347DAB7: secondary receiver registered through 4337EFD9.
}
void NativeAgentRuntime::process(void* player, std::int32_t update) {
    (void)update;
    if (bindings_.original_model == -1 || !player)
        return;
    if (engine::invoke<bool>(bindings_.engine_client, 82))
        return;
    if (engine::invoke<bool>(bindings_.engine_client, 93))
        return;
    const auto& globals = bindings_.offsets;
    const auto offset = [&](std::uint32_t pointer, std::uint32_t mask, std::uint32_t key) {
        return std::ptrdiff_t(*globals.pointers.at(pointer) ^ globals.keys.at(mask) ^ key);
    };
    const auto team = field<std::int32_t>(player, offset(0x434688B0, 0x4346A518, 0x8B5586F6));
    bool enabled = false;
    std::uint32_t selected = 0;
    if (team == 2) {
        enabled = settings_.agent_t.value != 0;
        selected = static_cast<std::uint32_t>(settings_.agent_model_t.value);
    } else if (team == 3) {
        enabled = settings_.agent_ct.value != 0;
        selected = static_cast<std::uint32_t>(settings_.agent_model_ct.value);
    }
    if (enabled && field<std::uint8_t>(player, offset(0x43468360, 0x4346891C, 0x40C98EF6)) == 0) {
        const auto paths = catalog_.agent_paths(team);
        if (selected >= paths.size())
            return;
        const auto& path = paths[selected];
        const auto index = resolve_model_index(path, models_, tables_);
        if (index == -1)
            return;
        const auto* support = bindings_.support_models(const_cast<char*>(path.c_str()));
        if (support) {
            const auto* first = field<const char*>(support, 8);
            resolve_model_index(first ? std::string_view(first) : std::string_view{}, models_,
                                tables_);
            const auto* second = field<const char*>(support, 0xC);
            resolve_model_index(second ? std::string_view(second) : std::string_view{}, models_,
                                tables_);
        }
        if (field<std::uint8_t>(player, offset(0x43468E78, 0x434684E4, 0x9D920E35)) == 0) {
            set_model(player, offset(0x43468978, 0x43468F7C, 0x47877DEB), index);
            return;
        }
        const auto handle =
            field<std::uint32_t>(player, offset(0x434691A0, 0x434670F0, 0xD5D91363));
        if (handle == 0xFFFFFFFFu)
            return;
        auto* ragdoll = engine::invoke<void*>(bindings_.entity_list, 4, handle);
        if (ragdoll)
            set_model(ragdoll, offset(0x43468978, 0x43468F7C, 0x47877DEB), index);
        return;
    }
    if (bindings_.original_model == -1)
        return;
    const auto* handle = engine::invoke<const std::uint32_t*>(player, 2);
    if (*handle == bindings_.original_handle)
        set_model(player, offset(0x43468978, 0x43468F7C, 0x47877DEB), bindings_.original_model);
}
} // namespace skeet::skins
