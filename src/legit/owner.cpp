#include "skeet/legit/owner.hpp"
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::legit {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define LEGIT_MEMBER __thiscall
#else
#define LEGIT_MEMBER
#endif
using LifecycleEntry = void(LEGIT_MEMBER*)(void*);
void LEGIT_MEMBER destroy_entry(void*);
void LEGIT_MEMBER forward_reset_entry(void*);
void LEGIT_MEMBER reset_entry(void*);
void LEGIT_MEMBER unchanged_entry(void*) {} //433CCE8E
// Exact primary4346FA04. Slot5 forwards to slot7, in this direction.
const std::array<LifecycleEntry, 9> lifecycle_table{
    destroy_entry,       unchanged_entry, unchanged_entry, unchanged_entry, unchanged_entry,
    forward_reset_entry, unchanged_entry, reset_entry,     unchanged_entry};
void clear_low_byte(float& value) noexcept {
    std::memset(&value, 0, 1);
}
}

void initialize_runtime_state(RuntimeState& state) noexcept {
    state.target_offset = {};
    state.profile.profile = 0;
    clear_low_byte(state.profile.maximum_lock_time); // native byte+824
    state.profile.limit_lock_time = false;
    clear_low_byte(state.current_view[0]);
    state.current_view_valid = 0;
    clear_low_byte(state.previous_view[0]);
    state.previous_view_valid = 0;
    for (auto& target : state.targets) {
        target.elapsed = 0;
        target.visible_hitgroups = 0;
        target.point = {};
        // Native+14 is the first byte of velocity.x, not velocity_valid+20.
        clear_low_byte(target.velocity[0]);
        target.velocity_valid = 0;
    }
}

class LifecycleOwner {
public:
    const LifecycleEntry* vtable = nullptr;
    OwnerBindings bindings;
    client::NativeCommandPhaseReceiver command_receiver;

    explicit LifecycleOwner(OwnerBindings context) : bindings(context) {
        bindings.lifecycle.append(this); //434845D7 ->433676A0, primary null
        bindings.command_owners.append(command_receiver, &bindings.commands); //434845DF,+8
        bindings.empty_tick_owners.append(bindings.commands); //434845E7,+10
        vtable = lifecycle_table.data();
        bindings.command_owners.publish(command_receiver, client::CommandOwnerId::legit); //434845FA
        bindings.empty_tick_owners.publish(
            bindings.commands); //43484601, after primary/command tables
        register_settings(bindings.configuration, bindings.settings);
        initialize_runtime_state(bindings.commands.state);
        finish_settings_initialization(bindings.settings);
    }
};

namespace {
void LEGIT_MEMBER destroy_entry(void* object) {
    auto* owner = static_cast<LifecycleOwner*>(object);
    destroy_legit_owner(owner, owner->bindings.active);
}
void LEGIT_MEMBER forward_reset_entry(void* object) { //433B06E3
    auto* owner = static_cast<LifecycleOwner*>(object);
    owner->vtable[7](object);
}
void LEGIT_MEMBER reset_entry(void* object) { //433C8F71
    auto& command = static_cast<LifecycleOwner*>(object)->bindings.commands;
    command.reset();
}
}

std::size_t legit_owner_size() noexcept {
    return sizeof(LifecycleOwner);
}
LifecycleOwner& construct_legit_owner(void* storage, OwnerBindings bindings) {
    if (&bindings.commands.settings.legit != &bindings.settings)
        throw std::invalid_argument("LEGIT constructor requires its canonical command settings");
    return *::new (storage) LifecycleOwner(bindings);
}
LifecycleOwner* create_legit_owner(OwnerBindings bindings) { //4347FEBC
    static_assert(sizeof(LifecycleOwner) <= 0x1310);
    const auto memory = bindings.lifecycle.memory;
    auto* raw = memory.allocate(memory.process_heap, 8, 0x1330);
    if (!raw) {
        bindings.active = nullptr; //4347FED3 / recorded4347FED5 MOV[global],EAX
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    constexpr std::uint32_t allocation_size = 0x1330;
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    // HeapZeroMemory covered these fields inside the native allocation. The
    // canonical source storage is borrowed by both command interfaces.
    bindings.settings = Settings{};
    bindings.commands.clear_allocated_storage();
    auto& owner = construct_legit_owner(reinterpret_cast<void*>(header + 16), bindings);
    bindings.active = &owner; //mem_int3.h:4347FED5 MOV[4347588C],EAX
    return &owner;
}
void destroy_legit_owner(LifecycleOwner* owner, LifecycleOwner*& active) {
    active = nullptr;
    if (!owner)
        return;
    const auto memory = owner->bindings.lifecycle.memory;
    owner->~LifecycleOwner();
    auto* bytes = reinterpret_cast<std::byte*>(owner);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
void* legit_lifecycle_object(LifecycleOwner& owner) noexcept {
    return &owner;
}
const Settings* legit_owner_settings(const LifecycleOwner* owner) noexcept {
    return owner ? &owner->bindings.settings : nullptr;
}
#undef LEGIT_MEMBER
} // namespace skeet::legit
