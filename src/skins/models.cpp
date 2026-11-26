#include "skeet/skins/models.hpp"
#include <array>

namespace skeet::skins {
namespace {
constexpr std::array<KnifeModel, 19> models{{
    {500, 0x4A39E8B9, 1, 1, 0}, {505, 0x785D931C, 1, 1, 0}, {506, 0x8D5BF97D, 1, 1, 0},
    {507, 0x606AF709, 1, 1, 0}, {508, 0x697E90CE, 1, 1, 0}, {509, 0x50E1EA52, 1, 1, 0},
    {515, 0x5EC93922, 3, 2, 0}, {512, 0xE37F744C, 2, 1, 0}, {516, 0x3BCCCF3B, 1, 1, 1},
    {514, 0x913A7A98, 1, 1, 0}, {519, 0xFB1B5F45, 2, 2, 0}, {520, 0x97A409DE, 1, 1, 0},
    {522, 0xC93B4D19, 2, 1, 0}, {523, 0x33AF4B65, 2, 1, 0}, {503, 0x8DBBB37E, 2, 1, 0},
    {517, 0xB733813F, 2, 2, 0}, {518, 0x5A3FA2DF, 2, 2, 0}, {521, 0x7A2E3B99, 2, 2, 0},
    {525, 0x77EAAA08, 2, 2, 0},
}};
}
std::span<const KnifeModel, 19> knife_models() noexcept {
    return models;
}
const KnifeModel* knife_model(std::int32_t selection) noexcept {
    return std::uint32_t(selection) < models.size() ? &models[std::size_t(selection)] : nullptr;
}
} // namespace skeet::skins
