#include "skeet/skins/model_resources.hpp"
#include "skeet/events/game_events.hpp"

namespace skeet::skins {
std::int32_t resolve_model_index(std::string_view path, ModelInfo& models, StringTables& tables) {
    if (path.empty() || path.front() == '\0')
        return -1;
    auto index = models.model_index(path);
    if (index != -1)
        return index;
    StringTable* precache = nullptr;
    const auto count = tables.count();
    for (std::int32_t i = 0; i < count; ++i) {
        auto* table = tables.at(i);
        if (!table)
            continue;
        const auto* name = table->name();
        if (name && events::event_hash(name) == 0x9FE6B136) {
            precache = table;
            break;
        }
    }
    if (precache && models.load_model(path))
        index = precache->add_string(0, path, -1, nullptr);
    return index;
}
} // namespace skeet::skins
