#include "skeet/client/packet_runtime_owner.hpp"
#include "skeet/client/combat_runtime_factories.hpp"
#include "skeet/client/command_orchestrator.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/movement_hud_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/build_packet_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {
std::ptrdiff_t offset(ClientRuntimeStorage& storage, std::uint32_t identity) {
    const auto& g = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(g, identity, row.xor_key_identity, row.initial_xor_key, 0);
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(g, identity, row.xor_key_identity, row.initial_xor_key, 0);
    throw std::invalid_argument("Packet field has no recovered receive/datamap recipe");
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history,
                              MovementHudRuntimeOwner& movement, TargetRefreshRuntimeOwner& target,
                              PacketRuntimeImports imports) {
    if (&engine.variables.data() != &storage.data() || &config.storage() != &storage ||
        &history.storage() != &storage || &movement.storage() != &storage ||
        &target.storage() != &storage || !imports.seed || !imports.integer)
        throw std::invalid_argument(
            "Packet requires its canonical engine, configuration, history, movement, target and random boundaries");
    auto& data = storage.data();
    for (const auto identity : {0x434757DCu, 0x43475B2Cu, 0x4346A338u, 0x4346A334u, 0x43467B04u,
                                0x4346A514u, 0x4346A890u, 0x43469300u, 0x434678F8u, 0x43467E68u})
        if (!data.native_cell<void*>(identity))
            throw std::invalid_argument("Packet engine alias or class producer is missing");
    for (const auto identity : {0x434679D0u, 0x43468338u, 0x434671CCu, 0x43467A8Cu, 0x4346A42Cu})
        if (!engine.variables.at(identity).get())
            throw std::invalid_argument("Packet ConVar is not acquired");
    return storage;
}
}
PacketRuntimeImports native_packet_runtime_imports(ClientRuntimeStorage& storage,
                                                   const EngineRuntimeBindings& engine) {
    if (&engine.variables.data() != &storage.data())
        throw std::invalid_argument("Packet module inputs must share canonical source storage");
    const auto module = GetModuleHandleW(L"vstdlib.dll");
    if (!module || module != engine.modules.at(u"vstdlib.dll") ||
        module != storage.data().native_cell<void*>(0x43468A7Cu))
        throw std::invalid_argument("Packet requires its actual already loaded vstdlib module");
    const auto seed = GetProcAddress(module, "RandomSeed"),
               integer = GetProcAddress(module, "RandomInt");
    if (!seed || !integer)
        throw std::runtime_error("Packet requires vstdlib RandomSeed and RandomInt exports");
    engine::initialize_build_packet_bindings(storage.data());
    return {reinterpret_cast<PacketRuntimeImports::Seed>(seed),
            reinterpret_cast<PacketRuntimeImports::Integer>(integer)};
}
struct PacketRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    PacketRuntimeImports imports;
    InputBitmap input;
    network::FakeLagState state;
    engine::PacketBoxRuntime box;
    rage::TargetBindings targeting;
    network::PacketWorld world;
    network::PacketCommandOwner command;
    PacketRuntimeFactoryBindings factory;
    static void seed(void* raw, std::int32_t value) {
        static_cast<Data*>(raw)->imports.seed(value);
    }
    static std::int32_t integer(void* raw, std::int32_t first, std::int32_t last) {
        return static_cast<Data*>(raw)->imports.integer(first, last);
    }
    static void initialize(void* raw) {
        auto& self = *static_cast<Data*>(raw);
        create_packet_runtime_owner(self.factory, self.storage.configuration());
    }
    Data(ClientRuntimeStorage& s, const EngineRuntimeBindings& e, ConfigurationRuntimeOwner& c,
         HistoryRuntimeOwner& h, MovementHudRuntimeOwner& m, TargetRefreshRuntimeOwner& target,
         PacketRuntimeImports i)
        : storage(checked(s, e, c, h, m, target, i)), imports(i), input(s.input_runtime().bitmap()),
          state{c.fake_lag_settings().amount.value,
                c.fake_lag_settings().variance.value,
                c.fake_lag_settings().limit.value,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                0,
                {},
                0,
                0,
                0,
                {},
                0},
          box(s.data()), targeting{s.data().native_cell<void*>(0x43475808u),
                                   s.data().native_cell<void*>(0x4346A890u),
                                   h.game_type(),
                                   h.teammates_are_enemies(),
                                   h.players().adjustments(),
                                   h.target_offsets()},
          world{s.data().native_cell<void*>(0x4346A890u),
                s.data().native_cell<void*>(0x43475808u),
                h.target_offsets().rejection_bytes[0],
                offset(s, 0x434670CCu),
                offset(s, 0x434682A4u),
                e.variables.at(0x434679D0u),
                e.variables.at(0x43468338u),
                e.variables.at(0x434671CCu),
                e.variables.at(0x43467A8Cu),
                e.variables.at(0x4346A42Cu),
                s.data().native_cell<const void*>(0x43467E68u),
                network::PacketPlayerGeometry(h.players().cache()),
                box.binding()},
          command{
              state,
              s.data().native_cell<anti_aim::TargetState>(0x4346DB14u),
              c.fake_lag_settings(),
              {s.data().native_cell<std::int32_t*>(0x434757DCu),
               s.data().native_cell<std::int32_t*>(0x43475B2Cu),
               s.data().native_cell<float*>(0x4346A338u), s.data().native_cell<float*>(0x4346A334u),
               s.data().native_cell<void*>(0x43467B04u), s.data().native_cell<void*>(0x4346A514u),
               s.data().native_cell<void*>(0x4346A890u), s.data().native_cell<void**>(0x4346A3A4u),
               s.data().native_cell<std::int32_t*>(0x43469300u),
               s.data().native_cell<std::int32_t*>(0x434678F8u),
               s.data().native_cell<std::int32_t>(0x434759C4u),
               s.data().native_cell<std::uint8_t>(0x43468D7Cu), c.movement().air_duck.value,
               s.data().native_cell<std::int32_t>(0x43469274u), input,
               s.data().native_cell<std::uint8_t*>(0x434683D0u),
               s.data().native_cell<bool>(0x4347CA30u), s.data().native_cell<void**>(0x43475B90u)},
              {offset(s, 0x434670CCu), offset(s, 0x434688D0u), offset(s, 0x434679F8u),
               offset(s, 0x4346A8E4u), offset(s, 0x43468BD4u), offset(s, 0x434678A4u),
               offset(s, 0x43469374u), offset(s, 0x43467910u), offset(s, 0x43468378u),
               offset(s, 0x4346836Cu), offset(s, 0x43468BF4u)},
              {world,
               target,
               targeting,
               m.movement_capture(),
               m.movement_environment(),
               {s.data().native_cell<void*>(0x4346A890u), offset(s, 0x4346A60Cu),
                offset(s, 0x4346749Cu), offset(s, 0x434670A4u)},
               {this, seed, integer},
               s.data().native_cell<std::array<std::uint32_t, 3>>(0x43474580u)}},
          factory{{s.owner_registries().at(0x43475B78u), s.memory()},
                  s.command_phases(),
                  c.fake_lag_settings(),
                  command} {}
};
PacketRuntimeOwner::PacketRuntimeOwner(ClientRuntimeStorage& s, const EngineRuntimeBindings& e,
                                       ConfigurationRuntimeOwner& c, HistoryRuntimeOwner& h,
                                       MovementHudRuntimeOwner& m, TargetRefreshRuntimeOwner& t)
    : PacketRuntimeOwner(s, e, c, h, m, t, native_packet_runtime_imports(s, e)) {}
PacketRuntimeOwner::PacketRuntimeOwner(ClientRuntimeStorage& s, const EngineRuntimeBindings& e,
                                       ConfigurationRuntimeOwner& c, HistoryRuntimeOwner& h,
                                       MovementHudRuntimeOwner& m, TargetRefreshRuntimeOwner& t,
                                       PacketRuntimeImports i)
    : data_(std::make_unique<Data>(s, e, c, h, m, t, i)) {}
PacketRuntimeOwner::~PacketRuntimeOwner() = default;
ClientRuntimeStorage& PacketRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
network::PacketCommandOwner& PacketRuntimeOwner::command() noexcept {
    return data_->command;
}
network::FakeLagState& PacketRuntimeOwner::state() noexcept {
    return data_->state;
}
PacketRuntimeFactoryBindings& PacketRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void PacketRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("Packet factory requires the canonical startup dispatcher");
    sources.bind_initializer({packet_runtime_factory, data_.get(), Data::initialize});
}
} //namespace skeet::client
