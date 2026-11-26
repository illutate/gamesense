#include "skeet/animation/fix_animation_backup_cleanup.hpp"
#include "skeet/animation/state_copy.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/client/runtime_storage.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::animation {
namespace {
FixAnimationBackupCleanupContext* active_cleanup = nullptr;

void free_native_allocation(void* allocation, const engine::JobMemory& memory) {
    if (!allocation)
        return; //complete433837D0 null gate
    auto* bytes = static_cast<std::byte*>(allocation);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void finalize_backup_map() {
    active_cleanup->finalize();
}
void append_backup_map_finalizer(void* owner) {
    auto& context = *static_cast<FixAnimationBackupCleanupContext*>(owner);
    //433B86B4 MOVECX43359C18 -> JMP433837F5. Only registration occurs here.
    (void)context.callbacks.append(context.source_finalizer(0x43359C18u));
}
}

void cleanup_fix_animation_backups(FixAnimationBackupRuntime& runtime, void* const& allocator) {
    const auto& bindings = runtime.bindings;
    cleanup_fix_animation_backups(
        {bindings.buckets, bindings.bucket_count, bindings.size, bindings.memory}, allocator);
}
void cleanup_fix_animation_backups(const FixAnimationBackupMapBindings& map,
                                   void* const& allocator) {
    auto** const initial_buckets = map.buckets;
    const auto initial_count = map.bucket_count;
    for (std::uint32_t index = 0; index < initial_count; ++index) {
        auto* node = initial_buckets[index];
        while (node) {
            //4339D4A0 saves next before either genuine free boundary. The
            // native vector header is node+148, i.e. animation+13C.
            auto* const next = node->next;
            release_animation_state_vector(node->animation.data(), allocator);
            free_native_allocation(node, map.memory);
            node = next;
        }
        // The original captured array is cleared even if an engine/free
        // boundary changed the current global bucket pointer while releasing.
        initial_buckets[index] = nullptr;
    }
    //4339D4CE reloads the live count after a nonempty traversal. The size write
    // follows that read; the tail's allocation pointer is then read afresh.
    const auto final_count = initial_count ? map.bucket_count : initial_count;
    map.size = 0;
    if (final_count > 1)
        free_native_allocation(map.buckets, map.memory);
}
FixAnimationBackupCleanupContext::FixAnimationBackupCleanupContext(
    client::ClientRuntimeStorage& storage)
    : callbacks(storage.exit_callbacks()),
      allocator(storage.data().native_cell<void*>(0x43467204u)),
      map_{storage.data().native_cell<FixAnimationBackupNode**>(0x4346D6E4u),
           storage.data().native_cell<std::uint32_t>(0x4346D6E8u),
           storage.data().native_cell<std::uint32_t>(0x4346D6ECu), storage.memory()} {
    if (callbacks.entries.size() != 512 || !map_.buckets || !map_.bucket_count ||
        !map_.memory.allocate || !map_.memory.free || !map_.memory.process_heap)
        throw std::invalid_argument(
            "Early animation cleanup requires the canonical initialized source map and process heap");
}
void FixAnimationBackupCleanupContext::require_producer(
    const FixAnimationBackupRuntime& runtime) const {
    const auto& actual = runtime.bindings;
    if (&map_.buckets != &actual.buckets || &map_.bucket_count != &actual.bucket_count ||
        &map_.size != &actual.size || map_.memory.allocate != actual.memory.allocate ||
        map_.memory.free != actual.memory.free ||
        map_.memory.process_heap != actual.memory.process_heap)
        throw std::invalid_argument(
            "Animation capture and early terminal cleanup must share the actual map and heap");
}
void FixAnimationBackupCleanupContext::finalize() {
    cleanup_fix_animation_backups(map_, allocator);
}
void (*FixAnimationBackupCleanupContext::source_finalizer(std::uint32_t address))() {
    if (address != 0x43359C18u)
        throw std::out_of_range("Finalizer is not the recovered animation-backup cleanup entry");
    if (active_cleanup && active_cleanup != this)
        throw std::logic_error("Animation-backup exit callback requires one stable source binding");
    active_cleanup = this;
    return finalize_backup_map;
}

void bind_fix_animation_backup_exit_initializer(client::NativeStartupSources& sources,
                                                FixAnimationBackupCleanupContext& context) {
    sources.bind_initializer({0x433B86B4u, &context, append_backup_map_finalizer});
}
} // namespace skeet::animation
