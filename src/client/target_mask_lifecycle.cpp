#include "skeet/client/target_mask_lifecycle.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/hud/composite_runtime.hpp"
#include "skeet/misc/network_console_handlers.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define TARGET_MASK_MEMBER __thiscall
#define TARGET_MASK_FAST __fastcall
#else
#define TARGET_MASK_MEMBER
#define TARGET_MASK_FAST
#endif
using PrimaryEntry = void(TARGET_MASK_MEMBER*)(void*);
using HudSlotOne = bool(TARGET_MASK_FAST*)(void*, ui::RenderQueue*, void*, std::int32_t*);
using HudSlotTwo = std::int32_t(TARGET_MASK_MEMBER*)(void*);
struct TargetMaskHudTable {
    hud::HudFrameEntry draw;
    HudSlotOne slot_one;
    HudSlotTwo slot_two;
};
static_assert(offsetof(TargetMaskHudTable, slot_one) == sizeof(hud::HudFrameEntry));
static_assert(offsetof(TargetMaskHudTable, slot_two) == sizeof(hud::HudFrameEntry) * 2);
struct TargetMaskMessage : misc::NetworkMessageCallback {
    TargetMaskOwner* owner;
};

void TARGET_MASK_MEMBER destruction_entry(void*);
void TARGET_MASK_MEMBER reset_entry(void*);
void TARGET_MASK_MEMBER unchanged_entry(void*) {} //433CCE8E
void TARGET_MASK_FAST message_entry(void*, misc::NetworkMessageCall*);
void TARGET_MASK_FAST hud_draw(void*, ui::RenderQueue*, void*, std::int32_t*) {} //4334A269 RET8
bool TARGET_MASK_FAST hud_slot_one(void*, ui::RenderQueue*, void*, std::int32_t*) {
    return false;
} //43382057
std::int32_t TARGET_MASK_MEMBER hud_slot_two(void*) {
    return 1;
} //433686FC

const std::array<PrimaryEntry, 9> primary_table{
    destruction_entry, unchanged_entry, unchanged_entry, unchanged_entry, unchanged_entry,
    unchanged_entry,   reset_entry,     unchanged_entry, unchanged_entry}; //43470114
const std::array<misc::NetworkMessageEntry, 1> message_table{message_entry}; //4346F334
const TargetMaskHudTable hud_table{hud_draw, hud_slot_one, hud_slot_two}; //434701CC

void require_canonical_descriptor(const TargetMaskFactoryBindings& bindings) {
    const auto& data = engine::require_native_data_image(bindings.data);
    if (&bindings.registries.commands.image() != &data ||
        &bindings.registries.entities.image() != &data)
        throw std::invalid_argument(
            "Target-mask lifecycle requires the canonical native data owner");
    if (&bindings.command.enabled != &bindings.settings.enabled.value)
        throw std::invalid_argument(
            "Target-mask lifecycle requires the canonical CRT descriptor347A4FA6");
}
void* allocate(engine::JobMemory memory) {
    constexpr auto total = std::uint32_t(target_mask_native_size + 0x20u);
    auto* const raw = memory.allocate(memory.process_heap, 8, total);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15u) & ~std::uintptr_t(15u);
    const auto displacement = std::uint32_t(header - address);
    std::memcpy(reinterpret_cast<void*>(header + 8u), &displacement, sizeof displacement);
    std::memcpy(reinterpret_cast<void*>(header + 12u), &total, sizeof total);
    return reinterpret_cast<void*>(header + 16u);
}
void release(engine::JobMemory memory, void* storage) {
    auto* const bytes = static_cast<std::byte*>(storage);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}

class TargetMaskLifecycleOwner {
public:
    const PrimaryEntry* vtable = nullptr;
    NativeCommandPhaseReceiver command_receiver;
    NativeEntityObserverReceiver entity_receiver;
    TargetMaskMessage message{{nullptr}, nullptr};
    hud::HudFrameCallback hud;
    TargetMaskFactoryBindings bindings;

