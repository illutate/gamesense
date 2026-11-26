#include "skeet/misc/purchase_log_owner.hpp"
#include "skeet/misc/native_text.hpp"
#include "skeet/script/client_api.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
void __fastcall purchase_configuration_frame(void* object, std::uint8_t notify) {
#else
void purchase_configuration_frame(void* object, std::uint8_t notify) {
#endif
    auto& handler =
        *static_cast<PurchaseLogFrameHandler*>(static_cast<engine::FrameUpdateEndpoint*>(object));
    handler.runtime.configuration_changed(notify != 0, handler.events);
}
const engine::FrameUpdateEntry frame_table[]{purchase_configuration_frame};
}
PurchaseLogFrameHandler::PurchaseLogFrameHandler(engine::OwnerRegistryBinding frames,
                                                 PurchaseLogRuntime& source,
                                                 events::EventRegistry& registry)
    : runtime(source), events(registry) {
    engine::register_frame_update(frames, this); //433FCB12, before table assignment
    vtable = frame_table;
}

namespace {
#if defined(_M_IX86) || defined(__i386__)
#define PURCHASE_MEMBER __thiscall
#define PURCHASE_FAST __fastcall
#else
#define PURCHASE_MEMBER
#define PURCHASE_FAST
#endif
using OwnerEntry = void(PURCHASE_MEMBER*)(void*);
using ConsoleInputEntry = void(PURCHASE_MEMBER*)(void*, void*, const char*);
using NativePurchaseEvent = void(PURCHASE_FAST*)(void*, void*, void*);
void PURCHASE_MEMBER destroy_purchase_owner(void*);
void PURCHASE_MEMBER capture_console_owner(void*);
void PURCHASE_MEMBER restore_console_owner(void*);
void PURCHASE_MEMBER console_input_owner_entry(void*, void*, const char*);
void PURCHASE_MEMBER unchanged_owner(void*) {} //433CCE8E RET
void PURCHASE_FAST native_purchase_listener(void*, void*, void*);
void PURCHASE_FAST native_purchase_frame(void*, std::uint8_t);
struct OwnerTable {
    std::array<OwnerEntry, 9> lifecycle;
    ConsoleInputEntry console_input;
};
const OwnerTable owner_table{{destroy_purchase_owner, unchanged_owner, unchanged_owner,
                              capture_console_owner, restore_console_owner, unchanged_owner,
                              unchanged_owner, unchanged_owner, unchanged_owner},
                             console_input_owner_entry};
#if UINTPTR_MAX == UINT32_MAX
static_assert(offsetof(OwnerTable, console_input) == 0x24);
static_assert(sizeof(OwnerTable) == 0x28);
#endif
const engine::FrameUpdateEntry native_frame_table[]{native_purchase_frame};
struct NativePurchaseSubscription {
    std::uint32_t hash;
    void* owner;
    NativePurchaseEvent callback;
    std::uint8_t subscribed;
    std::array<std::byte, 3> preserved;
};
struct PurchaseLogPrefix {
    const OwnerTable* vtable; //0,4346F750
    std::uint32_t preserved; //4
    engine::FrameUpdateEndpoint frame; //8,4346EF98
    EventLogConfig configuration; //C,values1C/30
    NativeEncryptedText hit_format; //34
    NativeEncryptedText purchase_format; //3C
    NativePurchaseSubscription purchase; //44,raw subscription50
    std::array<NativeText, 9> hitgroups; //54..D4
};
static_assert(std::is_trivially_copyable_v<PurchaseLogPrefix>);
// Clang's layout extension preserves the native inherited-config prefix.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
constexpr std::size_t purchase_frame_offset = offsetof(PurchaseLogPrefix, frame);
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(PurchaseLogPrefix) == 0xE4);
static_assert(offsetof(PurchaseLogPrefix, frame) == 8);
static_assert(offsetof(PurchaseLogPrefix, configuration) == 0xC);
static_assert(offsetof(PurchaseLogPrefix, hit_format) == 0x34);
static_assert(offsetof(PurchaseLogPrefix, purchase_format) == 0x3C);
static_assert(offsetof(PurchaseLogPrefix, purchase) == 0x44);
static_assert(offsetof(PurchaseLogPrefix, hitgroups) == 0x54);
#endif
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

class PurchaseLogLifecycleOwner {
public:
    PurchaseLogPrefix prefix;
    engine::JobMemory memory;
    events::EventRegistry& events;
    script::ConsoleInputHook& console;
    void* const& console_container;
    alignas(PurchaseLogRuntime) std::array<std::byte, sizeof(PurchaseLogRuntime)> state;

    PurchaseLogLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                              engine::OwnerRegistryBinding frames, ConfigRegistries& registries,
                              events::EventRegistry& registry, NormalLog& output,
                              rage::TargetRelationshipBindings targets, void* entity_list,
                              script::ConsoleInputHook& input, void* const& container,
                              const PurchaseLogPrefix& previous)
        : memory(lifecycle.memory), events(registry), console(input), console_container(container) {
        // The native constructor preserves reserved words/bytes and both
        // existing boolean values. Restore the caller's prefix before its
        // first observable registration, then perform only native writes.
        std::memcpy(&prefix, &previous, sizeof prefix);
        prefix.vtable = nullptr;
        lifecycle.append(this); //433676A0, before vtable assignment
        engine::register_frame_update(frames, &prefix.frame); //433FCB12
        prefix.vtable = &owner_table;
        prefix.frame.vtable = native_frame_table;
        initialize_event_log_config(registries, prefix.configuration);

        constexpr char hit_format[] = "Hit %s in the %s for %d damage (%d health remaining)";
        static_assert(sizeof(hit_format) == 0x35);
        construct_native_encrypted_text(prefix.hit_format, hit_format, sizeof(hit_format), memory);
        // Native encrypts14 bytes: the12 visible characters and TWO zeros.
        constexpr std::array<char, 0xE> purchase_format{'%', 's', ' ', 'b', 'o', 'u', 'g',
                                                        'h', 't', ' ', '%', 's', 0,   0};
        construct_native_encrypted_text(prefix.purchase_format, purchase_format.data(),
                                        purchase_format.size(), memory);
        prefix.purchase.hash = 0xD6CAA6ED;
        prefix.purchase.owner = this;
        prefix.purchase.callback = native_purchase_listener;
        prefix.purchase.subscribed = 0;
        constexpr const char* groups[]{"body",      "head",     "chest",     "stomach", "left arm",
                                       "right arm", "left leg", "right leg", "neck"};
        for (std::size_t i = 0; i < prefix.hitgroups.size(); ++i)
            construct_native_text(prefix.hitgroups[i], groups[i], memory);

        ::new (state.data())
            PurchaseLogRuntime(prefix.configuration, output, targets, prefix.purchase_format,
                               prefix.hit_format, prefix.hitgroups, prefix.purchase.subscribed);
        runtime().entity_list = entity_list;
        runtime().bind_events(events); //actual constructor's permanent damage listener
    }
    PurchaseLogRuntime& runtime() noexcept {
        return *std::launder(reinterpret_cast<PurchaseLogRuntime*>(state.data()));
    }
};
#if UINTPTR_MAX == UINT32_MAX
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(PurchaseLogLifecycleOwner, prefix) == 0);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

namespace {
void PURCHASE_FAST native_purchase_frame(void* endpoint, std::uint8_t notify) {
    auto* owner = reinterpret_cast<PurchaseLogLifecycleOwner*>(static_cast<std::byte*>(endpoint) -
                                                               purchase_frame_offset);
    owner->runtime().configuration_changed(notify != 0, owner->events);
}
void PURCHASE_FAST native_purchase_listener(void* object, void* raw_event, void* local) {
    auto& owner = *static_cast<PurchaseLogLifecycleOwner*>(object);
    events::EngineGameEventReader event(raw_event);
    events::EngineEventPlayers players(owner.events.engine_interface, local);
    owner.runtime().item_purchase(event, players); //same433A0D37 source and backing
}
void PURCHASE_MEMBER capture_console_owner(void* object) { //43482378
    auto& owner = *static_cast<PurchaseLogLifecycleOwner*>(object);
    owner.console.capture(owner.console_container, &owner.prefix);
}
void PURCHASE_MEMBER restore_console_owner(void* object) { //433F8C58
    static_cast<PurchaseLogLifecycleOwner*>(object)->console.restore();
}
void PURCHASE_MEMBER console_input_owner_entry(void* object, void* engine_object,
                                               const char* text) { //433410B6
    process_purchase_console_input(*static_cast<PurchaseLogLifecycleOwner*>(object), engine_object,
                                   text);
}
void PURCHASE_MEMBER destroy_purchase_owner(void* object) { //433B6C4E ->433E0610
    auto* owner = static_cast<PurchaseLogLifecycleOwner*>(object);
    if (!owner)
        return;
    const auto memory = owner->memory;
    for (std::size_t i = owner->prefix.hitgroups.size(); i != 0; --i)
        destroy_native_text(owner->prefix.hitgroups[i - 1], memory);
    owner->runtime().disable_purchase_subscription(owner->events);
    destroy_native_encrypted_text(owner->prefix.purchase_format, memory);
    destroy_native_encrypted_text(owner->prefix.hit_format, memory);
    // The permanent damage triple remains until the common native registry
    // shutdown; that registry is cleared before final owner destruction.
    owner->runtime().~PurchaseLogRuntime();
    owner->~PurchaseLogLifecycleOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
std::size_t purchase_log_owner_size() noexcept {
    return sizeof(PurchaseLogLifecycleOwner);
}
PurchaseLogLifecycleOwner&
construct_purchase_log(void* storage, engine::OwnerRegistryBinding lifecycle,
                       engine::OwnerRegistryBinding frames, ConfigRegistries& registries,
                       events::EventRegistry& events, NormalLog& output,
                       rage::TargetRelationshipBindings targets, void* entity_list,
                       script::ConsoleInputHook& console, void* const& container) {
    PurchaseLogPrefix previous;
    std::memcpy(&previous, storage, sizeof previous);
    return *::new (storage)
        PurchaseLogLifecycleOwner(lifecycle, frames, registries, events, output, targets,
                                  entity_list, console, container, previous);
}
PurchaseLogRuntime& purchase_log_runtime(PurchaseLogLifecycleOwner& owner) noexcept {
    return owner.runtime();
}
EventLogConfig& purchase_log_configuration(PurchaseLogLifecycleOwner& owner) noexcept {
    return owner.prefix.configuration;
}
void process_purchase_console_input(PurchaseLogLifecycleOwner& owner, void* engine_object,
                                    const char* text) {
    owner.console.process(engine_object, text);
}
#undef PURCHASE_MEMBER
#undef PURCHASE_FAST
} //namespace skeet::misc
