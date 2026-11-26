#include "skeet/anti_aim/runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/movement_hud_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/target_refresh_runtime_owner.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/client/combat_runtime_factories.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/engine/scene_runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/engine/build_hitbox_clip_bindings.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/lagcomp/native_history_boundaries.hpp"
#include "skeet/rage/settings.hpp"
#include "skeet/esp/other_config.hpp"
#include "skeet/misc/config.hpp"
#include <cstring>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::anti_aim {
namespace {
std::uint32_t field(client::ClientRuntimeStorage& storage, std::uint32_t salt) {
    const auto& globals = storage.offsets().globals;
    for (const auto& row : engine::build_receive_offset_recipes())
        if (row.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                globals, row.pointer_identity, row.xor_key_identity, salt, 0)));
    for (const auto& row : engine::build_data_map_offset_recipes())
        if (row.initial_xor_key == salt)
            return std::uint32_t(std::ptrdiff_t(engine::DecodedOffset(
                globals, row.pointer_identity, row.xor_key_identity, salt, 0)));
    if (salt == 0x043FA575u)
        return std::uint32_t(
            std::ptrdiff_t(engine::DecodedOffset(globals, 0x4346757Cu, 0x4346707Cu, salt, 0)));
    throw std::invalid_argument("Anti-aim field has no recovered canonical offset recipe");
}
CommandOwnerOffsets command_fields(client::ClientRuntimeStorage& storage) {
    const auto f = [&](std::uint32_t salt) { return field(storage, salt); };
    return {f(0xE2FBE695), f(0x36375C23), f(0x3309C0CF), f(0xFB7E3404), f(0x6B1A59B5),
            f(0x7501CB91), f(0x8B5586F6), f(0xA15C3105), f(0x3D10B94C), f(0x7FCB7381),
            f(0x3EB71843), f(0xD720AEA6), f(0x9F8E7F71), f(0xBF567619), f(0xDDA99AB5),
            f(0xB7EA8FDA), f(0xE1DF7E9B), f(0x3B93667B), f(0x38CA5228), f(0x043FA575),
            f(0x334B460C), f(0x86A5F8B3)};
}
template <class T> T read(const void* pointer, std::ptrdiff_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(pointer) + offset, sizeof value);
    return value;
}
engine::OwnerRegistryBinding registry(client::ClientRuntimeStorage& storage,
                                      std::uint32_t identity) {
    return {storage.owner_registries().at(identity), storage.memory()};
}
class RuntimeProximity final : public ProximityEngine {
    void* const& current_;
    const std::uint32_t ticking_;
    ProximityList projection_;

public:
    RuntimeProximity(void* const& current, std::uint32_t ticking)
        : current_(current), ticking_(ticking) {}
    const ProximityList* list() override {
        if (!current_)
            return nullptr;
        projection_ = {read<const ProximityEntity*>(current_), read<std::int32_t>(current_, 0xC)};
        return &projection_;
    }
    bool active(ProximityEntity entity) override {
        return read<std::uint8_t>(reinterpret_cast<const void*>(entity), ticking_) != 0;
    }
    BallisticVector origin(ProximityEntity entity) override {
        return *engine::invoke<const BallisticVector*>(reinterpret_cast<void*>(entity), 10);
    }
};
client::ClientRuntimeStorage&
checked(client::ClientRuntimeStorage& storage, const client::EngineRuntimeBindings& engine,
        client::ConfigurationRuntimeOwner& config, engine::SceneQueryRuntimeOwner& scene,
        animation::RuntimeOwner& animation, client::MovementHudRuntimeOwner& movement,
        client::CommandCaptureRuntimeOwner& capture, client::HistoryRuntimeOwner& history,
        client::TargetRefreshRuntimeOwner& target, client::VisualRuntimeOwner& visual,
        script::SourceScriptRuntimeOwner& scripts, const RuntimeImports& imports) {
    auto& data = storage.data();
    if (&engine.variables.data() != &data || &config.storage() != &storage ||
        &scene.storage() != &storage || &animation.storage() != &storage ||
        &movement.storage() != &storage || &capture.storage() != &storage ||
        &history.storage() != &storage || &target.storage() != &storage ||
        &visual.storage() != &storage || &scripts.storage() != &storage ||
        &scene.players() != &history.players() ||
        &animation.players() != &history.players().cache() ||
        capture.angle_environment().angle_guard != capture.view_angles().environment.angle_guard ||
        capture.angle_environment().angle_guard != &config.anti_untrusted().enabled.value ||
        &capture.budget().game_rules != &data.native_cell<void**>(0x4346A3A4u) ||
        &capture.budget().configured != &data.native_cell<std::int32_t>(0x43469274u) ||
        &scripts.bridge().name_storage != &storage.event_names())
        throw std::invalid_argument(
            "Anti-aim requires the canonical configuration, scene, animation, movement, capture, history and Lua owners");
    const auto core = engine.interfaces.core();
    const auto* globals = data.native_cell<const std::byte*>(0x43467B04u);
    if (!globals || !core.engine_client || !core.entity_list ||
        core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        data.native_cell<void*>(0x4346A338u) != globals + 0x20 ||
        data.native_cell<void*>(0x43467EB8u) != globals + 0x10 ||
        data.native_cell<void*>(0x434678F8u) != globals + 4 ||
        !data.native_cell<void*>(0x43475B2Cu) || !data.native_cell<void*>(0x434757DCu) ||
        !data.native_cell<void*>(0x4346A89Cu) || !data.native_cell<void*>(0x43468304u) ||
        !data.native_cell<void**>(0x4346A3A4u) ||
        animation.bones().entity_list != core.entity_list ||
        history.entity_runtime().entity_list != core.entity_list ||
        history.entity_runtime().offsets.animation_state !=
            std::uint32_t(animation.bones().fields.animation_state_3d52a1a7) ||
        history.entity_runtime().offsets.pose_parameters !=
            std::uint32_t(animation.bones().fields.pose_parameters))
        throw std::invalid_argument(
            "Anti-aim requires its actual published engine objects and clock aliases");
    if (!imports.seed || !imports.integer || !imports.range || !imports.trigonometry.sine ||
        !imports.trigonometry.cosine || !imports.atan2 || !imports.clipper.clip)
        throw std::invalid_argument(
            "Anti-aim requires genuine RNG, CRT and hitbox clipping boundaries");
    return storage;
}
}
RuntimeImports native_runtime_imports(client::ClientRuntimeStorage& storage,
                                      const client::EngineRuntimeBindings& engine) {
    if (&engine.variables.data() != &storage.data())
        throw std::invalid_argument("Anti-aim imports require canonical engine modules");
    const auto module = GetModuleHandleW(L"vstdlib.dll");
    if (!module || module != engine.modules.at(u"vstdlib.dll") ||
        module != storage.data().native_cell<void*>(0x43468A7Cu))
        throw std::invalid_argument("Anti-aim requires its actual already loaded vstdlib module");
    const auto seed = GetProcAddress(module, "RandomSeed"),
               integer = GetProcAddress(module, "RandomInt"),
               range = GetProcAddress(module, "RandomFloat");
    if (!seed || !integer || !range)
        throw std::runtime_error(
            "Anti-aim requires vstdlib RandomSeed, RandomInt and RandomFloat exports");
    engine::initialize_build_hitbox_clip_bindings(storage.data());
    return {reinterpret_cast<RuntimeImports::Seed>(seed),
            reinterpret_cast<RuntimeImports::Integer>(integer),
            reinterpret_cast<RuntimeImports::Range>(range),
            animation::native_sse_trigonometry(),
            animation::native_sse_atan2,
            engine::native_hitbox_clipper(storage.data())};
}
struct RuntimeOwner::Data {
    client::ClientRuntimeStorage& storage;
    RuntimeImports imports;
    InputBitmap input;
    AngleStageState state;
    AngleParameters parameters;
    PrecalculationState precalculation;
    std::array<std::byte, 0x348> animation_state;
    std::array<Matrix3x4, animation::maximum_bones> matrices;
    FinisherState finisher;
    MovementSimulation movement_workspace;
    BodyFreestandingWorkspace body_workspace;
    RuntimeProximity proximity;
    CommandOwnerPitchRandomBindings pitch;
    JitterRandom random;
    BodyFreestandingMath body_math;
    CommandOwnerPhase2Bindings phase2;
    CommandOwnerEligibilityBindings eligibility;
    CommandOwnerAngleBindings angles;
    CommandOwnerPhase4Bindings phase4;
    CommandOwnerPhase6Bindings phase6;
    CommandOwner command;
    OwnerHudBindings hud;
    client::AntiAimRuntimeFactoryBindings factory;
    static void seed(void* raw, std::int32_t value) {
        static_cast<Data*>(raw)->imports.seed(value);
    }
    static std::int32_t integer(void* raw, std::int32_t first, std::int32_t last) {
        return static_cast<Data*>(raw)->imports.integer(first, last);
    }
    static float range(void* raw, float first, float last) {
        return static_cast<Data*>(raw)->imports.range(first, last);
    }
    static void initialize(void* raw) {
        auto& self = *static_cast<Data*>(raw);
        client::create_anti_aim_runtime_owner(self.factory, self.storage.configuration());
    }
    Data(client::ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
         client::ConfigurationRuntimeOwner& c, engine::SceneQueryRuntimeOwner& scene,
         animation::RuntimeOwner& animation, client::MovementHudRuntimeOwner& movement,
         client::CommandCaptureRuntimeOwner& capture, client::HistoryRuntimeOwner& history,
         client::TargetRefreshRuntimeOwner& target, client::VisualRuntimeOwner& visual,
         script::SourceScriptRuntimeOwner& scripts, RuntimeImports i)
        : storage(checked(s, e, c, scene, animation, movement, capture, history, target, visual,
                          scripts, i)),
          imports(i), input(s.input_runtime().bitmap()), precalculation{state, 0, 0, 0},
          finisher{state, animation_state, {}, matrices},
          proximity(s.data().native_cell<void*>(0x434688C0u), field(s, 0x0E5BECE5u)),
          pitch{this, seed, integer}, random{this, seed, range},
          body_math{imports.trigonometry, imports.atan2},
          phase2{scene.queries().movement(), movement_workspace, capture.angle_environment()},
          eligibility{
              s.data().native_cell<void*>(0x4346A89Cu), s.data().native_cell<void*>(0x4346A514u),
              s.data().native_cell<void*>(0x43468994u),
              s.data().native_cell<engine::MemberFunction<void*, const char*>>(0x4346A374u)},
          angles{proximity,      scene.queries().ballistics(),
                 body_math,      imports.clipper,
                 body_workspace, pitch,
                 random},
          phase4{eligibility, target, angles},
          phase6{s.data().native_cell<float>(0x43476B40u),
                 s.data().native_cell<RageVector>(0x4347A4F0u),
                 history.entity_runtime().offsets,
                 s.data().native_cell<std::uint8_t*>(0x43467B24u),
                 s.data().native_cell<void*>(0x434693F0u),
                 s.data().native_cell<lagcomp::SetAbsoluteVector>(0x434688C8u),
                 animation.bones()},
          command{
              c.anti_aim_global(),
              c.anti_aim_settings(),
              parameters,
              state,
              precalculation,
              finisher,
              phase2,
              phase4,
              phase6,
              movement.movement_capture(),
              movement.movement_environment(),
              command_fields(s),
              {s.data().native_cell<std::int32_t*>(0x43475B2Cu),
               s.data().native_cell<std::int32_t*>(0x434757DCu),
               s.data().native_cell<float*>(0x4346A338u),
               s.data().native_cell<float*>(0x43467EB8u),
               s.data().native_cell<void**>(0x4346A3A4u),
               s.data().native_cell<std::int32_t*>(0x434678F8u),
               s.data().native_cell<std::int32_t*>(0x43469300u),
               s.data().native_cell<void*>(0x43467B04u),
               s.data().native_cell<std::int32_t>(0x43469274u),
               s.data().native_cell<std::uint32_t>(0x434759C0u),
               s.data().native_cell<TargetState>(0x4346DB14u),
               s.data().native_cell<std::int32_t>(0x434759C8u),
               s.data().native_cell<std::int32_t>(0x434759CCu),
               s.data().native_cell<std::int32_t>(0x434759F4u),
               s.data().native_cell<std::uint8_t>(0x43468410u),
               s.data().native_cell<RageVector>(0x434759D0u),
               c.rage_settings().global.duck_peek_assist.value.binding,
               input,
               s.data().native_cell<std::uint8_t*>(0x434683D0u),
               s.data().native_cell<std::uint8_t>(0x43468D7Cu),
               s.data().native_cell<std::array<animation::AnimationAngleSample, 2>>(0x434759DCu)},
              s.data().native_cell<void*>(0x4346A514u),
              s.data().native_cell<void*>(0x43468304u),
              s.data().native_cell<void*>(0x4346A890u)},
          hud{scripts.bridge(), c.other_esp_settings().feature_indicators, visual.indicators(),
              field(s, 0x9D920E35u)},
          factory{{registry(s, 0x43475B78u), s.command_phases(), registry(s, 0x43475A9Cu),
                   registry(s, 0x43475AD8u)},
                  {command, hud, s.data().native_cell<void*>(0x43467204u),
                   s.data().native_cell<CommandOwner*>(0x43475940u)}} {}
};
RuntimeOwner::RuntimeOwner(
    client::ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
    client::ConfigurationRuntimeOwner& c, engine::SceneQueryRuntimeOwner& scene,
    animation::RuntimeOwner& animation, client::MovementHudRuntimeOwner& movement,
    client::CommandCaptureRuntimeOwner& capture, client::HistoryRuntimeOwner& history,
    client::TargetRefreshRuntimeOwner& target, client::VisualRuntimeOwner& visual,
    script::SourceScriptRuntimeOwner& scripts)
    : RuntimeOwner(s, e, c, scene, animation, movement, capture, history, target, visual, scripts,
                   native_runtime_imports(s, e)) {}
