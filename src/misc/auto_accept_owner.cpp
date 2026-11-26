#include "skeet/misc/auto_accept_owner.hpp"
#include "skeet/misc/auto_accept_hook.hpp"
#include "skeet/misc/native_text.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define ACCEPT_MEMBER __thiscall
#else
#define ACCEPT_MEMBER
#endif
using OwnerEntry = void(ACCEPT_MEMBER*)(void*);
void ACCEPT_MEMBER destroy_owner(void*);
void ACCEPT_MEMBER install_owner(void*);
void ACCEPT_MEMBER restore_owner(void*);
void ACCEPT_MEMBER unchanged_owner(void*) {} //433CCE8E RET
const std::array<OwnerEntry, 9> lifecycle_table{destroy_owner,   unchanged_owner, unchanged_owner,
                                                install_owner,   restore_owner,   unchanged_owner,
                                                unchanged_owner, unchanged_owner, unchanged_owner};
struct AutoAcceptPrefix {
    const OwnerEntry* vtable;
    AutoAcceptConfig configuration;
    NativeEncryptedText name;
    std::uint32_t name_hash;
    void* callback;
};
static_assert(std::is_trivially_copyable_v<AutoAcceptPrefix>);
#if UINTPTR_MAX == UINT32_MAX
// Clang's x86 layout extension verifies the native inherited-config prefix.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(sizeof(AutoAcceptPrefix) == 0x28);
static_assert(sizeof(AutoAcceptConfig) == 0x14);
static_assert(offsetof(BooleanConfig, value) == 0x10);
static_assert(offsetof(AutoAcceptPrefix, configuration) == 4);
static_assert(offsetof(AutoAcceptPrefix, name) == 0x18);
static_assert(offsetof(AutoAcceptPrefix, name_hash) == 0x20);
static_assert(offsetof(AutoAcceptPrefix, callback) == 0x24);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif
template <class T> T field(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
void free_aligned(engine::JobMemory memory, void* object) { //433837D0
    if (!object)
        return;
    auto* bytes = static_cast<std::byte*>(object);
    memory.free(memory.process_heap, 0, bytes - 16 - field<std::uint32_t>(bytes, -8));
}
AutoAcceptLifecycleOwner** active_owner_slot = nullptr;
}

class AutoAcceptLifecycleOwner {
public:
    AutoAcceptPrefix prefix;
    engine::JobMemory memory;
    AutoAcceptOwnerBindings bindings;

    AutoAcceptLifecycleOwner(engine::OwnerRegistryBinding lifecycle, ConfigRegistries& registries,
                             AutoAcceptOwnerBindings shared, const AutoAcceptPrefix& previous)
        : memory(lifecycle.memory), bindings(shared) {
        std::memcpy(&prefix, &previous, sizeof prefix);
        prefix.vtable = nullptr;
        lifecycle.append(this);
        prefix.vtable = lifecycle_table.data(); //4346EDFC
        initialize_auto_accept_config(registries, prefix.configuration);
        construct_native_encrypted_text(prefix.name, "accept", 6, memory);
        prefix.name_hash = 0xE8BB3240;
        prefix.callback = bindings.constructor_callback;
    }
};
#if UINTPTR_MAX == UINT32_MAX
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(AutoAcceptLifecycleOwner, prefix) == 0);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#endif

namespace {
void ACCEPT_MEMBER install_owner(void* object) {
    install_auto_accept(*static_cast<AutoAcceptLifecycleOwner*>(object));
}
void ACCEPT_MEMBER restore_owner(void* object) {
    restore_auto_accept(*static_cast<AutoAcceptLifecycleOwner*>(object));
}
void ACCEPT_MEMBER destroy_owner(void* object) { //433E3DAC
    *active_owner_slot = nullptr;
    auto* owner = static_cast<AutoAcceptLifecycleOwner*>(object);
    if (!owner)
        return;
    const auto memory = owner->memory;
    destroy_native_encrypted_text(owner->prefix.name, memory);
    owner->~AutoAcceptLifecycleOwner();
    free_aligned(memory, owner);
}
}

std::size_t auto_accept_owner_size() noexcept {
    return sizeof(AutoAcceptLifecycleOwner);
}
AutoAcceptLifecycleOwner& construct_auto_accept(void* storage,
                                                engine::OwnerRegistryBinding lifecycle,
                                                ConfigRegistries& registries,
                                                AutoAcceptOwnerBindings bindings) {
    active_owner_slot = &bindings.active_owner;
    AutoAcceptPrefix previous;
    std::memcpy(&previous, storage, sizeof previous);
    auto* owner =
        ::new (storage) AutoAcceptLifecycleOwner(lifecycle, registries, bindings, previous);
    bindings.active_owner = owner;
    return *owner;
}
AutoAcceptConfig& auto_accept_configuration(AutoAcceptLifecycleOwner& owner) noexcept {
    return owner.prefix.configuration;
}
AutoAcceptHandlerData auto_accept_handler_data(AutoAcceptLifecycleOwner& owner) noexcept {
    return {owner.prefix.configuration, owner.prefix.name, owner.prefix.name_hash,
            owner.prefix.callback};
}
void install_auto_accept(AutoAcceptLifecycleOwner& owner) {
    if (!owner.prefix.callback || !owner.bindings.source_module)
        return;
    auto* ui_engine = engine::invoke<void*>(owner.bindings.panorama, 11);
    if (!ui_engine)
        return;
    auto* target = engine::invoke<void*>(ui_engine, 25);
    if (!target)
        return;
    owner.bindings.patch.metadata.target = target;
    owner.bindings.patch.metadata.owner = &owner;
}
void restore_auto_accept(AutoAcceptLifecycleOwner& owner) {
    auto& descriptor = owner.bindings.patch.metadata;
    if (!descriptor.replacement_table || !descriptor.target)
        return;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(descriptor.target),
                               descriptor.original_table);
}
std::span<const AutoAcceptHookReplacement, 2> auto_accept_hook_replacements() {
    static const std::array<AutoAcceptHookReplacement, 2> replacements{
        {{0, 0x4338A9E9u, reinterpret_cast<void*>(&auto_accept_restore_hook)},
         {1, 0x43320430u, reinterpret_cast<void*>(&auto_accept_event_hook)}}};
    return replacements;
}
AutoAcceptHookFamily::AutoAcceptHookFamily(AutoAcceptPatchState& patch,
                                           engine::VtableHookRegistry& hooks,
                                           std::byte* const& source_module,
                                           engine::NativeImageEnvironment images,
                                           engine::HookDescriptorSystem system) noexcept
    : patch_(patch), hooks_(hooks), source_module_(source_module), images_(images),
      system_(system) {}
void AutoAcceptHookFamily::prepare() {
    engine::prepare_rtti_vtable_hook(patch_.metadata, source_module_, auto_accept_hook_class_hash,
                                     auto_accept_hook_base_hash, images_, hooks_.memory, system_);
    bind_replacements();
}
void AutoAcceptHookFamily::bind_replacements() noexcept {
    // The slot0 callback reads this same descriptor view even if cleanup-node
    // allocation fails after install_vtable_hook has performed its atomic swap.
    bind_auto_accept_restore_hook(patch_);
    for (const auto& replacement : auto_accept_hook_replacements())
        patch_.metadata.replacement_table[replacement.slot] = replacement.source_entry;
}
engine::VtableHookNode* AutoAcceptHookFamily::install(void* actual_receiver) {
    return engine::install_vtable_hook(hooks_, actual_receiver, patch_.metadata);
}
#undef ACCEPT_MEMBER
} //namespace skeet::misc
