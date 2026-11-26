#include "skeet/client/entity_command_runtime_owner.hpp"
#include "skeet/client/entity_command_lifecycle.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/local_animation_runtime_owner.hpp"
#include "skeet/client/local_player_animation_hooks.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/build_prediction_scratch_bindings.hpp"
#include "entity_command_scratch.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/legit/runtime.hpp"
#include "skeet/rage/settings.hpp"
#include <array>
#include <functional>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {
std::ptrdiff_t field(ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& globals = storage.offsets().globals;
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                         salt, 0);
    for (const auto& recipe : engine::build_data_map_offset_recipes())
        if (recipe.initial_xor_key == salt)
            return engine::DecodedOffset(globals, recipe.pointer_identity, recipe.xor_key_identity,
                                         salt, 0);
    struct Derived {
        std::uint32_t pointer, key, salt;
    };
    constexpr std::array derived{Derived{0x43468368u, 0x43467FACu, 0x3B1F6ECCu},
                                 Derived{0x4346757Cu, 0x4346707Cu, 0x043FA575u},
                                 Derived{0x43467EBCu, 0x43467038u, 0x432B1F5Bu},
                                 Derived{0x43467A88u, 0x43467160u, 0x0D560FACu},
                                 Derived{0x4346828Cu, 0x43468D2Cu, 0x9E2078A1u}};
    for (const auto& recipe : derived)
        if (recipe.salt == salt)
            return engine::DecodedOffset(globals, recipe.pointer, recipe.key, salt, 0);
    throw std::invalid_argument("Entity command field has no recovered receive/datamap recipe");
}
EntityCommandOffsets offsets(ClientRuntimeStorage& s) {
    const auto f = [&](std::uint32_t salt) { return field(s, salt); };
    return {
        f(0x5DF1A4BC), f(0x3B1F6ECC), f(0x3D10B94C), f(0x5AE32976), f(0x1BAE1168), f(0x48442721),
        f(0xC246315E), f(0x2F33C681), f(0xB5A66556), f(0xFB7E3404), f(0x0317EEDE), f(0xCD812654),
        f(0x5C9F0FBB), f(0x43D0EFC4), f(0x9D920E35), f(0xCDE290D2), f(0x8B5586F6), f(0xA9EE6062),
        f(0x7501CB91), f(0x8D1BD8D0), f(0xE2FBE695), f(0x8DBC9874), f(0x68140CD8), f(0xD5FD2E2C),
        f(0x38CA5228), f(0x043FA575), f(0x334B460C), f(0x6610746E), f(0x66146802), f(0x00A5BD68),
        f(0x6B1A59B5), f(0x50F9CCC8), f(0x8CFC633A), f(0x7F5ED2D0), f(0xBCB263A5), f(0x9E2078A1),
        f(0xCA5EC698), f(0x0176A381), f(0x6777C9CE), f(0xD2563274), f(0x15335AAC), f(0x4013FC03),
        f(0xE865A877), f(0x3B93667B), f(0xC6C78C82), f(0x6E9852D6), f(0x432B1F5B), f(0x0D560FAC),
        f(0x71DCEE42), f(0x2F4E97C4), f(0x3B704B41), f(0x695FF325), f(0x47A6F50C), f(0x01AA449D),
        f(0xA116369A), f(0xA25D8271)};
}
engine::OwnerRegistryBinding registry(ClientRuntimeStorage& s, std::uint32_t cell) {
    return {s.owner_registries().at(cell), s.memory()};
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& s, const EngineRuntimeBindings& engine,
                              ConfigurationRuntimeOwner& configuration,
                              HistoryRuntimeOwner& history, LocalAnimationRuntimeOwner& local,
                              CommandCaptureRuntimeOwner& capture,
                              EntityCommandRuntimeImports imports) {
    auto& data = s.data();
    if (&engine.variables.data() != &data || &configuration.storage() != &s ||
        &history.storage() != &s || &local.storage() != &s || &local.history() != &history ||
        &capture.storage() != &s ||
        &local.local_hooks().bindings.view_angles !=
            &data.native_cell<std::array<float, 3>>(0x4347BD30u))
        throw std::invalid_argument(
            "Entity command contexts require canonical history/local animation/capture/settings owners");
    if (!imports.ceiling || !imports.exponential || !s.offsets().words ||
        s.offsets().globals.pointers.empty() || !data.native_cell<void*>(0x4346A894u) ||
        !data.native_cell<void*>(0x43467B04u) || !data.native_cell<float*>(0x43467EB8u) ||
        !data.native_cell<float*>(0x4346A338u) || !data.native_cell<float*>(0x4346A334u) ||
        !data.native_cell<std::int32_t*>(0x43475B2Cu) ||
        !data.native_cell<std::int32_t*>(0x434757DCu))
        throw std::invalid_argument(
            "Entity command contexts require acquired offsets, clocks, input and CRT imports");
    for (const auto cell : {0x4346A514u, 0x4346A890u, 0x43469264u, 0x4346A614u}) {
        bool found = false;
        const auto manifest = engine::build_interface_manifest();
        for (std::size_t i = 0; i < manifest.size(); ++i)
            if (manifest[i].destination_cell == cell) {
                if (!data.native_cell<void*>(cell) ||
                    data.native_cell<void*>(cell) != engine.interfaces.objects[i])
                    throw std::invalid_argument(
                        "Entity command interface differs from its acquired engine object");
                found = true;
                break;
            }
        if (!found)
            throw std::logic_error(
                "Entity command interface is absent from its acquisition manifest");
    }
    return s;
}
}
EntityCommandRuntimeImports native_entity_command_runtime_imports() {
    const auto crt = GetModuleHandleW(L"msvcrt.dll");
    auto* ceiling = crt ? GetProcAddress(crt, "ceil") : nullptr;
    if (!ceiling)
        throw std::runtime_error("Entity command model categories require loaded msvcrt!ceil");
    return {reinterpret_cast<double (*)(double)>(ceiling), animation::native_sse_exponential};
}
struct EntityCommandRuntimeOwner::Data {
    ClientRuntimeStorage& storage;
    legit::RecoilDecayBindings recoil;
    EntityCommandState state{};
    EntityCommandBindings bindings;
    EntityCommandOwner command;
    EntityCommandFactoryBindings factory;
    Data(ClientRuntimeStorage& source, const EngineRuntimeBindings& engine,
         ConfigurationRuntimeOwner& config, HistoryRuntimeOwner& history,
         LocalAnimationRuntimeOwner& local, CommandCaptureRuntimeOwner& capture,
         EntityCommandRuntimeImports imports)
        : storage(checked(source, engine, config, history, local, capture, imports)),
          recoil{engine.variables.at(0x43468D8Cu), engine.variables.at(0x43467BB4u),
                 engine.variables.at(0x4346A480u), *source.data().native_cell<float*>(0x4346A338u),
                 imports.exponential},
          bindings{nullptr,
                   nullptr,
                   source.data().native_cell<void*>(0x4346A890u),
                   source.data().native_cell<void*>(0x4346A894u),
                   source.data().native_cell<void*>(0x4346A614u),
                   engine.variables.at(0x43468CACu),
                   engine.variables.at(0x4346A850u),
                   engine.variables.at(0x4346794Cu),
                   nullptr,
                   &source.data().native_cell<std::int32_t>(0x4347BDACu),
                   &source.data().native_cell<std::int32_t>(0x4347BD78u),
                   &source.data().native_cell<std::uint8_t>(0x434758B0u),
                   &source.data().native_cell<std::uint8_t>(0x43475BD2u),
                   std::ref(source.data().native_cell<void (*)()>(0x43468BF0u)),
                   source.data().native_cell<const std::int32_t*>(0x434758D8u),
                   source.data().native_cell<const std::int32_t*>(0x43475794u),
                   source.data().native_cell<const std::int32_t*>(0x43475B2Cu),
                   source.data().native_cell<const std::int32_t*>(0x434757DCu),
                   &source.data().native_cell<std::int32_t>(0x43468290u),
                   source.data().native_cell<void*>(0x43467B04u),
                   source.data().native_cell<float*>(0x4346A334u),
                   source.data().native_cell<float*>(0x4346A338u),
                   source.data().native_cell<float*>(0x43467EB8u),
                   &recoil,
                   &local.local_hooks().bindings.view_angles,
                   source.data().native_cell<RestorePredictionFrame>(0x4346D660u),
                   source.data().native_cell<engine::MemberFunction<std::int32_t, std::int32_t>>(
                       0x4346A364u),
                   &source.data().native_cell<std::int32_t>(0x4347C9ECu),
                   &source.data().native_cell<std::int32_t>(0x43467114u),
                   &source.data().native_cell<std::uint32_t>(0x434759C0u),
                   &source.data().native_cell<void*>(0x43467478u),
                   &source.prediction_observers(),
                   source.data().native_cell<PredictionHistoryOwner*>(0x434759B4u),
                   &history.players().cache(),
                   {source.data().native_cell<std::uint8_t>(0x43468D7Cu),
                    source.data().native_cell<const std::int32_t*>(0x43475B2Cu),
                    source.data().native_cell<std::array<animation::AnimationAngleSample, 2>>(
                        0x434759DCu)},
                   &source.data().native_cell<EntityAngleConfirmation>(0x43479794u),
                   source.data().native_cell<std::array<std::uint32_t, 3>>(0x43474580u),
                   offsets(source)},
          command{state, bindings},
          factory{{registry(source, 0x43475B78u), source.command_phases(),
                   source.prediction_observers(), source.data_update_observers(),
                   source.entity_observers(), registry(source, 0x43475770u),
                   source.recharge_observers()},
                  command,
                  bindings,
                  source.data_owner(),
                  source.data().native_cell<void*>(0x4346A514u),
                  source.data().native_cell<void*>(0x43469264u),
                  config.rage_settings().global.enabled.value,
                  imports.ceiling} {
        engine::initialize_build_prediction_scratch_bindings(storage.data());
    }
};
EntityCommandRuntimeOwner::EntityCommandRuntimeOwner(
    ClientRuntimeStorage& s, const EngineRuntimeBindings& e, ConfigurationRuntimeOwner& c,
    HistoryRuntimeOwner& h, LocalAnimationRuntimeOwner& l, CommandCaptureRuntimeOwner& capture)
    : EntityCommandRuntimeOwner(s, e, c, h, l, capture, native_entity_command_runtime_imports()) {}
