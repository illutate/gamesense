#include "skeet/legit/runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/engine/build_legit_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/esp/runtime_owner.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/rage/spread_storage.hpp"
#include "skeet/misc/config.hpp"
#include <bit>
#include <stdexcept>

namespace skeet::legit {
namespace {
using client::ClientRuntimeStorage;
std::ptrdiff_t offset(ClientRuntimeStorage& s, std::uint32_t identity) {
    const auto& g = s.offsets().globals;
    for (const auto& r : engine::build_receive_offset_recipes())
        if (r.pointer_identity == identity)
            return engine::DecodedOffset(g, identity, r.xor_key_identity, r.initial_xor_key, 0);
    for (const auto& r : engine::build_data_map_offset_recipes())
        if (r.pointer_identity == identity)
            return engine::DecodedOffset(g, identity, r.xor_key_identity, r.initial_xor_key, 0);
    throw std::invalid_argument("LEGIT field has no proven receive/datamap recipe");
}
std::ptrdiff_t derived(ClientRuntimeStorage& s, std::uint32_t pointer, std::uint32_t key,
                       std::uint32_t salt) {
    return engine::DecodedOffset(s.offsets().globals, pointer, key, salt, 0);
}
template <class T> T field(const void* object, std::size_t at) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + at, sizeof value);
    return value;
}
RuntimeNetvars runtime_fields(ClientRuntimeStorage& s) {
    return {offset(s, 0x4346A6DCu),
            offset(s, 0x43468B50u),
            derived(s, 0x4346757Cu, 0x4346707Cu, 0x043FA575u),
            offset(s, 0x4346934Cu),
            offset(s, 0x434682B8u),
            offset(s, 0x43467F74u),
            offset(s, 0x4346A80Cu),
            offset(s, 0x43467A50u),
            offset(s, 0x4346A60Cu),
            derived(s, 0x43467430u, 0x4346A718u, 0xB8969256u),
            derived(s, 0x43467434u, 0x43467FA8u, 0x1BC6D160u)};
}
HistoricalBoneOffsets bone_fields(ClientRuntimeStorage& s,
                                  const lagcomp::EntityStateRuntime& entity) {
    return {entity.offsets,         offset(s, 0x43468EA0u),
            offset(s, 0x43468F88u), derived(s, 0x43468D30u, 0x43468ED4u, 0x2A671B42u),
            offset(s, 0x434670E0u), offset(s, 0x43469224u)};
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& engine,
                              client::ConfigurationRuntimeOwner& config,
                              client::HistoryRuntimeOwner& history,
                              engine::SceneQueryRuntimeOwner& scene, animation::RuntimeOwner& bones,
                              client::CommandCaptureRuntimeOwner& capture,
                              const RuntimeServices& services) {
    auto& data = s.data();
    if (&engine.variables.data() != &data || &config.storage() != &s || &history.storage() != &s ||
        &scene.storage() != &s || &scene.players() != &history.players() ||
        &bones.storage() != &s || &bones.players() != &history.players().cache() ||
        &capture.storage() != &s ||
        &capture.globals().engine_client != &data.native_cell<void*>(0x4346A514u))
        throw std::invalid_argument(
            "LEGIT requires the actual shared engine, configuration, history, scene, animation and capture owners");
    if (!services.random.seed || !services.random.uniform || !services.shotgun_pattern.get ||
        !services.rotation.sine || !services.rotation.cosine || !services.rotation.arc_cosine ||
        !services.rotation.arc_tangent2 || !services.exponential || !services.trace.entity_index ||
        !services.trace.through_smoke)
        throw std::invalid_argument(
            "LEGIT requires its genuine RNG, math, entity-index and smoke boundaries");
    for (const auto cell : {0x43467B04u, 0x43467EB8u, 0x4346A334u, 0x4346A338u, 0x43469300u,
                            0x43468D4Cu, 0x43475A60u, 0x43475B90u, 0x43469060u, 0x4346A82Cu})
        if (!data.native_cell<void*>(cell))
            throw std::invalid_argument(
                "LEGIT requires actual clock, bone, filter and smoke publications");
    for (const auto cell : {0x4346794Cu, 0x43468520u, 0x43468D8Cu, 0x43467BB4u, 0x4346A480u})
        if (!engine.variables.at(cell).get())
            throw std::invalid_argument("LEGIT requires its actual acquired ConVars");
    if (data.native_cell<LifecycleOwner*>(0x4347588Cu))
        throw std::logic_error("LEGIT source context must precede its actual feature factory");
    return s;
}
RuntimeServices production_services(ClientRuntimeStorage& s, animation::RuntimeOwner& bones,
                                    client::CrtRuntimeOwner& crt, esp::EspRuntimeOwner& esp) {
    if (&crt.storage() != &s || &esp.storage() != &s)
        throw std::invalid_argument(
            "LEGIT must borrow the same CRT spread and ESP shotgun-pattern owners");
    const auto numeric = lagcomp::native_history_numeric_bindings();
    const auto& trig = bones.trigonometry();
    return {crt.spread().tables(),
            crt.random(),
            esp.shotgun_pattern(),
            {trig.sine, trig.cosine, numeric.arccosine, animation::native_sse_atan2},
            animation::native_sse_exponential,
            engine::BuildLegitBindings{s.data()}.trigger_trace()};
}
}

