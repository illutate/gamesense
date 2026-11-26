#include "skeet/client/source_terminal_cleanup.hpp"
#include "skeet/client/crt_initialization.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/player_runtime_owner.hpp"
#include "skeet/script/source_runtime_owner.hpp"
#include "skeet/lagcomp/history_runtime.hpp"
#include "skeet/script/initialize.hpp"
#include "skeet/script/modules.hpp"
#include "skeet/script/registry.hpp"
#include "skeet/script/shutdown.hpp"
#include "skeet/script/ui.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace skeet::client {
namespace {
constexpr std::array<SourceTerminalCleanupSlot, 8> slots{{
    {0x1CCu, 0x4332BB3Au, 0x433C956Cu},
    {0x1ECu, 0x433C7FAEu, 0x4332F1FDu},
    {0x1F0u, 0x4334C970u, 0x4334DA70u},
    {0x204u, 0x43325025u, 0x4339D4FEu},
    {0x20Cu, 0x433F47E6u, 0x433874D2u},
    {0x210u, 0x4340BCC1u, 0x4341EFD3u},
    {0x228u, 0x433412AFu, 0x433C8C11u},
    {0x2CCu, 0x4334115Au, 0x433AFCE8u},
}};
SourceTerminalCleanupBindings* active_cleanup = nullptr;

template <std::size_t Index> void finalize_entry() {
    if (!active_cleanup)
        throw std::logic_error("Source terminal callback has no process owner");
    active_cleanup->finalize(slots[Index].finalizer);
}
template <std::size_t... Index> constexpr auto finalizer_functions(std::index_sequence<Index...>) {
    return std::array<void (*)(), sizeof...(Index)>{&finalize_entry<Index>...};
}
constexpr auto functions = finalizer_functions(std::make_index_sequence<slots.size()>{});
template <std::size_t Index> void initialize_entry(void* owner) {
    static_cast<SourceTerminalCleanupBindings*>(owner)->initialize(slots[Index].initializer);
}
template <std::size_t... Index>
void bind_entries(NativeStartupSources& sources, SourceTerminalCleanupBindings& binding,
                  std::index_sequence<Index...>) {
    (sources.bind_initializer({slots[Index].initializer, &binding, &initialize_entry<Index>}), ...);
}
void require_plan() {
    const auto plan = crt_initialization_plan();
    for (const auto& slot : slots) {
        bool found = false;
        for (const auto& entry : plan)
            if (entry.operand == slot.operand && entry.identifier == slot.initializer) {
                found = true;
                break;
            }
        if (!found)
            throw std::logic_error("Source terminal cleanup differs from the configured CRT plan");
    }
}
}

std::span<const SourceTerminalCleanupSlot, 8> source_terminal_cleanup_plan() noexcept {
    return slots;
}

SourceTerminalCleanupBindings::SourceTerminalCleanupBindings(
    ExitCallbacks& exits, script::Bridge& callback_bridge, script::BrowserState& panorama,
    script::UiBindings& bindings, script::ReferenceStrings& flags, PlayerStateCache& cache,
    lagcomp::RecordPool& pool)
    : callbacks(exits), bridge(callback_bridge), browser(panorama), ui(bindings), strings(flags),
      players(cache), records(pool) {
    if (callbacks.entries.size() != 0x200)
        throw std::invalid_argument(
            "Source terminal cleanup requires the canonical512-slot exit registry");
    require_plan();
}
SourceTerminalCleanupBindings::SourceTerminalCleanupBindings(
    ClientRuntimeStorage& storage, script::SourceScriptRuntimeOwner& scripts,
    PlayerRuntimeOwner& player_owner)
    : SourceTerminalCleanupBindings(storage.exit_callbacks(), scripts.bridge(),
                                    scripts.browser_state(), scripts.ui_bindings(), scripts.flags(),
                                    player_owner.cache(), player_owner.records()) {
    if (&scripts.storage() != &storage || &player_owner.storage() != &storage ||
        &callbacks != &storage.exit_callbacks() || &bridge != &scripts.runtime().bridge ||
        &bridge != &scripts.extensions().process_bridge() ||
        &browser != &scripts.extensions().browser_state() ||
        &bridge.name_storage != &storage.event_names())
        throw std::invalid_argument(
            "Early source terminal cleanup requires the actual canonical script and player owners");
    source_owners_validated_ = true;
}
void SourceTerminalCleanupBindings::require_producers(
    const script::ScriptRuntime& runtime, const script::ScriptModulesBindings& modules,
    const lagcomp::HistoryRecordingBindings& history) {
    if (&bridge != &runtime.bridge || &bridge != &modules.extensions.process_bridge() ||
        &browser != &modules.extensions.browser_state() || &ui != &modules.ui ||
        &strings != &modules.flags || &players != &modules.player_cache ||
        &players != &history.players || &records != &history.records ||
        &modules.players != &history.globals.adjustments)
        throw std::invalid_argument(
            "Source terminal cleanup must borrow the actual script and history producers");
    producers_validated_ = true;
}
void SourceTerminalCleanupBindings::initialize(std::uint32_t address) {
    for (const auto& slot : slots) {
        if (slot.initializer != address)
            continue;
        //4332BB3A writes only native inline-buffer metadata and an unused word.
        //The canonical C++ vector is constructed by its process owner already;
        //its source producer must not run before this CRT registration point.
        if (address == 0x4332BB3Au && !strings.entries.empty())
            throw std::logic_error(
                "Reference-string production precedes its source CRT initializer");
        (void)callbacks.append(source_finalizer(slot.finalizer));
        return;
    }
    throw std::out_of_range("Initializer is not a source-owned terminal cleanup registration");
}
void SourceTerminalCleanupBindings::finalize(std::uint32_t address) {
    switch (address) {
    case 0x4332F1FDu:
        bridge.finalize_active_callback_storage();
        break;
    case 0x4334DA70u:
        bridge.finalize_dirty_callback_storage();
        break;
    case 0x4339D4FEu:
        browser.finalize_object_handles();
        break;
    case 0x433874D2u:
        browser.finalize_script_handles();
        break;
    case 0x4341EFD3u:
        browser.finalize_global_handle();
        break;
    case 0x433C8C11u:
        ui.finalize_record_storage();
        break;
    case 0x433AFCE8u:
        players.finalize_history(records);
        break;
    case 0x433C956Cu:
        strings.finalize_storage();
        break;
    default:
        throw std::out_of_range("Finalizer is not a source-owned terminal cleanup body");
    }
}
void (*SourceTerminalCleanupBindings::source_finalizer(std::uint32_t address))() {
    for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index].finalizer != address)
            continue;
        if (active_cleanup && active_cleanup != this)
            throw std::logic_error("Source terminal callbacks require one stable process context");
        active_cleanup = this;
        return functions[index];
    }
    throw std::out_of_range("Finalizer is not a source-owned terminal cleanup body");
}

void bind_source_terminal_cleanup(NativeStartupSources& sources,
                                  SourceTerminalCleanupBindings& binding) {
    if (!binding.source_owners_validated_ && !binding.producers_validated_)
        throw std::logic_error(
            "Validate canonical source owners before selecting their terminal startup profile");
    if (binding.callbacks.count != 0)
        throw std::logic_error(
            "Select source terminal ownership before any CRT exits are registered");
    require_plan();
    bind_entries(sources, binding, std::make_index_sequence<slots.size()>{});
}
} //namespace skeet::client
