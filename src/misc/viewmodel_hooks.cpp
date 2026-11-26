#include "skeet/misc/viewmodel_hooks.hpp"
#include "skeet/misc/viewmodel_lag_owner.hpp"
#include "skeet/menu/misc_menu.hpp"
#include "skeet/effects/view.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::misc {
namespace {
ViewmodelHooksRuntime* active_runtime = nullptr;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void assign(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
const engine::VtableHookMetadata& metadata(void* receiver) {
    const auto* table = *static_cast<void***>(receiver);
    return *static_cast<const engine::VtableHookMetadata*>(table[-2]);
}
}
std::span<const ViewmodelHookReplacement, 6> viewmodel_hook_replacements() {
    static const std::array<ViewmodelHookReplacement, 6> replacements{{
        {0, 0x43402028, reinterpret_cast<void*>(&viewmodel_destroy_hook)},
        {206, 0x43359D27, reinterpret_cast<void*>(&viewmodel_206_hook)},
        {113, 0x43324F45, reinterpret_cast<void*>(&viewmodel_113_hook)},
        {247, 0x4336BA27, reinterpret_cast<void*>(&viewmodel_247_hook)},
        {248, 0x4340821A, reinterpret_cast<void*>(&viewmodel_248_hook)},
        {250, 0x433B0541, reinterpret_cast<void*>(&viewmodel_250_hook)},
    }};
    return replacements;
}
ViewmodelHooksRuntime::ViewmodelHooksRuntime(ViewmodelLagState& owner_state,
                                             ViewmodelLagOwner*& owner,
                                             const ui::SettingsMenuValues& settings,
                                             ViewmodelHookInputs inputs, effects::ViewRuntime& view,
                                             engine::VtableHookRegistry& registry,
                                             engine::HookDescriptorStorage& descriptors)
    : owner_state_(owner_state), owner_(owner), enabled_(settings.fix_viewmodel_lag),
      inputs_(inputs), view_(view), registry_(registry), image_(descriptors.image()),
      descriptor_(descriptors.at(viewmodel_hook_descriptor)), feature_{enabled_,
                                                                       inputs_.engine,
                                                                       inputs_.entity_list,
                                                                       &inputs_.prediction_time,
                                                                       &inputs_.interval,
                                                                       inputs_.interpolation_cvar,
                                                                       inputs_.offsets} {
    if (&owner_ != &image_.native_cell<ViewmodelLagOwner*>(viewmodel_lag_owner_slot))
        throw std::invalid_argument(
            "Viewmodel hooks require canonical retained-image owner slot43475860");
}
const ViewmodelLagBindings& ViewmodelHooksRuntime::feature_bindings() const noexcept {
    return feature_;
}
ViewmodelLagHookBindings
ViewmodelHooksRuntime::hook_bindings(const ViewmodelLagBindings& feature) const {
    return {feature,
            inputs_.input,
            inputs_.local_player,
            inputs_.player_tick_base,
            &inputs_.prediction_time,
            &inputs_.interpolation_fraction};
}
void ViewmodelHooksRuntime::prepare(void** original_table, engine::JobMemory memory,
                                    engine::HookDescriptorSystem system) {
    engine::prepare_vtable_hook(descriptor_, original_table, memory, system);
    bind_replacements();
}
void ViewmodelHooksRuntime::prepare_from_module(std::byte* module,
                                                engine::NativeImageEnvironment images,
                                                engine::JobMemory memory,
                                                engine::HookDescriptorSystem system) {
    // This is the whole source recipe14 operation. It is not inserted before
    // the preserved VM's separate raw replacement-operand loop.
    engine::prepare_rtti_vtable_hook(descriptor_, module, viewmodel_hook_class_hash, 0, images,
                                     memory, system);
    bind_replacements();
}
void ViewmodelHooksRuntime::bind_replacements() noexcept {
    // Publish before the atomic swap; native may install the vtable even when
    // allocating its cleanup-list node subsequently fails.
    active_runtime = this;
    for (const auto& replacement : viewmodel_hook_replacements())
        descriptor_.replacement_table[replacement.slot] = replacement.source_entry;
}
engine::VtableHookNode* ViewmodelHooksRuntime::install(void* receiver) {
    return engine::install_vtable_hook(registry_, receiver, descriptor_);
}
std::uintptr_t ViewmodelHooksRuntime::destroy(void* receiver, std::uint32_t argument) { //43402028
    engine::uninstall_vtable_hook(registry_, receiver);
    // Native rereads the receiver after restoring its table and invokes slot0
    // through433E533F. The engine deleting-destructor argument is unchanged.
    return engine::invoke<std::uintptr_t>(receiver, 0, argument);
}
bool ViewmodelHooksRuntime::method113(void* receiver, float argument) {
    const auto feature = feature_bindings();
    return viewmodel_method_113(viewmodel_lag_owner_state(owner_), hook_bindings(feature), receiver,
                                argument);
}
std::uint32_t ViewmodelHooksRuntime::method206(void* receiver, std::uint32_t first,
                                               std::uint32_t second, std::uint32_t third,
                                               std::uint32_t fourth, float fifth) {
    const auto feature = feature_bindings();
    return viewmodel_method_206(viewmodel_lag_owner_state(owner_), hook_bindings(feature), receiver,
                                {first, second, third, fourth}, fifth);
}
void ViewmodelHooksRuntime::method247(void* receiver, std::int32_t argument) {
    const auto feature = feature_bindings();
    viewmodel_method_247(viewmodel_lag_owner_state(owner_), hook_bindings(feature), receiver,
                         argument);
}
std::uint32_t ViewmodelHooksRuntime::method248(void* receiver, std::uint32_t first,
                                               std::uint32_t second) { //4340821A
    const auto token = field<std::uint32_t>(receiver, inputs_.offsets.animation_token_0c);
    const auto previous = field<std::uint32_t>(receiver, inputs_.offsets.field_18);
    const auto original =
        reinterpret_cast<engine::MemberFunction<std::uint32_t, std::uint32_t, std::uint32_t>>(
            metadata(receiver).original_table[248]);
    const auto result = original(receiver, first, second);
    assign(receiver, inputs_.offsets.field_18, previous);
    assign(receiver, inputs_.offsets.animation_token_0c, token);
    return result;
}
void ViewmodelHooksRuntime::method250(void* receiver, std::uintptr_t first, std::uintptr_t second,
                                      std::uintptr_t third) {
    view_.recoil_scale_hooked(receiver, inputs_.encoded_recoil_scale, first, second, third);
}
void ViewmodelHooksRuntime::update_entities(void* player, std::uintptr_t) {
    const auto feature = feature_bindings();
    repair_viewmodel_lag(owner_state_, feature, player);
}
void ViewmodelHooksRuntime::frame_update() {
    consume_viewmodel_prediction(owner_state_);
}

#if defined(_M_IX86) || defined(__i386__)
#define SKEET_VIEWMODEL_HOOK_ABI __fastcall
#else
#define SKEET_VIEWMODEL_HOOK_ABI
#endif
std::uintptr_t SKEET_VIEWMODEL_HOOK_ABI viewmodel_destroy_hook(void* receiver, void*,
                                                               std::uint32_t argument) {
    return active_runtime->destroy(receiver, argument);
}
bool SKEET_VIEWMODEL_HOOK_ABI viewmodel_113_hook(void* receiver, void*, float argument) {
    return active_runtime->method113(receiver, argument);
}
std::uint32_t SKEET_VIEWMODEL_HOOK_ABI viewmodel_206_hook(void* receiver, void*, std::uint32_t a,
                                                          std::uint32_t b, std::uint32_t c,
                                                          std::uint32_t d, float e) {
    return active_runtime->method206(receiver, a, b, c, d, e);
}
void SKEET_VIEWMODEL_HOOK_ABI viewmodel_247_hook(void* receiver, void*, std::int32_t argument) {
    active_runtime->method247(receiver, argument);
}
std::uint32_t SKEET_VIEWMODEL_HOOK_ABI viewmodel_248_hook(void* receiver, void*,
                                                          std::uint32_t first,
                                                          std::uint32_t second) {
    return active_runtime->method248(receiver, first, second);
}
void SKEET_VIEWMODEL_HOOK_ABI viewmodel_250_hook(void* receiver, void*, std::uintptr_t first,
                                                 std::uintptr_t second, std::uintptr_t third) {
    active_runtime->method250(receiver, first, second, third);
}
#undef SKEET_VIEWMODEL_HOOK_ABI
} //namespace skeet::misc
