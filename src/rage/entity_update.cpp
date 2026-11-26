#include "skeet/rage/entity_update.hpp"
#include "skeet/client/entity_command_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
std::uint32_t level_hash(const char* name) noexcept { //43359C70
    std::uint32_t result = 0x811C9DC5;
    for (auto* current = reinterpret_cast<const unsigned char*>(name); *current; ++current)
        result = (result ^ *current) * 0x1000193u;
    return result ^ 0x92CA5DF1u;
}
}
void update_rage_entities(const EntityUpdateBindings& bindings, void* local,
                          const client::EntityPredictionEntities& incoming) {
    if (!local)
        return;
    const auto* level = engine::invoke<const char*>(bindings.engine_client, 53);
    if (!level || level_hash(level) != 0xDC515205u)
        return;
    const auto maximum = *bindings.maximum_clients;
    for (std::int32_t index = 0; index < incoming.count; ++index) {
        const auto entity_index = incoming.entries[std::size_t(index)].entity_index;
        if (entity_index <= maximum || entity_index > 0x800 ||
            (std::uint32_t(entity_index) & 0xFFFF0000u))
            continue;
        auto* entity = engine::invoke<void*>(bindings.entity_list, 3, entity_index);
        if (!entity)
            continue;
        auto* actual_class = engine::invoke<void*>(static_cast<std::byte*>(entity) + 8, 2);
        if (actual_class != bindings.required_class)
            continue;
        if (field<std::uint8_t>(entity, bindings.offsets.movement_type) != 7)
            continue;
        auto* collision = engine::invoke<void*>(entity, 3);
        if (engine::invoke<std::int32_t>(collision, 11) != 1)
            continue;
        collision = engine::invoke<void*>(entity, 3);
        if (!(engine::invoke<std::uint32_t>(collision, 12) & 0x800u))
            continue;
        if (field<std::int32_t>(entity, bindings.offsets.owner_handle) != -1)
            continue;
        const auto model_index = field<std::int16_t>(entity, bindings.offsets.model_index);
        if (model_index <= 1)
            continue;
        auto* model = engine::invoke<void*>(bindings.model_info, 1, std::int32_t(model_index));
        if (!model)
            continue;
        const auto* name = engine::invoke<const char*>(bindings.model_info, 3, model);
        if (!name || *name != '*')
            continue;
        const std::uint32_t invalid = UINT32_MAX;
        std::memcpy(static_cast<std::byte*>(entity) + bindings.offsets.health, &invalid,
                    sizeof invalid);
    }
}
}
