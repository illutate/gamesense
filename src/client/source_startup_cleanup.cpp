#include "skeet/client/source_startup_cleanup.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/engine/unload.hpp"
#include <cstdint>
#include <cstring>
#include <limits>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::client {
namespace {
enum class Access {
    read,
    write,
    execute
};
bool accessible(const void* pointer, std::size_t length, Access access) noexcept {
    auto position = reinterpret_cast<std::uintptr_t>(pointer);
    if (!position || !length || length > std::numeric_limits<std::uintptr_t>::max() - position)
        return false;
    const auto end = position + length;
    do {
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<const void*>(position), &region, sizeof region) !=
                sizeof region ||
            region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const auto protection = region.Protect & 0xFFu;
        const auto writable = protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                              protection == PAGE_EXECUTE_READWRITE ||
                              protection == PAGE_EXECUTE_WRITECOPY;
        const auto executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                                protection == PAGE_EXECUTE_READWRITE ||
                                protection == PAGE_EXECUTE_WRITECOPY;
        const auto readable =
            writable || protection == PAGE_READONLY || protection == PAGE_EXECUTE_READ;
        if ((access == Access::read && !readable) || (access == Access::write && !writable) ||
            (access == Access::execute && !executable))
            return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(region.BaseAddress);
        if (region.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin)
            return false;
        const auto next = begin + region.RegionSize;
        if (next <= position)
            return false;
        position = next;
    } while (position < end);
    return true;
}
template <class T> T read(const void* pointer) noexcept {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
bool executable(const void* pointer) noexcept {
    return accessible(pointer, 1, Access::execute);
}
template <class Function> bool callable(Function function) noexcept {
    return executable(reinterpret_cast<const void*>(function));
}
bool receiver_entry(void* receiver, std::size_t slot) noexcept {
    if (!accessible(receiver, sizeof(void*), Access::read))
        return false;
    auto** table = read<void**>(receiver);
    if (!accessible(table, (slot + 1) * sizeof(void*), Access::read))
        return false;
    return executable(read<void*>(table + slot));
}
bool primary_owners_ready(const engine::OwnerRegistry& owners) noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(owners.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(owners.end);
    const auto limit = reinterpret_cast<std::uintptr_t>(owners.limit);
    if (!begin)
        return !end && !limit;
    if (end < begin || limit < end || (end - begin) % sizeof(void*) ||
        (limit - begin) % sizeof(void*) || (end - begin) / sizeof(void*) > 57u)
        return false;
    if (end != begin && !accessible(owners.begin, end - begin, Access::read))
        return false;
    for (auto position = begin; position != end; position += sizeof(void*)) {
        auto* owner = read<void*>(reinterpret_cast<void*>(position));
        if (!receiver_entry(owner, 4) || !receiver_entry(owner, 0))
            return false;
    }
    return true;
}
bool hooks_ready(const engine::VtableHookNode& sentinel) noexcept {
    const auto* previous = &sentinel;
    auto* current = sentinel.next;
    // This is a bounded startup-cancellation check, not a map-lifetime list
    // walk. All57 factories plus their initial entity receivers fit this cut.
    constexpr std::size_t maximum_startup_hooks = 4096;
    std::size_t count = 0;
    while (current != &sentinel) {
        if (++count > maximum_startup_hooks ||
            !accessible(current, sizeof(*current), Access::write))
            return false;
        const auto node = read<engine::VtableHookNode>(current);
        if (node.previous != previous || !accessible(node.object, sizeof(void*), Access::write))
            return false;
        auto** installed = read<void**>(node.object);
        const auto address = reinterpret_cast<std::uintptr_t>(installed);
        if (address < 2 * sizeof(void*) ||
            !accessible(installed - 2, 3 * sizeof(void*), Access::read))
            return false;
        auto* metadata = read<engine::VtableHookMetadata*>(installed - 2);
        if (!accessible(metadata, sizeof(*metadata), Access::read))
            return false;
        const auto descriptor = read<engine::VtableHookMetadata>(metadata);
        if (descriptor.replacement_table != installed ||
            !accessible(descriptor.original_table, sizeof(void*), Access::read))
            return false;
        previous = current;
        current = node.next;
    }
    return sentinel.previous == previous;
}
bool pointer_patch_ready(const engine::SavedPointerPatch& patch, bool active) noexcept {
    return !active || (!patch.target) ||
           (accessible(patch.target, sizeof(void*), Access::write) &&
            accessible(patch.saved, sizeof(void*), Access::read));
}
}
bool source_exit_callbacks_ready(const ClientRuntimeStorage& storage) noexcept {
    try {
        const auto& data = storage.data();
        const auto count = data.native_cell<std::uint32_t>(0x4347CA0Cu);
        if (count > 512u)
            return false;
        const auto mask = data.native_cell<std::uintptr_t>(0x4347CA08u);
        const auto& entries = data.native_cell<std::array<std::uintptr_t, 512>>(0x43479CF0u);
        for (std::uint32_t index = 0; index < count; ++index)
            if (!executable(reinterpret_cast<const void*>(entries[index] ^ mask)))
                return false;
        return true;
    } catch (...) {
        return false;
    }
}
bool source_startup_detach_ready(ClientRuntimeStorage& storage,
                                 WindowRuntimeOwner& window) noexcept {
    try {
        if (&window.storage() != &storage || !source_exit_callbacks_ready(storage))
            return false;
        const auto& state = window.unload_state();
        const auto& system = window.unload_runtime().system;
        if (&state.owners != &storage.owner_registries().at(0x43475B78u) ||
            !primary_owners_ready(state.owners) || !hooks_ready(state.hooks) ||
            !callable(system.free_heap))
            return false;
        if (state.in_game_callback &&
            (!callable(state.in_game_callback) || !receiver_entry(state.engine, 27)))
            return false;
        if (!pointer_patch_ready(state.first_patch, state.first_patch.enabled != 0) ||
            !pointer_patch_ready(state.second_patch, state.second_patch.enabled != 0) ||
            !pointer_patch_ready(state.final_patch, state.final_patch.enabled != 0) ||
            !pointer_patch_ready(state.cleared_patch,
                                 state.cleared_patch.target && state.cleared_patch.saved))
            return false;
        if (state.listener_registered &&
            (!state.event_listener || !receiver_entry(state.event_manager, 21)))
            return false;
        if (state.window.window && state.window.subclass_id &&
            (!callable(state.window.subclass_procedure) || !callable(system.set_window_long_w) ||
             !callable(system.set_window_long_a) || !callable(system.get_window_long_a) ||
             !callable(system.get_window_long_w) || !callable(system.remove_window_subclass)))
            return false;
        return true;
    } catch (...) {
        return false;
    }
}
} //namespace skeet::client