struct RuntimeOwner::Data final : CommandSource {
    ClientRuntimeStorage& storage;
    const client::EngineRuntimeBindings& engine;
    client::ConfigurationRuntimeOwner& configuration;
    client::HistoryRuntimeOwner& history;
    engine::SceneQueryRuntimeOwner& scene;
    animation::RuntimeOwner& bones;
    client::CommandCaptureRuntimeOwner& capture;
    const CommandFrameInputs* explicit_inputs;
    const RuntimeServices services;
    Settings settings{};
    RuntimeState state{};
    RuntimeSettings runtime_settings;
    RuntimeNetvars offsets;
    HistoricalBoneOffsets historical;
    lagcomp::EntityPreparationLiveGlobals preparation_globals;
    lagcomp::HistoryTickLiveGlobals selection_globals;
    std::array<Matrix3x4, 19> target_bones;
    client::LegitCommandOwner commands;
    OwnerBindings factory;

    Data(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
         client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
         engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& b,
         client::CommandCaptureRuntimeOwner& capture_owner, const CommandFrameInputs* inputs,
         RuntimeServices imported)
        : storage(checked(s, e, c, h, q, b, capture_owner, imported)), engine(e), configuration(c),
          history(h), scene(q), bones(b), capture(capture_owner), explicit_inputs(inputs),
          services(imported), runtime_settings{settings, c.rage_settings().global.enabled,
                                               c.rage_settings().global.activation.value.binding},
          offsets(runtime_fields(s)), historical(bone_fields(s, h.entity_runtime())),
          preparation_globals{s.data().native_cell<void*>(0x43467B04u),
                              s.data().native_cell<float*>(0x43467EB8u),
                              s.data().native_cell<float*>(0x4346A334u),
                              s.data().native_cell<lagcomp::TickTimingSamples>(0x43478C70u)},
          selection_globals{
              s.data().native_cell<float*>(0x43467EB8u), s.data().native_cell<float>(0x43476928u),
              s.data().native_cell<float*>(0x4346A338u), c.settings_menu().maximum_unlag},
          commands{state, runtime_settings, *this},
          factory{{s.owner_registries().at(0x43475B78u), s.memory()},
                  s.command_phases(),
                  s.empty_ticks(),
                  s.configuration(),
                  settings,
                  commands,
                  s.data().native_cell<LifecycleOwner*>(0x4347588Cu)} {}
    void require_state(RuntimeState& current) const {
        if (&current != &state)
            throw std::invalid_argument(
                "LEGIT command endpoint does not name this source owner's canonical state");
    }
    void clear_allocated_storage() override {
        // These nineteen matrices were inside the native zeroed allocation.
        // Keep their clearing at successful allocation, before constructor.
        target_bones = {};
    }
    void capture_view(RuntimeState& current) override {
        require_state(current);
        capture_legit_view(current, storage.data().native_cell<void*>(0x4346A514u));
    }
    void reset(RuntimeState& current) override {
        require_state(current);
        reset_runtime_live(
            current, storage.data().native_cell<float*>(0x4346A338u),
            {services.random.context, services.random.seed, services.random.uniform});
    }
    void pending(RuntimeState& current, client::Command& command, float sample_time) override {
        require_state(current);
        apply_pending_aim(current, command, sample_time,
                          *storage.data().native_cell<float*>(0x4346A338u));
    }
    void process(RuntimeState& current, const RuntimeSettings& selected,
                 client::CommandFrame& frame, void* player, client::Command& command) override {
        require_state(current);
        if (&selected != &runtime_settings || !player)
            throw std::invalid_argument(
                "LEGIT requires its canonical settings and this phase's actual local player");
        auto& data = storage.data();
        auto* globals = data.native_cell<std::byte*>(0x43467B04u);
        auto* interval = data.native_cell<float*>(0x4346A338u);
        const RuntimeTimes times{
            interval,
            data.native_cell<float*>(0x4346A334u),
            data.native_cell<float*>(0x43467EB8u),
            reinterpret_cast<float*>(globals + 0x20),
            reinterpret_cast<float*>(globals + 0x10),
            &data.native_cell<float>(0x43476928u),
            data.native_cell<std::int32_t*>(0x43468D4Cu),
            reinterpret_cast<std::int32_t*>(
                static_cast<std::byte*>(data.native_cell<void*>(0x43475A60u)) + 4),
            data.native_cell<std::int32_t*>(0x43469300u)};
        const rage::TargetBindings targeting{player,
                                             data.native_cell<void*>(0x4346A890u),
                                             history.game_type(),
                                             history.teammates_are_enemies(),
                                             history.players().adjustments(),
                                             history.target_offsets()};
        // Native43344C0C/433EEDFF allocate these locals per invocation. Keep
        // their opaque initial stack bytes; no persistent synthetic seeds.
        CommandFrameInputs workspace;
        if (explicit_inputs)
            std::memcpy(&workspace, explicit_inputs, sizeof workspace);
        auto& record_scratch = workspace.record_interpolation;
        auto& accuracy_scratch = workspace.accuracy_interpolation;
        auto& attachment_matrices = workspace.attachment_matrices;
        auto& saved_matrices = workspace.saved_matrices;
        const lagcomp::HistoryTickTiming timing{*times.world_time,
                                                field<float>(&frame, 0x198),
                                                *times.interpolation,
                                                *interval,
                                                configuration.settings_menu().maximum_unlag,
                                                std::bit_cast<std::int32_t>(frame.field_d4),
                                                std::bit_cast<std::int32_t>(frame.field_18c),
                                                &selection_globals,
                                                &frame.field_d4,
                                                &frame.field_18c};
        const lagcomp::EntityPreparationContext preparation{
            history.entity_runtime(),
            std::bit_cast<std::uint32_t>(*times.world_time),
            field<float>(globals, 0),
            *times.command_time,
            data.native_cell<lagcomp::TickTimingSamples>(0x43478C70u),
            nullptr,
            &preparation_globals};
        const lagcomp::RecordSelectionLiveContext selection{
            history.players().cache(),
            preparation,
            timing,
            services.rotation,
            record_scratch,
            {bones.bones(), attachment_matrices, historical.field_6e9852d6,
             historical.field_2a671b42, historical.field_4e66bb34, historical.field_83a6cf2b},
            data.native_cell<std::uint32_t*>(0x43469060u)};
        const anti_aim::RawTargetTraceEntities entities{
            targeting.entity_list, std::uint32_t(offset(storage, 0x434683C8u)),
            std::uint32_t(offset(storage, 0x43469344u) - 0x18)};
        AccuracyEnvironment accuracy{targeting,
                                     history.entity_runtime(),
                                     selection,
                                     offsets.player_punch_1bae1168,
                                     engine.variables.at(0x4346794Cu),
                                     configuration.rage_settings().global.remove_recoil.value,
                                     configuration.anti_untrusted().enabled.value,
                                     times.world_time,
                                     times.interpolation,
                                     interval,
                                     times.maximum_clients,
                                     saved_matrices,
                                     accuracy_scratch,
                                     scene.queries().ballistics(),
                                     services.spread_tables,
                                     services.random,
                                     services.shotgun_pattern,
                                     engine.variables.at(0x43468520u),
                                     services.rotation,
                                     state.targets,
                                     services.trace,
                                     data.native_cell<void**>(0x43475B90u),
                                     entities};
        const RecoilDecayBindings decay{
            engine.variables.at(0x43468D8Cu), engine.variables.at(0x43467BB4u),
            engine.variables.at(0x4346A480u), *interval, services.exponential};
        RuntimeBindings bindings{
            offsets,
            times,
            accuracy,
            historical,
            target_bones,
            attachment_matrices,
            decay,
            {services.random.context, services.random.seed, services.random.uniform},
            storage.input_runtime().bitmap(),
            capture.view_angles(),
            data.native_cell<std::uint32_t>(0x434759C0u)};
        process_legit_command(current, frame, player, command, selected, bindings);
    }
};

