#include "skeet/skins/native_catalog.hpp"
#include "skeet/menu/skins_menu.hpp"
#include "skeet/events/game_events.hpp"
#include <cstdlib>

namespace skeet::skins {
namespace {
// Native one-use buffers 0x434672A0 / 0x43467640, before their clearing pass.
constexpr std::string_view native_t =
    "Jumpsuit A\1tm_jumpsuit_varianta\2Jumpsuit B\1tm_jumpsuit_variantb\2"
    "Jumpsuit C\1tm_jumpsuit_variantc\2Pirate\1tm_pirate\2Pirate A\1tm_pirate_varianta\2"
    "Pirate B\1tm_pirate_variantb\2Pirate C\1tm_pirate_variantc\2Pirate D\1tm_pirate_variantd\2"
    "Professional\1tm_professional\2Professional 1\1tm_professional_var1\2"
    "Professional 2\1tm_professional_var2\2Professional 3\1tm_professional_var3\2Professional 4\1tm_professional_var4\2";
constexpr std::string_view native_ct =
    "FBI\1ctm_fbi\2FBI (Variant A)\1ctm_fbi_varianta\2FBI (Variant C)\1ctm_fbi_variantc\2"
    "FBI (Variant D)\1ctm_fbi_variantd\2FBI (Variant E)\1ctm_fbi_variante\2"
    "GIGN\1ctm_gign\2GIGN (Variant A)\1ctm_gign_varianta\2GIGN (Variant B)\1ctm_gign_variantb\2"
    "GIGN (Variant C)\1ctm_gign_variantc\2GIGN (Variant D)\1ctm_gign_variantd\2"
    "GSG-9\1ctm_gsg9\2GSG-9 (Variant A)\1ctm_gsg9_varianta\2GSG-9 (Variant B)\1ctm_gsg9_variantb\2"
    "GSG-9 (Variant C)\1ctm_gsg9_variantc\2GSG-9 (Variant D)\1ctm_gsg9_variantd\2"
    "SWAT\1ctm_swat\2SWAT (Variant A)\1ctm_swat_varianta\2SWAT (Variant B)\1ctm_swat_variantb\2"
    "SWAT (Variant C)\1ctm_swat_variantc\2SWAT (Variant D)\1ctm_swat_variantd\2";
}
bool NativeCatalog::available() {
    return engine_.acquire_schema();
}
std::span<const std::uint16_t> NativeCatalog::paintkits(std::uint16_t item) {
    const auto found = paintkits_.find(item);
    return found == paintkits_.end() ? std::span<const std::uint16_t>{} : found->second;
}
std::optional<std::u16string> NativeCatalog::paintkit_name(std::uint32_t id) {
    const auto* token = engine_.paintkit_token(id);
    return token ? engine_.localize(token) : std::nullopt;
}
std::span<const CatalogChoice> NativeCatalog::agent_models(std::int32_t team) {
    if (team == 2)
        return agents_t_;
    if (team == 3)
        return agents_ct_;
    return {};
}
std::span<const std::string> NativeCatalog::agent_paths(std::int32_t team) const {
    if (team == 2)
        return paths_t_;
    if (team == 3)
        return paths_ct_;
    return {};
}
void NativeCatalog::append_native_agents(std::string_view data, std::int32_t team) {
    auto& options = team == 2 ? agents_t_ : agents_ct_;
    auto& paths = team == 2 ? paths_t_ : paths_ct_;
    while (!data.empty()) {
        const auto first = data.find('\1');
        if (first == data.npos)
            return;
        const auto second = data.find('\2', first + 1);
        if (second == data.npos)
            return;
        const auto label = data.substr(0, first);
        const auto model = data.substr(first + 1, second - first - 1);
        paths.push_back("models/player/custom_player/legacy/" + std::string(model) + ".mdl");
        options.push_back(
            {std::uint32_t(paths.size() - 1), std::u16string(label.begin(), label.end())});
        data.remove_prefix(second + 1);
    }
}
void NativeCatalog::initialize(ui::SkinsMenu& menu) {
    if (available()) {
        const auto count = engine_.definition_count();
        std::int32_t processed = 0;
        for (std::uint32_t id = 0; id < 30000; ++id) {
            auto item = engine_.definition(id);
            if (!item)
                continue;
            if (item->type_token) {
                const auto type = events::event_hash(item->type_token);
                if (type == 0x2B77F7D8 && item->base_name_token && item->view_model) {
                    if (auto label = engine_.localize(item->base_name_token)) {
                        const std::string_view path = item->view_model;
                        const auto slash = path.rfind('/');
                        if (slash != path.npos && slash + 1 < path.size()) {
                            if (path[slash + 1] == 'c' && id != 0x13AD) {
                                paths_ct_.emplace_back(path);
                                agents_ct_.push_back(
                                    {std::uint32_t(paths_ct_.size() - 1), std::move(*label)});
                            } else if (path[slash + 1] == 't' && id != 0x13AC) {
                                paths_t_.emplace_back(path);
                                agents_t_.push_back(
                                    {std::uint32_t(paths_t_.size() - 1), std::move(*label)});
                            }
                        }
                    }
                } else if (type == 0x8E483246 && item->base_name_token && item->view_model &&
                           item->world_model) {
                    if (auto label = engine_.localize(item->base_name_token))
                        gloves_.push_back({id, std::move(*label)});
                }
            }
            if (++processed >= count)
                break;
        }
    }
    if (!native_agents_consumed_) {
        append_native_agents(native_t, 2);
        append_native_agents(native_ct, 3);
        native_agents_consumed_ = true;
    }
    menu.initialize_schema_options();
    auto directory = files_.current_directory();
    if (directory.empty())
        return;
    directory += u"\\csgo\\scripts\\items\\items_game.txt";
    auto contents = files_.read_file(directory);
    if (contents && !contents->empty())
        parse_weapon_icons(*contents);
}
void NativeCatalog::parse_weapon_icons(std::string_view text) {
    constexpr std::string_view opening = "\t\t\"weapon_icons\"\r\n\t\t{\r\n";
    constexpr std::string_view closing = "\t\t}\r\n\t}\r\n";
    constexpr std::string_view next = "\r\n\t\t\t\"";
    const auto begin = text.find(opening);
    if (begin == text.npos)
        return;
    auto cursor = begin + 0x1B;
    if (cursor > text.size())
        return;
    const auto close = text.find(closing, cursor);
    if (close == text.npos)
        return;
    const auto end = close - 6;
    std::uint32_t previous_paint = 0, previous_item = 0;
    const std::string terminated(text);
    while (cursor < text.size()) {
        const auto value = std::uint32_t(std::strtoul(terminated.c_str() + cursor, nullptr, 10));
        if (!value || value == 0xFFFFFFFF)
            return;
        const auto item = value >> 16, paint = (value >> 2) & 0x3FFF;
        if (previous_paint != paint || std::uint16_t(previous_item) != std::uint16_t(item)) {
            if (paint < 10000 && maximum_ < paint)
                maximum_ = paint;
            paintkits_[std::uint16_t(item)].push_back(std::uint16_t(paint));
            previous_item = item;
            previous_paint = paint;
        }
        if (cursor >= end)
            return;
        const auto found = text.find(next, cursor);
        if (found == text.npos || found >= end)
            return;
        cursor = found + next.size();
    }
}
} // namespace skeet::skins
