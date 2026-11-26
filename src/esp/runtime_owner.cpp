#include "skeet/esp/runtime_owner.hpp"
#include "skeet/esp/owner_runtime.hpp"
#include "skeet/esp/runtime_bindings.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/effects/glow_engine.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/rage/spread_storage.hpp"
#include "skeet/render/renderer_assets.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::esp {
namespace {
template <class T> T& pointed(engine::NativeDataImage& data, std::uint32_t identity) {
    auto* pointer = data.native_cell<T*>(identity);
    if (!pointer)
        throw std::invalid_argument("ESP requires the actual initialized engine alias");
    return *pointer;
}
std::ptrdiff_t offset(client::ClientRuntimeStorage& storage, std::uint32_t identity) {
    const auto& globals = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(globals, identity, row.xor_key_identity,
                                         row.initial_xor_key, 0);
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.pointer_identity == identity)
            return engine::DecodedOffset(globals, identity, row.xor_key_identity,
                                         row.initial_xor_key, 0);
    throw std::invalid_argument("ESP field has no proven receive/datamap offset recipe");
}
engine::OwnerRegistryBinding registry(client::ClientRuntimeStorage& storage,
                                      std::uint32_t identity) {
    return {storage.owner_registries().at(identity), storage.memory()};
}
client::ClientRuntimeStorage&
checked(client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
        client::ConfigurationRuntimeOwner& configuration, client::PlayerRuntimeOwner& players,
        engine::SceneQueryRuntimeOwner& scene, client::VisualRuntimeOwner& visual,
        client::CrtRuntimeOwner& crt, script::SourceScriptRuntimeOwner& scripts,
        ui::RendererAssets& assets, engine::BuildEspBindings native) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &native.data != &data ||
        &configuration.storage() != &storage || &players.storage() != &storage ||
        &scene.storage() != &storage || &scene.players() != &players ||
        &visual.storage() != &storage || &crt.storage() != &storage ||
        &scripts.storage() != &storage || &assets.storage() != &storage ||
        &assets.startup_renderer() != &visual.startup_renderer())
        throw std::invalid_argument(
            "ESP must share the canonical engine, player, scene, visual, CRT and script owners");
    if (data.native_cell<EspOwnerRuntime*>(0x434758DC))
        throw std::logic_error(
            "ESP bindings must be composed before the canonical feature factory");
    for (const auto cell :
         {0x4346A31Cu, 0x43468B9Cu, 0x43467198u, 0x43467470u, 0x4346A898u, 0x43467B54u, 0x434758B4u,
          0x43475B90u, 0x43468D4Cu, 0x434678F8u, 0x43475A60u})
        if (!data.native_cell<void*>(cell))
            throw std::invalid_argument(
                "ESP requires its genuine interface, icon, clock and packet producers");
    for (const auto cell : {0x43467C3Cu, 0x43467A8Cu, 0x4346A568u, 0x4346A850u, 0x43468520u})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument("ESP requires the actual acquired encoded ConVar");
    return storage;
}
std::int32_t dropped_capacity(void*, const WorldEntity& entity) {
    return engine::invoke<std::int32_t>(reinterpret_cast<void*>(entity.identity), 375);
}
PacketTracerOffsets packet_fields(client::ClientRuntimeStorage& storage) {
    return {{offset(storage, 0x43467EC0), offset(storage, 0x43468910)},
            offset(storage, 0x4346A45C),
            offset(storage, 0x434693FC),
            offset(storage, 0x43467034),
            offset(storage, 0x43468B28),
            offset(storage, 0x43467480),
            offset(storage, 0x43468E90),
            offset(storage, 0x43468FAC)};
}
TabletUpgradeOffsets tablet_fields(client::ClientRuntimeStorage& storage) {
    return {offset(storage, 0x43467A74), offset(storage, 0x4346908C), offset(storage, 0x43467E74),
            offset(storage, 0x43467000), offset(storage, 0x434693B0), offset(storage, 0x4346A770)};
}
}
struct EspRuntimeOwner::Data {
    client::ClientRuntimeStorage& storage;
    EspFrameOwner state;
    PlayerEngineBindings players;
    WeaponTextBindings words;
    WeaponIconProvider icons;
    WeaponTextProvider names;
    PlayerDrawContext draw;
    SoundOverlayBindings sound;
    effects::GlowEngine glow_engine;
    effects::GlowEntityEngine glow_entities;
    effects::GlowRuntime glow;
    TabletUpgradeBindings tablet;
    LineOfSightBindings sight;
    anti_aim::RawTargetTraceEntities trace_entities;
    rage::ShotgunPattern pattern;
    PacketTracerBindings packet;
    events::DeathBindings death;
    EspOwnerBindings bindings;