EntityCommandRuntimeOwner::EntityCommandRuntimeOwner(
    ClientRuntimeStorage& s, const EngineRuntimeBindings& e, ConfigurationRuntimeOwner& c,
    HistoryRuntimeOwner& h, LocalAnimationRuntimeOwner& l, CommandCaptureRuntimeOwner& capture,
    EntityCommandRuntimeImports imports)
    : data_(std::make_unique<Data>(s, e, c, h, l, capture, imports)) {}
EntityCommandRuntimeOwner::~EntityCommandRuntimeOwner() = default;
ClientRuntimeStorage& EntityCommandRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
EntityCommandState& EntityCommandRuntimeOwner::state() noexcept {
    return data_->state;
}
EntityCommandOwner& EntityCommandRuntimeOwner::command() noexcept {
    return data_->command;
}
EntityCommandBindings& EntityCommandRuntimeOwner::bindings() noexcept {
    return data_->bindings;
}
EntityCommandFactoryBindings& EntityCommandRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void EntityCommandRuntimeOwner::initialize_prediction_scratch(bool local_creation) {
    auto& storage = data_->storage;
    entity_command_detail::initialize_prediction_scratch(
        storage.data().native_cell<EntityCommandOwner*>(0x43475898u), storage.data(),
        storage.offsets().globals, local_creation);
}
void EntityCommandRuntimeOwner::bind_initializers(NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("Entity command startup requires canonical source data");
    bind_native_entity_command_factory(sources, data_->factory);
}
} //namespace skeet::client
