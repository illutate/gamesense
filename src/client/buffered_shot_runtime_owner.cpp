#include "skeet/client/buffered_shot_runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/history_runtime_owner.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/client/command_capture_runtime_owner.hpp"
#include "skeet/client/command_checksum.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/netvar_initialization.hpp"
#include "skeet/esp/runtime_owner.hpp"
#include "skeet/rage/runtime_owner.hpp"
#include "skeet/rage/feedback_runtime_owner.hpp"
#include "skeet/rage/buffered_shot_repair.hpp"
#include "skeet/rage/spread_storage.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::client {
namespace {
std::ptrdiff_t field(ClientRuntimeStorage& s, std::uint32_t hash) {
    for (const auto& recipe : engine::build_receive_offset_recipes())
        if (recipe.initial_xor_key == hash)
            return engine::DecodedOffset(s.offsets().globals, recipe.pointer_identity,
                                         recipe.xor_key_identity, recipe.initial_xor_key, 0);
    throw std::invalid_argument("Buffered-shot field has no recovered receive recipe");
}
ClientRuntimeStorage& checked(ClientRuntimeStorage& s, const EngineRuntimeBindings& e,
                              ConfigurationRuntimeOwner& c, HistoryRuntimeOwner& h,
                              CommandCaptureRuntimeOwner& capture, animation::RuntimeOwner& b,
                              CrtRuntimeOwner& crt, esp::EspRuntimeOwner& esp,
                              rage::RuntimeOwner& rage, rage::FeedbackRuntimeOwner& feedback) {
    if (&e.variables.data() != &s.data() || &c.storage() != &s || &h.storage() != &s ||
        &capture.storage() != &s || &b.storage() != &s || &b.players() != &h.players().cache() ||
        &crt.storage() != &s || &esp.storage() != &s || &rage.storage() != &s ||
        &feedback.storage() != &s || &feedback.runtime().players != &h.players().cache())
        throw std::invalid_argument(
            "Buffered-shot runtime requires its actual shared engine, history, capture, animation, CRT, ESP, RAGE and feedback owners");
    if (!s.data().native_cell<void*>(0x4346A894u) || !s.data().native_cell<void*>(0x43475A60u) ||
        !s.data().native_cell<void*>(0x434757DCu) || !s.data().native_cell<void*>(0x43475B2Cu))
        throw std::invalid_argument(
            "Buffered-shot runtime requires genuine input and command-clock publications");
    if (s.data().native_cell<BufferedShotLifecycleOwner*>(0x43475968u))
        throw std::logic_error("Buffered-shot context must precede its actual feature factory");
    initialize_source_command_checksum(s.data());
    return s;
}
}
struct BufferedShotRuntimeOwner::Data final : BufferedShotCommandSource {
    ClientRuntimeStorage& storage;
    const EngineRuntimeBindings& engine;
    ConfigurationRuntimeOwner& configuration;
    HistoryRuntimeOwner& history;
    CommandCaptureRuntimeOwner& capture;
    animation::RuntimeOwner& animation;
    CrtRuntimeOwner& crt;
    esp::EspRuntimeOwner& esp;
    rage::RuntimeOwner& rage;
    rage::FeedbackRuntimeOwner& feedback;
    BufferedShotCommandStorage state_;
    BufferedShotCommandOwner command_;
    BufferedShotFactoryBindings factory;
    Data(ClientRuntimeStorage& s, const EngineRuntimeBindings& e, ConfigurationRuntimeOwner& c,
         HistoryRuntimeOwner& h, CommandCaptureRuntimeOwner& cap, animation::RuntimeOwner& b,
         CrtRuntimeOwner& process, esp::EspRuntimeOwner& esp_owner, rage::RuntimeOwner& rage_owner,
         rage::FeedbackRuntimeOwner& feedback_owner)
        : storage(checked(s, e, c, h, cap, b, process, esp_owner, rage_owner, feedback_owner)),
          engine(e), configuration(c), history(h), capture(cap), animation(b), crt(process),
          esp(esp_owner), rage(rage_owner), feedback(feedback_owner), command_(state_, *this),
          factory(s.owner_registries(),
                  {{s.owner_registries().at(0x43475B78u), s.memory()},
                   s.command_phases(),
                   command_,
                   s.data().native_cell<BufferedShotLifecycleOwner*>(0x43475968u)}) {}
    void prepare(BufferedShotCommandStorage& state) noexcept override {
        if (*storage.data().native_cell<std::int32_t*>(0x434757DCu) == 0)
            state.diagnostics.count = 0;
    }
    void repair(BufferedShotCommandStorage& state, CommandFrame& frame, void* player,
                std::uint8_t send) override {
        if (&state != &state_)
            throw std::invalid_argument(
                "Buffered-shot source endpoint must use its canonical frame/diagnostic state");
        auto& data = storage.data();
        const BufferedShotCommandTiming timing{data.native_cell<void*>(0x43475A60u),
                                               data.native_cell<std::int32_t*>(0x43468F9Cu),
                                               data.native_cell<std::int32_t*>(0x434670A8u)};
        const CommandButtonPolicy policy{
            data.native_cell<std::array<std::uint32_t, 150>>(0x43476090u),
            configuration.anti_aim_other().leg_movement.value,
            *data.native_cell<std::int32_t*>(0x434757DCu)};
        const CommandTrigonometry trig{animation.trigonometry().sine,
                                       animation.trigonometry().cosine};
        const rage::BufferedShotRepairBindings reconstruction{rage};
        auto* input = static_cast<std::byte*>(data.native_cell<void*>(0x4346A894u));
        const BufferedShotCommandBindings bindings{
            configuration.rage_settings().global.enabled.value,
            configuration.rage_settings().global.silent_aim.value,
            configuration.settings_menu().hold_aim,
            *data.native_cell<std::int32_t*>(0x434757DCu),
            *data.native_cell<std::int32_t*>(0x43475B2Cu),
            configuration.settings_menu().maximum_unlag,
            *data.native_cell<float*>(0x4346A334u),
            *data.native_cell<float*>(0x4346A338u),
            data.native_cell<float>(0x43469254u),
            *data.native_cell<float*>(0x43467EB8u),
            data.native_cell<CommandChecksum>(0x43467C48u),
            *reinterpret_cast<Command**>(input + 0xF0),
            *reinterpret_cast<VerifiedCommand**>(input + 0xF4),
            data.native_cell<EntityCommandOwner*>(0x43475898u),
            data.native_cell<void*>(0x4346A514u),
            data.native_cell<void*>(0x4346A890u),
            history.players().cache(),
            {field(storage, 0xD5FD2E2Cu), field(storage, 0x7F5ED2D0u), field(storage, 0x9D920E35u),
             field(storage, 0xE2FBE695u), field(storage, 0x3309C0CFu)},
            {engine.variables.at(0x4346747Cu), engine.variables.at(0x4346A748u),
             engine.variables.at(0x4346A330u), engine.variables.at(0x4346794Cu),
             engine.variables.at(0x43468528u), engine.variables.at(0x43468520u)},
            capture.angle_environment(),
            data.native_cell<network::LatencySamples>(0x43478C70u),
            timing,
            policy,
            trig,
            crt.spread().tables(),
            crt.random(),
            esp.shotgun_pattern(),
            animation.trigonometry(),
            {animation::native_sse_atan2},
            animation::native_sse_arctangent,
            data.native_cell<std::array<animation::AnimationAngleSample, 2>>(0x434759DCu),
            feedback.pending_shots(),
            reconstruction};
        BufferedShotCommandOwner current(state, bindings);
        repair_buffered_shot_commands(current, frame, player, send);
    }
};
BufferedShotRuntimeOwner::BufferedShotRuntimeOwner(
    ClientRuntimeStorage& s, const EngineRuntimeBindings& e, ConfigurationRuntimeOwner& c,
    HistoryRuntimeOwner& h, CommandCaptureRuntimeOwner& capture, animation::RuntimeOwner& b,
    CrtRuntimeOwner& crt, esp::EspRuntimeOwner& esp, rage::RuntimeOwner& rage,
    rage::FeedbackRuntimeOwner& feedback)
    : data_(std::make_unique<Data>(s, e, c, h, capture, b, crt, esp, rage, feedback)) {}
BufferedShotRuntimeOwner::~BufferedShotRuntimeOwner() = default;
ClientRuntimeStorage& BufferedShotRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
BufferedShotCommandOwner& BufferedShotRuntimeOwner::command() noexcept {
    return data_->command_;
}
BufferedShotCommandStorage& BufferedShotRuntimeOwner::state() noexcept {
    return data_->state_;
}
BufferedShotFactoryBindings& BufferedShotRuntimeOwner::factory_bindings() noexcept {
    return data_->factory;
}
void BufferedShotRuntimeOwner::bind_initializer(NativeStartupSources& sources) {
    bind_buffered_shot_factory(sources, data_->factory);
}
} //namespace skeet::client