RuntimeOwner::RuntimeOwner(
    client::ClientRuntimeStorage& s, const client::EngineRuntimeBindings& e,
    client::ConfigurationRuntimeOwner& c, engine::SceneQueryRuntimeOwner& scene,
    animation::RuntimeOwner& animation, client::MovementHudRuntimeOwner& movement,
    client::CommandCaptureRuntimeOwner& capture, client::HistoryRuntimeOwner& history,
    client::TargetRefreshRuntimeOwner& target, client::VisualRuntimeOwner& visual,
    script::SourceScriptRuntimeOwner& scripts, RuntimeImports imports)
    : data_(std::make_unique<Data>(s, e, c, scene, animation, movement, capture, history, target,
                                   visual, scripts, imports)) {}
RuntimeOwner::~RuntimeOwner() = default;
client::ClientRuntimeStorage& RuntimeOwner::storage() const noexcept {
    return data_->storage;
}
CommandOwner& RuntimeOwner::command() noexcept {
    return data_->command;
}
AngleStageState& RuntimeOwner::state() noexcept {
    return data_->state;
}
PrecalculationState& RuntimeOwner::precalculation() noexcept {
    return data_->precalculation;
}
FinisherState& RuntimeOwner::finisher() noexcept {
    return data_->finisher;
}
client::AntiAimRuntimeFactoryBindings& RuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void RuntimeOwner::bind_initializers(client::NativeStartupSources& sources) {
    if (&sources.data() != &storage().data())
        throw std::invalid_argument("Anti-aim factory requires its canonical startup scheduler");
    sources.bind_initializer({client::anti_aim_runtime_factory, data_.get(), Data::initialize});
}
} //namespace skeet::anti_aim
