#include "skeet/client/buffered_shot_lifecycle.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/engine/native_owner_registry_storage.hpp"
#include "skeet/engine/unload.hpp"
#include <array>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define BUFFERED_SHOT_MEMBER __thiscall
#else
#define BUFFERED_SHOT_MEMBER
#endif
using PrimaryEntry = void(BUFFERED_SHOT_MEMBER*)(void*);
void BUFFERED_SHOT_MEMBER destruction_entry(void*);
void BUFFERED_SHOT_MEMBER forward_reset_entry(void*);
void BUFFERED_SHOT_MEMBER reset_entry(void*);
void BUFFERED_SHOT_MEMBER unchanged_entry(void*) {} //actual433CCE8E RET

// Exact primary4346FC00. Slot5 forwards to the currently installed slot7.
const std::array<PrimaryEntry, 9> primary_table{
    destruction_entry,   unchanged_entry, unchanged_entry, unchanged_entry, unchanged_entry,
    forward_reset_entry, unchanged_entry, reset_entry,     unchanged_entry};
}

class BufferedShotLifecycleOwner {
public:
    const PrimaryEntry* vtable = nullptr;
    NativeCommandPhaseReceiver command_receiver;
    BufferedShotLifecycleBindings bindings;

    explicit BufferedShotLifecycleOwner(BufferedShotLifecycleBindings source) : bindings(source) {
        bindings.primary.append(this); //4347FE5A ->433676A0, primary still null
        bindings.commands.append(command_receiver, &bindings.command); //4347FE62
        vtable = primary_table.data(); //4347FE69, primary4346FC00
        bindings.command.state.diagnostics.count = 0; //4347FE72, before the secondary table write
        bindings.commands.publish(command_receiver, CommandOwnerId::owner_4346f01c); //4347FE76
        // Reuse sparse4347FE72..FEA4 construction on the existing state: count,
        // six handle sentinels and all150 sparse43366497 frame constructors.
        ::new (static_cast<void*>(&bindings.command.state)) BufferedShotCommandStorage;
    }
};

namespace {
void BUFFERED_SHOT_MEMBER destruction_entry(void* object) {
    auto* owner = static_cast<BufferedShotLifecycleOwner*>(object);
    destroy_buffered_shot_lifecycle(owner, owner->bindings.active);
}
void BUFFERED_SHOT_MEMBER forward_reset_entry(void* object) {
    refresh_buffered_shot_lifecycle(*static_cast<BufferedShotLifecycleOwner*>(object));
}
void BUFFERED_SHOT_MEMBER reset_entry(void* object) {
    reset_buffered_shot_lifecycle(*static_cast<BufferedShotLifecycleOwner*>(object));
}
void factory_entry(void* context) {
    const auto& source = *static_cast<BufferedShotFactoryBindings*>(context);
    create_buffered_shot_owner(source.lifecycle);
}
}

std::size_t buffered_shot_lifecycle_size() noexcept {
    return sizeof(BufferedShotLifecycleOwner);
}

BufferedShotLifecycleOwner&
construct_buffered_shot_lifecycle(void* storage, BufferedShotLifecycleBindings bindings) {
    return *::new (storage) BufferedShotLifecycleOwner(bindings);
}

BufferedShotLifecycleOwner* create_buffered_shot_owner(BufferedShotLifecycleBindings bindings) {
    static_assert(sizeof(BufferedShotCommandStorage) == buffered_shot_native_size);
    static_assert(sizeof(BufferedShotLifecycleOwner) <= buffered_shot_native_size);
    const auto memory = bindings.primary.memory;
    constexpr auto allocation_size = buffered_shot_native_size + 0x20u;
    auto* const raw = memory.allocate(memory.process_heap, 8, allocation_size);
    if (!raw) {
        //43485BFA XOR EAX,EAX followed by the recorded43485BFC MOV.
        bindings.active = nullptr;
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15u) & ~std::uintptr_t(15u);
    const auto displacement = static_cast<std::uint32_t>(header - address);
    const auto size = static_cast<std::uint32_t>(allocation_size);
    std::memcpy(reinterpret_cast<void*>(header + 8u), &displacement, sizeof displacement);
    std::memcpy(reinterpret_cast<void*>(header + 12u), &size, sizeof size);

    // The portable lifecycle borrows the native diagnostics/frame projection.
    // HEAP_ZERO_MEMORY covered that whole126B8 state before the constructor;
    // on failure the borrowed state is left unchanged, as in the native path.
    std::memset(&bindings.command.state, 0, sizeof bindings.command.state);
    auto& owner =
        construct_buffered_shot_lifecycle(reinterpret_cast<void*>(header + 16u), bindings);
    //mem_int3.h:560 records this MOV at43485BFC with size5; execution then
    //continues at the unmodified43485C01 RET. No saved-return substitution.
    bindings.active = &owner;
    return &owner;
}

void destroy_buffered_shot_lifecycle(BufferedShotLifecycleOwner* owner,
                                     BufferedShotLifecycleOwner*& active) {
    active = nullptr; //4334D8FE, before433837D0's null guard
    if (!owner)
        return;
    const auto memory = owner->bindings.primary.memory;
    owner->~BufferedShotLifecycleOwner();
    engine::UnloadSystemFunctions system;
    system.free_heap = memory.free;
    system.process_heap = memory.process_heap;
    engine::free_native_allocation(owner, system);
}

void reset_buffered_shot_lifecycle(BufferedShotLifecycleOwner& owner) noexcept {
    auto& state = owner.bindings.command.state;
    state.diagnostics.count = 0;
    for (auto& frame : state.frames)
        frame.command_number_ac = -1;
}

void refresh_buffered_shot_lifecycle(BufferedShotLifecycleOwner& owner) {
    owner.vtable[7](&owner);
}
void* buffered_shot_lifecycle_object(BufferedShotLifecycleOwner& owner) noexcept {
    return &owner;
}
BufferedShotCommandOwner* buffered_shot_command_owner(BufferedShotLifecycleOwner* owner) noexcept {
    return owner ? &owner->bindings.command : nullptr;
}
BufferedShotCommandStorage*
buffered_shot_command_storage(BufferedShotLifecycleOwner* owner) noexcept {
    const auto* command = buffered_shot_command_owner(owner);
    return command ? &command->state : nullptr;
}
rage::ShotDiagnosticHistory* buffered_shot_diagnostics(BufferedShotLifecycleOwner* owner) noexcept {
    auto* state = buffered_shot_command_storage(owner);
    return state ? &state->diagnostics : nullptr;
}

void bind_buffered_shot_factory(NativeStartupSources& sources,
                                BufferedShotFactoryBindings& source) {
    const auto& image = sources.data();
    if (&source.registries.image() != &image)
        throw std::invalid_argument(
            "Buffered-shot factory must share the startup initialization image");
    if (&source.lifecycle.primary.registry != &source.registries.at(0x43475B78u))
        throw std::invalid_argument(
            "Buffered-shot factory requires the canonical43475B78 lifecycle registry");
    if (&source.lifecycle.active != &image.native_cell<BufferedShotLifecycleOwner*>(0x43475968u))
        throw std::invalid_argument(
            "Buffered-shot factory requires the canonical43475968 full-owner slot");
    sources.bind_initializer({buffered_shot_factory, &source, factory_entry});
}

#undef BUFFERED_SHOT_MEMBER
}
