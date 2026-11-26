#include "skeet/misc/fov_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define FOV_MEMBER __thiscall
#else
#define FOV_MEMBER
#endif
using OwnerEntry = void(FOV_MEMBER*)(void*);
void FOV_MEMBER destroy(void*);
void FOV_MEMBER unchanged(void*) {} //433CCE8E
void FOV_MEMBER reset(void*);
const std::array<OwnerEntry, 9> owner_table{destroy,   unchanged, unchanged, unchanged, reset,
                                            unchanged, unchanged, unchanged, unchanged}; //43471E7C
}
class FovOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    ZoomSensitivityRuntime sensitivity;
    effects::ViewFovRuntime view;
    effects::FovViewObserver update;
    FovOwner(engine::OwnerRegistryBinding lifecycle, engine::OwnerRegistryBinding updates,
             ConfigRegistries& registries, FovConfig& config, FovOwnerBindings bindings)
        : memory(lifecycle.memory), sensitivity(config, bindings.sensitivity),
          view{config, bindings.entity_list, &bindings.sensitivity.local_player,
               bindings.view_offsets},
          update(sensitivity, view) {
        lifecycle.append(this);
        network::register_update_observer(updates, update.endpoint);
        vtable = owner_table.data();
        update.initialize_vtable();
        initialize_fov_config(registries, config);
    }
};
namespace {
void FOV_MEMBER destroy(void* object) { //433B1A7A tail433837D0
    if (!object)
        return;
    auto* owner = static_cast<FovOwner*>(object);
    const auto memory = owner->memory;
    owner->~FovOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void FOV_MEMBER reset(void* object) {
    static_cast<FovOwner*>(object)->sensitivity.reset(); //43416A53
}
#undef FOV_MEMBER
}
std::size_t fov_owner_size() noexcept {
    return sizeof(FovOwner);
}
FovOwner& construct_fov_owner(void* storage, engine::OwnerRegistryBinding lifecycle,
                              engine::OwnerRegistryBinding updates, ConfigRegistries& registries,
                              FovConfig& config, FovOwnerBindings bindings) {
    return *::new (storage) FovOwner(lifecycle, updates, registries, config, bindings);
}
ZoomSensitivityRuntime& fov_sensitivity_runtime(FovOwner& owner) noexcept {
    return owner.sensitivity;
}
effects::ViewFovRuntime& fov_view_runtime(FovOwner& owner) noexcept {
    return owner.view;
}
} //namespace skeet::misc