RuntimeOwner::RuntimeOwner(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
                           client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
                           engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& b,
                           client::CommandCaptureRuntimeOwner& capture,
                           client::CrtRuntimeOwner& crt, esp::EspRuntimeOwner& esp)
    : RuntimeOwner(s, e, c, h, q, b, capture, production_services(s, b, crt, esp)) {}
RuntimeOwner::RuntimeOwner(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
                           client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
                           engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& b,
                           client::CommandCaptureRuntimeOwner& capture,
                           client::CrtRuntimeOwner& crt, esp::EspRuntimeOwner& esp,
                           CommandFrameInputs& incoming)
    : RuntimeOwner(s, e, c, h, q, b, capture, incoming, production_services(s, b, crt, esp)) {}
RuntimeOwner::RuntimeOwner(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
                           client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
                           engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& b,
                           client::CommandCaptureRuntimeOwner& capture,
                           CommandFrameInputs& incoming, RuntimeServices services)
    : data_(std::make_unique<Data>(s, e, c, h, q, b, capture, &incoming, services)) {}
RuntimeOwner::RuntimeOwner(ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
                           client::ConfigurationRuntimeOwner& c, client::HistoryRuntimeOwner& h,
                           engine::SceneQueryRuntimeOwner& q, animation::RuntimeOwner& b,
                           client::CommandCaptureRuntimeOwner& capture, RuntimeServices services)
    : data_(std::make_unique<Data>(s, e, c, h, q, b, capture, nullptr, services)) {}
RuntimeOwner::~RuntimeOwner() = default;
ClientRuntimeStorage& RuntimeOwner::storage() const noexcept {
    return data_->storage;
}
Settings& RuntimeOwner::settings() noexcept {
    return data_->settings;
}
RuntimeState& RuntimeOwner::state() noexcept {
    return data_->state;
}
client::LegitCommandOwner& RuntimeOwner::commands() noexcept {
    return data_->commands;
}
OwnerBindings& RuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void RuntimeOwner::bind_initializer(client::NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("LEGIT initializer requires its canonical source data");
    sources.bind_initializer({0x4347FEBCu, data_.get(), [](void* value) {
                                  (void)create_legit_owner(static_cast<Data*>(value)->factory);
                              }});
}
} //namespace skeet::legit
