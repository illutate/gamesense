#include "skeet/skins/apply.hpp"
#include <algorithm>

namespace skeet::skins {
float quality_to_wear(std::int32_t quality) noexcept {
    const float scaled = static_cast<float>(quality) * 0.01f;
    return std::max(1.0f - scaled, 0.00001f);
}
bool apply_attributes(const WeaponSkin& record, FallbackAttributes& item, std::uint32_t account,
                      std::string_view name, std::span<const CounterClass> classes,
                      CounterRandom& rng, Refresh& refresh) {
    item.item_id_high = -1;
    item.item_id_low = -1;
    if (account)
        item.account_id = account;
    const auto old_paint = item.paintkit;
    item.paintkit = record.paintkit;
    item.seed = record.seed;
    item.wear = quality_to_wear(record.quality);
    if (old_paint == record.paintkit)
        return false;
    if (record.stattrak) {
        auto count = -1;
        for (const auto& entry : classes) {
            if (entry.catalog_index == -1)
                break;
            if (entry.valid && entry.catalog_index <= 0x133 && entry.name == name) {
                count = entry.counter;
                break;
            }
        }
        if (count == -1)
            count = rng.random_integer(1, 10000);
        item.stattrak = count;
    }
    refresh.schedule_full_update(refresh.current_time() + 0.25f);
    return true;
}
} // namespace skeet::skins
