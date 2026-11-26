#include "skeet/client/player_receive_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/animation/fix_runtime_owner.hpp"
#include "skeet/animation/fix_animations.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include <array>
#include <bit>
#include <stdexcept>

namespace skeet::client {
namespace {
PlayerReceiveRuntime* active_players = nullptr;
animation::LayerReceiveRuntime* active_layers = nullptr;
template <unsigned Kind>
void receive(const engine::RecvProxyData* packet, void* entity, void* output) {
    if constexpr (Kind == 0)
        active_players->simulation_time(packet, entity, output);
    else if constexpr (Kind == 1)
        active_players->tick_base(packet, entity, output);
    else if constexpr (Kind < 5)
        active_players->velocity(packet, entity, output, Kind - 2);
    else if constexpr (Kind < 7)
        active_players->eye_angle(packet, entity, output, Kind - 5);
    else if constexpr (Kind == 7)
        active_players->client_animation(packet, entity, output);
    else
        active_layers->receive(static_cast<animation::ReceivedLayerField>(Kind - 8), packet, entity,
                               output);
}
constexpr std::uint32_t hash(const char* text) {
    std::uint32_t value = 0x811C9DC5u;
    for (; *text; ++text)
        value = (value ^ std::uint8_t(*text)) * 0x1000193u;
    return value ^ 0x92CA5DF1u;
}
constexpr std::array<PlayerReceiveDescriptor, 15> descriptors{
    {{0x91C, hash("CBaseEntity"), hash("DT_BaseEntity"), hash("m_flSimulationTime"), 0x433FCA81,
      0x434769DC, receive<0>},
     {0x930, hash("CBasePlayer"), hash("DT_LocalPlayerExclusive"), hash("m_nTickBase"), 0x43402A54,
      0x43476ABC, receive<1>},
     {0x944, hash("CBasePlayer"), hash("DT_LocalPlayerExclusive"), hash("m_vecVelocity[0]"),
      0x433C6496, 0x4347694C, receive<2>},
     {0x958, hash("CBasePlayer"), hash("DT_LocalPlayerExclusive"), hash("m_vecVelocity[1]"),
      0x43343176, 0x434769FC, receive<3>},
     {0x96C, hash("CBasePlayer"), hash("DT_LocalPlayerExclusive"), hash("m_vecVelocity[2]"),
      0x43417E35, 0x43476A5C, receive<4>},
     {0x980, hash("CCSPlayer"), hash("DT_CSPlayer"), hash("m_angEyeAngles[0]"), 0x4341DC67,
      0x4347698C, receive<5>},
     {0x994, hash("CCSPlayer"), hash("DT_CSPlayer"), hash("m_angEyeAngles[1]"), 0x4336B0E6,
      0x43476A4C, receive<6>},
     {0x9A8, hash("CBaseAnimating"), hash("DT_BaseAnimating"), hash("m_bClientSideAnimation"),
      0x4338A848, 0x434769CC, receive<7>},
     {0x9BC, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_flCycle"),
      0x433898BD, 0x43476AAC, receive<8>},
     {0x9D0, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_flPrevCycle"),
      0x4332A584, 0x43476A7C, receive<9>},
     {0x9E4, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_nSequence"),
      0x433CE396, 0x4347696C, receive<10>},
     {0x9F8, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_flWeight"),
      0x43337A05, 0x43476AFC, receive<11>},
     {0xA0C, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_flPlaybackRate"),
      0x433FBAE8, 0x43476A1C, receive<12>},
     {0xA20, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_flWeightDeltaRate"),
      0x4339768A, 0x4347697C, receive<13>},
     {0xA34, hash("CBaseAnimatingOverlay"), hash("DT_Animationlayer"), hash("m_nOrder"), 0x433D702B,
      0x4347699C, receive<14>}}};
ClientRuntimeStorage& require_owners(ClientRuntimeStorage& storage, PlayerRuntimeOwner& players,
                                     animation::FixRuntimeOwner& fix) {
    if (&players.storage() != &storage || &fix.storage() != &storage ||
        &fix.bones_owner().players() != &players.cache() ||
        &fix.animations().bindings.players != &players.cache())
        throw std::invalid_argument("Receive owners must share canonical storage and player cache");
    if (reinterpret_cast<void*>(&fix.animations().bindings.original_layer_proxy) !=
        reinterpret_cast<void*>(&storage.data().native_cell<engine::RecvVarProxy>(0x43476974u)))
        throw std::invalid_argument(
            "Sequence receiver must borrow the canonical saved original cell");
    if (!storage.data().native_cell<void*>(0x4346A514u) ||
        !storage.data().native_cell<void*>(0x43467B04u))
        throw std::invalid_argument(
            "Receive owners require initialized engine interfaces and gpGlobals");
    return storage;
}
}
std::span<const PlayerReceiveDescriptor, 15> player_receive_descriptors() noexcept {
    return descriptors;
}
PlayerReceiveOwner::PlayerReceiveOwner(ClientRuntimeStorage& storage, PlayerRuntimeOwner& players,
                                       animation::FixRuntimeOwner& fix)
    : storage_(require_owners(storage, players, fix)),
      players_(storage_.data(), storage_.offsets(), players.cache()),
      layers_(storage_.data(), storage_.offsets(), fix.animations()) {}
void bind_player_receive_patches(engine::NativeDataImage& data, PlayerReceiveRuntime& players,
                                 animation::LayerReceiveRuntime& layers) {
    if (&players.data() != &data || &layers.data() != &data ||
        &players.player_cache() != &layers.animations().bindings.players ||
        reinterpret_cast<void*>(&layers.animations().bindings.original_layer_proxy) !=
            reinterpret_cast<void*>(&data.native_cell<engine::RecvVarProxy>(0x43476974u)))
        throw std::invalid_argument(
            "Receive entries require the same data image, player cache and sequence original cell");
    for (const auto& row : descriptors) {
        const auto& node = data.native_cell<EntityUpdatePatch>(row.patch_address);
        if (!node.target || !node.after)
            throw std::invalid_argument(
                "Player receive patches require captured targets and originals");
    }
    active_players = &players;
    active_layers = &layers;
    for (const auto& row : descriptors)
        data.native_cell<EntityUpdatePatch>(row.patch_address).before = std::bit_cast<std::int32_t>(
            std::uint32_t(reinterpret_cast<std::uintptr_t>(row.replacement)));
}
void initialize_player_receive_patch_targets(engine::NativeDataImage& data) {
    auto* client = data.native_cell<void*>(0x434683E0u);
    if (!client)
        throw std::invalid_argument("Player receive patches require canonical VClient018");
    struct Capture {
        EntityUpdatePatch* node;
        engine::RecvProp* property;
        engine::RecvVarProxy original;
    };
    std::array<Capture, 15> captures;
    for (std::size_t i = 0; i < descriptors.size(); ++i) {
        const auto& row = descriptors[i];
        auto& node = data.native_cell<EntityUpdatePatch>(row.patch_address);
        if (node.target || node.after || node.before)
            throw std::logic_error("Player receive patch already initialized");
        auto* property = engine::find_receive_property(client, row.class_hash, row.table_hash,
                                                       row.property_hash);
        if (!property || !property->proxy)
            throw std::runtime_error("Required player receive property or original is missing");
        captures[i] = {&node, property, property->proxy};
    }
    for (const auto& capture : captures) {
        capture.node->target = reinterpret_cast<volatile std::int32_t*>(&capture.property->proxy);
        capture.node->after = std::bit_cast<std::int32_t>(
            std::uint32_t(reinterpret_cast<std::uintptr_t>(capture.original)));
    }
}
void PlayerReceiveOwner::bind_receive_patches() {
    bind_player_receive_patches(storage_.data(), players_, layers_);
}
void PlayerReceiveOwner::initialize_receive_patches() {
    initialize_player_receive_patch_targets(storage_.data());
    bind_receive_patches();
}
} //namespace skeet::client