    explicit TargetMaskLifecycleOwner(const TargetMaskFactoryBindings& source) : bindings(source) {
        auto& registries = bindings.registries;
        auto& command = bindings.command;
        registries.primary.append(this); //434806D8 ->433676A0, primary still zero
        registries.commands.append(command_receiver, &command); //434806E0
        message.owner = &command;
        message.vtable = nullptr; //434806E8, before the actual message append
        registries.messages.append(&message); //434806ED ->4336F73C
        registries.entities.append_target_mask(entity_receiver, command); //434806F5
        hud::register_hud_frame(registries.hud, &hud); //434806FD ->433D20B5

        message.vtable = message_table.data(); //43480704, native4346F334
        registries.commands.publish(command_receiver, CommandOwnerId::owner_43470478); //4348070C
        registries.entities.publish_target_mask(entity_receiver); //43480713
        hud.vtable = &hud_table.draw; //4348071F, all three native slots retained
        vtable = primary_table.data(); //43480726

        auto& state = command.state;
        state.accepted_tick = std::bit_cast<std::int32_t>(std::uint32_t(state.accepted_tick) &
                                                          0xFFFFFF00u); //4348072C
        state.has_accepted_tick = 0; //4348072F
        state.attack_pending = 0; //43480732
        //mem_int3.h's explicit43480735 record is MOV EAX,[43467A78].
        // Read that live cell only; this constructor does not invoke a VM.
        const auto variable = engine::require_native_data_image(bindings.data)
                                  .native_cell<std::uint32_t>(0x43467A78u) ^
                              0x3AE06513u;
        state.first = 0; //4348073F
        command.bindings.lag_compensation =
            reinterpret_cast<const void*>(std::uintptr_t(variable)); //43480742
        state.count = 0; //43480748
        for (auto& record : state.records) { //4348074B..43480756
            record.targets[0] = 0;
            record.targets[1] = 0;
        }
    }
};
static_assert(sizeof(TargetMaskLifecycleOwner) <= target_mask_native_size);

namespace {
void TARGET_MASK_MEMBER destruction_entry(void* object) {
    destroy_target_mask_lifecycle(static_cast<TargetMaskLifecycleOwner*>(object));
}
void TARGET_MASK_MEMBER reset_entry(void* object) {
    reset_target_mask_lifecycle(*static_cast<TargetMaskLifecycleOwner*>(object));
}
void TARGET_MASK_FAST message_entry(void* object, misc::NetworkMessageCall* call) {
    auto& endpoint =
        *static_cast<TargetMaskMessage*>(static_cast<misc::NetworkMessageCallback*>(object));
    process_target_mask_message(*endpoint.owner, call->message);
}
void factory_entry(void* context) {
    create_target_mask_lifecycle(*static_cast<TargetMaskFactoryBindings*>(context));
}
}

std::size_t target_mask_lifecycle_size() noexcept {
    return sizeof(TargetMaskLifecycleOwner);
}
TargetMaskLifecycleOwner&
construct_target_mask_lifecycle(void* storage, const TargetMaskFactoryBindings& bindings) {
    require_canonical_descriptor(bindings);
    return *::new (storage) TargetMaskLifecycleOwner(bindings);
}
TargetMaskLifecycleOwner* create_target_mask_lifecycle(const TargetMaskFactoryBindings& bindings) {
    require_canonical_descriptor(bindings);
    auto* const storage = allocate(bindings.registries.primary.memory); //4348066A..43480674
    if (!storage)
        throw std::runtime_error("Unrecovered native target-mask allocation-failure edge4348067F");
    //43387448 zeroed the native owner, while the source ring is the existing
    //borrowed canonical state. Project the same zeros only after allocation.
    bindings.command.state = TargetMaskState{};
    return &construct_target_mask_lifecycle(storage, bindings); //43480678/7A tail-jump
}
void destroy_target_mask_lifecycle(TargetMaskLifecycleOwner* owner) {
    if (!owner)
        return; //433837D0's only null guard
    const auto memory = owner->bindings.registries.primary.memory;
    owner->~TargetMaskLifecycleOwner();
    release(memory, owner);
}
void reset_target_mask_lifecycle(TargetMaskLifecycleOwner& owner) noexcept {
    reset_target_mask_owner(owner.bindings.command);
}
void* target_mask_lifecycle_object(TargetMaskLifecycleOwner& owner) noexcept {
    return &owner;
}
TargetMaskOwner& target_mask_command_owner(TargetMaskLifecycleOwner& owner) noexcept {
    return owner.bindings.command;
}
void bind_native_target_mask_factory(NativeStartupSources& sources,
                                     TargetMaskFactoryBindings& bindings) {
    require_canonical_descriptor(bindings);
    if (bindings.data.get() != &sources.data())
        throw std::invalid_argument("Target-mask factory must share the startup native data owner");
    sources.bind_initializer({target_mask_factory, &bindings, factory_entry});
}

#undef TARGET_MASK_MEMBER
#undef TARGET_MASK_FAST
} // namespace skeet::client
