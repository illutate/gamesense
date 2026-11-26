#include "skeet/engine/build_skin_receive_patches.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/skins/agent_receive.hpp"
#include "skeet/skins/sequence_receive.hpp"
#include <bit>

namespace skeet::engine {
namespace {
constexpr std::array<BuildSkinReceivePatchRecipe, 5> recipes{{
    {0xA48u, 0xDD75A186u, 0xB05C42F2u, 0xA5BF8ECDu, 0x434769BCu, "CBaseViewModel",
     "DT_BaseViewModel", "m_nSequence"},
    {0xA5Cu, 0xDD75A186u, 0xB05C42F2u, 0x0BA094FBu, 0x434769ACu, "CBaseViewModel",
     "DT_BaseViewModel", "m_nModelIndex"},
    {0xA70u, 0xDD75A186u, 0xB05C42F2u, 0x91E87C84u, 0x43476B0Cu, "CBaseViewModel",
     "DT_BaseViewModel", "m_nNewSequenceParity"},
    {0xA84u, 0xAEEB25F3u, 0x6B34DA8Fu, 0x623C927Eu, 0x43476A6Cu, "CBaseWeaponWorldModel",
     "DT_BaseWeaponWorldModel", "m_hCombatWeaponParent"},
    {0xAFCu, 0x0F0EDE49u, 0x8C24DA3Du, 0x0BA094FBu, 0x43476A8Cu, "CBaseEntity", "DT_BaseEntity",
     "m_nModelIndex"},
}};
static_assert(sizeof(client::EntityUpdatePatch) == 16);
static_assert(offsetof(client::EntityUpdatePatch, target) == 4);
static_assert(offsetof(client::EntityUpdatePatch, after) == 8);
static_assert(offsetof(client::EntityUpdatePatch, before) == 12);
}
std::span<const BuildSkinReceivePatchRecipe, 5> build_skin_receive_patch_manifest() noexcept {
    return recipes;
}
BuildSkinReceivePatchFailure::BuildSkinReceivePatchFailure(std::size_t at,
                                                           const BuildSkinReceivePatchRecipe& row,
                                                           BuildSkinReceivePatchFailureReason why)
    : std::runtime_error("Required skin receive patch cannot be initialized"), index(at),
      recipe(row), reason(why) {}
BuildSkinReceivePatches::BuildSkinReceivePatches(NativeDataImage& data)
    : data_(data),
      sequence_{{&data.native_cell<client::EntityUpdatePatch>(recipes[0].patch_address),
                 &data.native_cell<client::EntityUpdatePatch>(recipes[1].patch_address),
                 &data.native_cell<client::EntityUpdatePatch>(recipes[2].patch_address),
                 &data.native_cell<client::EntityUpdatePatch>(recipes[3].patch_address)}},
      agent_(data.native_cell<client::EntityUpdatePatch>(recipes[4].patch_address)) {}
BuildSkinReceivePatches initialize_build_skin_receive_patches(NativeDataImage& data) {
    auto* client = data.native_cell<void*>(0x434683E0u);
    if (!client)
        throw std::invalid_argument("Skin receive patches require canonical VClient018");
    struct Capture {
        client::EntityUpdatePatch* node;
        RecvProp* property;
        RecvVarProxy original;
    };
    std::array<Capture, 5> captures;
    const auto sequence = skins::sequence_receive_descriptors();
    const auto& agent = skins::agent_model_receive_descriptor();
    for (std::size_t i = 0; i < recipes.size(); ++i) {
        const auto& row = recipes[i];
        // Verify the receiver and discovery manifests cannot silently drift.
        const auto class_hash = i < 4 ? sequence[i].class_hash : agent.class_hash;
        const auto table_hash = i < 4 ? sequence[i].table_hash : agent.table_hash;
        const auto property_hash = i < 4 ? sequence[i].property_hash : agent.property_hash;
        const auto patch_address = i < 4 ? sequence[i].patch_address : agent.patch_address;
        if (row.class_hash != class_hash || row.table_hash != table_hash ||
            row.property_hash != property_hash || row.patch_address != patch_address ||
            netvar_name_hash(row.class_name) != row.class_hash ||
            netvar_name_hash(row.table_name) != row.table_hash ||
            netvar_name_hash(row.property_name) != row.property_hash)
            throw std::logic_error("Skin receive discovery and callback manifests differ");
        auto& node = data.native_cell<client::EntityUpdatePatch>(row.patch_address);
        if (node.target || node.after || node.before)
            throw BuildSkinReceivePatchFailure(
                i, row, BuildSkinReceivePatchFailureReason::already_initialized);
        auto* property =
            find_receive_property(client, row.class_hash, row.table_hash, row.property_hash);
        if (!property)
            throw BuildSkinReceivePatchFailure(
                i, row, BuildSkinReceivePatchFailureReason::missing_property);
        if (!property->proxy)
            throw BuildSkinReceivePatchFailure(
                i, row, BuildSkinReceivePatchFailureReason::missing_original);
        captures[i] = {&node, property, property->proxy};
    }
    for (const auto& capture : captures) {
        capture.node->target = reinterpret_cast<volatile std::int32_t*>(&capture.property->proxy);
        capture.node->after = std::bit_cast<std::int32_t>(
            std::uint32_t(reinterpret_cast<std::uintptr_t>(capture.original)));
    }
    return BuildSkinReceivePatches(data);
}
} //namespace skeet::engine
