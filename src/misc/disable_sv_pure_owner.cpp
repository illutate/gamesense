#include "skeet/misc/disable_sv_pure_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define PURE_MEMBER __thiscall
#else
#define PURE_MEMBER
#endif
using OwnerEntry = void(PURE_MEMBER*)(void*);
DisableSvPureOwner** active_slot = nullptr; //address of canonical43475974
void PURE_MEMBER destroy(void*);
void PURE_MEMBER unchanged_startup(void*) {} //4347D93C: RET
void PURE_MEMBER unchanged(void*) {} //433CCE8E: RET0
const std::array<OwnerEntry, 9> lifecycle_table{destroy,   unchanged_startup, unchanged, unchanged,
                                                unchanged, unchanged,         unchanged, unchanged,
                                                unchanged}; //raw4346F534
}

class DisableSvPureOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    DisablePureConfig& configuration;
    DisableSvPureHandler message;

    DisableSvPureOwner(engine::OwnerRegistryBinding lifecycle,
                       engine::OwnerRegistryBinding messages, ConfigRegistries& registries,
                       DisablePureConfig& settings, DisableSvPureOwner*& active)
        : memory(lifecycle.memory), configuration(settings), message(settings) {
        active_slot = &active;
        lifecycle.append(this); //433676A0
        messages.append(&message); //4336F73C
        vtable = lifecycle_table.data(); //4346F534
        message.initialize_vtable(); //43472C74 ->4335C233
        initialize_disable_pure_config(registries, settings); //existing byte preserved
    }
};
namespace {
void PURE_MEMBER destroy(void* object) { //433ECC98 ->433837D0
    *active_slot = nullptr;
    if (!object)
        return;
    auto* owner = static_cast<DisableSvPureOwner*>(object);
    const auto memory = owner->memory;
    owner->~DisableSvPureOwner();
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
#undef PURE_MEMBER
}
std::size_t disable_sv_pure_owner_size() noexcept {
    return sizeof(DisableSvPureOwner);
}
DisableSvPureOwner&
construct_disable_sv_pure_owner(void* storage, engine::OwnerRegistryBinding lifecycle,
                                engine::OwnerRegistryBinding messages, ConfigRegistries& registries,
                                DisablePureConfig& settings, DisableSvPureOwner*& active) {
    return *::new (storage) DisableSvPureOwner(lifecycle, messages, registries, settings, active);
}
const DisablePureConfig& disable_sv_pure_configuration(const DisableSvPureOwner& owner) noexcept {
    return owner.configuration;
}
} //namespace skeet::misc
