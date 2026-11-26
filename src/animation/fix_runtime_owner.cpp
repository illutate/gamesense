#include "skeet/animation/fix_runtime_owner.hpp"
#include "skeet/animation/fix_runtime_bindings.hpp"
#include "skeet/animation/fix_animation_backup_cleanup.hpp"
#include "skeet/animation/runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/effects/player_opacity.hpp"
#include "skeet/engine/hook_runtime_owner.hpp"
#include <stdexcept>

namespace skeet::animation {
namespace {
client::ClientRuntimeStorage& checked_hooks(RuntimeOwner& bones, engine::HookRuntimeOwner& hooks) {
    auto& storage = bones.storage();
    const auto memory = storage.memory();
    if (&hooks.storage() != &storage || &hooks.registry().sentinel != &hooks.unload_state().hooks ||
        hooks.registry().memory.allocate != memory.allocate ||
        hooks.registry().memory.free != memory.free ||
        hooks.registry().memory.process_heap != memory.process_heap)
        throw std::invalid_argument(
            "Fix animation hooks must share the actual canonical hook list and process heap");
    return storage;
}
}
struct FixRuntimeOwner::Data {
    RuntimeOwner& bones;
    client::ClientRuntimeStorage& storage;
    client::PlayerDataUpdateOffsets data_update_fields;
    FixAnimationsBindings animation_bindings;
    FixAnimationsRuntime animations;
    FixAnimationBackupBindings backup_bindings;
    FixAnimationBackupDetachBindings detach_bindings;
    FixAnimationBackupRuntime backup;
    FixAnimationBackupCleanupContext& cleanup;

    Data(RuntimeOwner& bone_owner, client::ConfigurationRuntimeOwner& settings,
         engine::HookRuntimeOwner& hooks, const effects::PlayerOpacityBindings& opacity,
         FixAnimationBackupCleanupContext& early_cleanup)
        : bones(bone_owner), storage(checked_hooks(bones, hooks)),
          data_update_fields(client::native_player_data_update_offsets(storage.offsets().globals)),
          animation_bindings(
              native_fix_animations_bindings(bones, settings, data_update_fields, opacity)),
          animations(animation_bindings),
          backup_bindings(native_fix_animation_backup_bindings(bones, animation_bindings)),
          detach_bindings{hooks.registry(),
                          storage.data().native_cell<std::ptrdiff_t>(0x43479CE4u),
                          animation_bindings.player_settings,
                          {storage.data().native_cell<std::int32_t>(0x4346DB1Cu),
                           storage.data().native_cell<std::uint32_t>(0x4346DB2Cu),
                           storage.data().native_cell<std::uint32_t>(0x4346DB30u),
                           storage.data().native_cell<std::uint32_t>(0x4346DB38u)}},
          backup(backup_bindings, detach_bindings), cleanup(early_cleanup) {
        if (&cleanup.callbacks != &storage.exit_callbacks() ||
            &cleanup.allocator != &storage.data().native_cell<void*>(0x43467204u))
            throw std::invalid_argument(
                "Fix runtime requires the canonical early animation cleanup context");
        cleanup.require_producer(backup);
    }
};
FixRuntimeOwner::FixRuntimeOwner(RuntimeOwner& bones, client::ConfigurationRuntimeOwner& settings,
                                 engine::HookRuntimeOwner& hooks,
                                 const effects::PlayerOpacityBindings& opacity,
                                 FixAnimationBackupCleanupContext& cleanup)
    : data_(std::make_unique<Data>(bones, settings, hooks, opacity, cleanup)) {}
FixRuntimeOwner::~FixRuntimeOwner() = default;
client::ClientRuntimeStorage& FixRuntimeOwner::storage() const noexcept {
    return data_->storage;
}
RuntimeOwner& FixRuntimeOwner::bones_owner() const noexcept {
    return data_->bones;
}
FixAnimationsRuntime& FixRuntimeOwner::animations() noexcept {
    return data_->animations;
}
FixAnimationBackupRuntime& FixRuntimeOwner::backup() noexcept {
    return data_->backup;
}
FixAnimationBackupCleanupContext& FixRuntimeOwner::cleanup() noexcept {
    return data_->cleanup;
}
void FixRuntimeOwner::bind_initializers(client::NativeStartupSources& sources) {
    bind_fix_animation_backup_exit_initializer(sources, data_->cleanup);
}
} //namespace skeet::animation