    Data(client::ClientRuntimeStorage& source, const client::EngineRuntimeBindings& engine,
         client::ConfigurationRuntimeOwner& configuration, client::PlayerRuntimeOwner& player_state,
         engine::SceneQueryRuntimeOwner& scene, client::VisualRuntimeOwner& visual,
         client::CrtRuntimeOwner& crt, script::SourceScriptRuntimeOwner& scripts,
         ui::RendererAssets& assets, engine::BuildEspBindings native)
        : storage(checked(source, engine, configuration, player_state, scene, visual, crt, scripts,
                          assets, native)),
          state(player_state.cache(), source.data().native_cell<std::uint8_t>(0x4346A65C)),
          players(native_player_engine_bindings(source, engine)),
          words{source.data().native_cell<void*>(0x43476424),
                source.data().native_cell<WeaponTextBindings::ItemSystemFactory>(0x43468B9C),
                source.data().native_cell<void*>(0x4346A31C)},
          icons(assets.icon_cache().weapon_provider()), names(weapon_text_provider(words)),
          draw{visual.projection(),
               player_state.cache(),
               source.data().native_cell<float>(0x4346D548),
               configuration.player_esp_settings().money.value,
               crt.esp_flags(),
               icons,
               names,
               &scripts.bridge(),
               &scripts.flags()},
          sound(native_sound_overlay_bindings(source, engine, scene)),
          glow_engine(native.glow_engine()),
          glow_entities({source.data().native_cell<void*>(0x4346A890),
                         source.data().native_cell<void*>(0x43475808),
                         source.data().native_cell<std::int32_t*>(0x43469300),
                         source.data().native_cell<void*>(0x434688C0),
                         source.data().native_cell<std::uint8_t>(0x43468E58),
                         source.data().native_cell<std::uint8_t>(0x4346A65C),
                         engine.variables.at(0x43467A8C), engine.variables.at(0x4346A568),
                         player_state.adjustments(), source.offsets()}),
          glow(configuration.player_esp_settings().player, configuration.other_esp_settings().world,
               glow_engine, glow_entities),
          tablet{configuration.other_esp_settings().world,
                 source.data().native_cell<void*>(0x43475808),
                 source.data().native_cell<void*>(0x4346A890),
                 source.data().native_cell<std::int32_t*>(0x434678F8),
                 source.data().native_cell<float*>(0x43467EB8),
                 tablet_fields(source)},
          sight{source.data().native_cell<void*>(0x4346A514),
                source.data().native_cell<void*>(0x43468260),
                engine.variables.at(0x4346A850),
                source.data().native_cell<std::int32_t*>(0x43469300),
                source.data().native_cell<std::uint8_t*>(0x434683D0),
                scene.queries().movement(),
                crt.trigonometry(),
                offset(source, 0x434679F8),
                offset(source, 0x43468EA0)},
          trace_entities(source.data().native_cell<void*>(0x4346A890),
                         static_cast<std::uint32_t>(offset(source, 0x434683C8)),
                         static_cast<std::uint32_t>(offset(source, 0x43469344) - 0x18)),
          pattern(native.shotgun_pattern()),
          packet{packet_fields(source),
                 crt.spread().tables(),
                 crt.random(),
                 pattern,
                 crt.trigonometry(),
                 engine.variables.at(0x43468520),
                 source.data().native_cell<void*>(0x43467B54),
                 scene.queries().ballistics(),
                 source.data().native_cell<void* const*>(0x43475B90),
                 trace_entities,
                 source.data().native_cell<const float*>(0x43467EB8)},
          death{source.data().native_cell<void*>(0x4346A514),
                source.data().native_cell<void*>(0x4346A890),
                source.data().native_cell<void*>(0x43475808)},
          bindings{{registry(source, 0x43475B78), registry(source, 0x43475A9C),
                    source.entity_observers(), registry(source, 0x43475988),
                    registry(source, 0x4347584C), registry(source, 0x43475AD8),
                    registry(source, 0x43475770), source.event_registry(), source.configuration()},
                   state,
                   configuration.player_esp_settings().player,
                   configuration.other_esp_settings().world,
                   configuration.effects_settings(),
                   {configuration.player_esp_settings().player,
                    configuration.other_esp_settings().world,
                    configuration.other_esp_settings().grenades,
                    draw,
                    {icons, names, nullptr, dropped_capacity},
                    &glow_engine,
                    visual.indicators(),
                    source.input_runtime().bitmap(),
                    player_state.adjustments()},
                   players,
                   {scene.queries(), source.data().native_cell<void**>(0x4346A3A4),
                    source.data().native_cell<void*>(0x434688C0), engine.variables.at(0x43467C3C),
                    engine.variables.at(0x43467A8C),
                    pointed<std::int32_t>(source.data(), 0x43469300),
                    pointed<float>(source.data(), 0x43467EB8),
                    source.data().native_cell<std::int32_t>(0x43467DA8),
                    source.data().native_cell<std::int32_t*>(0x43475A60)[1],
                    pointed<float>(source.data(), 0x4346A338),
                    source.data().native_cell<Vector3>(0x43479CCC),
                    source.data().native_cell<std::uint8_t>(0x43468E58),
                    WorldEngineOffsets{source.offsets().globals}},
                   {configuration.player_esp_settings().player, sound, player_state.adjustments(),
                    state.frame},
                   tablet,
                   glow,
                   {configuration.player_esp_settings().player, player_state.adjustments(),
                    state.frame, source.data().native_cell<std::uint8_t>(0x43468E58), sight},
                   packet,
                   death,
                   source.data().native_cell<const void*>(0x43467B04),
                   source.data().native_cell<const float*>(0x4346A338),
                   source.data().native_cell<std::int32_t*>(0x43468D4C),
                   source.data().native_cell<void*>(0x43467198),
                   source.data().native_cell<void*>(0x43467470),
                   source.data().native_cell<void*>(0x4346A898)} {}
};
EspRuntimeOwner::EspRuntimeOwner(client::ClientRuntimeStorage& storage,
                                 const client::EngineRuntimeBindings& engine,
                                 client::ConfigurationRuntimeOwner& configuration,
                                 client::PlayerRuntimeOwner& players,
                                 engine::SceneQueryRuntimeOwner& scene,
                                 client::VisualRuntimeOwner& visual, client::CrtRuntimeOwner& crt,
                                 script::SourceScriptRuntimeOwner& scripts,
                                 ui::RendererAssets& assets, engine::BuildEspBindings native)
    : data_(std::make_unique<Data>(storage, engine, configuration, players, scene, visual, crt,
                                   scripts, assets, native)) {}
EspRuntimeOwner::~EspRuntimeOwner() = default;
client::ClientRuntimeStorage& EspRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
EspOwnerBindings& EspRuntimeOwner::factory_bindings() noexcept {
    return data_->bindings;
}
EspOwnerRuntime*& EspRuntimeOwner::active() const noexcept {
    return data_->storage.data().native_cell<EspOwnerRuntime*>(0x434758DC);
}
EspFrameOwner& EspRuntimeOwner::state() noexcept {
    return data_->state;
}
effects::GlowRuntime& EspRuntimeOwner::glow() noexcept {
    return data_->glow;
}
const rage::ShotgunPattern& EspRuntimeOwner::shotgun_pattern() const noexcept {
    return data_->pattern;
}
} // namespace skeet::esp
