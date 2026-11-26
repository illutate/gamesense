#include "skeet/misc/console_output_owner.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::misc {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define CONSOLE_MEMBER __thiscall
#else
#define CONSOLE_MEMBER
#endif
using OwnerEntry = void(CONSOLE_MEMBER*)(void*);
// This points to the canonical434758CC slot. Keeping its address also
// preserves4332503F's unconditional clear when the receiver itself is null.
ColoredLogRing** active_slot = nullptr;
void CONSOLE_MEMBER destroy(void*);
void CONSOLE_MEMBER unchanged(void*) {} //433CCE8E, all eight remaining entries.
const std::array<OwnerEntry, 9> lifecycle_table{destroy,   unchanged, unchanged,
                                                unchanged, unchanged, unchanged,
                                                unchanged, unchanged, unchanged}; //4346F418

void* allocate_owner(engine::JobMemory memory, std::size_t size) { //43387448
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    const auto allocation_size = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &allocation_size, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free_owner(engine::JobMemory memory, void* object) { //433837D0
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}

class ConsoleOutputOwner {
public:
    const OwnerEntry* vtable = nullptr;
    engine::JobMemory memory;
    ColoredLogRing ring;
    ConsoleOutputHandler frame;

    ConsoleOutputOwner(engine::OwnerRegistryBinding lifecycle,
                       engine::OwnerRegistryBinding callbacks, ConfigRegistries& registries,
                       ConsoleOutputConfig& settings, ColoredLogRing*& active,
                       const float& frame_time)
        : memory(lifecycle.memory), frame(settings, ring, frame_time) {
        active_slot = &active;
        lifecycle.append(this); //433676A0
        hud::register_hud_frame(callbacks, &frame); //433D20B5
        vtable = lifecycle_table.data();
        frame.initialize_vtable();
        initialize_console_output_config(registries, settings); //preserves value
        ring.head = 0;
        ring.count = 0;
        for (auto& line : ring.lines)
            line.text.clear(); //eight native UTF16 vectors
        ring.hex = {u'0', u'1', u'2', u'3', u'4', u'5', u'6', u'7',
                    u'8', u'9', u'A', u'B', u'C', u'D', u'E', u'F'};
    }
};
namespace {
void CONSOLE_MEMBER destroy(void* object) { //4332503F
    *active_slot = nullptr;
    if (!object)
        return;
    auto* owner = static_cast<ConsoleOutputOwner*>(object);
    const auto memory = owner->memory;
    owner->~ConsoleOutputOwner(); //43369B05: destroy all eight UTF16 strings
    free_owner(memory, object);
}
#undef CONSOLE_MEMBER
}
std::size_t console_output_owner_size() noexcept {
    return sizeof(ConsoleOutputOwner);
}
ConsoleOutputOwner&
construct_console_output_owner(void* storage, engine::OwnerRegistryBinding lifecycle,
                               engine::OwnerRegistryBinding callbacks, ConfigRegistries& registries,
                               ConsoleOutputConfig& settings, ColoredLogRing*& active,
                               const float& frame_time) {
    auto* owner = ::new (storage)
        ConsoleOutputOwner(lifecycle, callbacks, registries, settings, active, frame_time);
    active = &owner->ring;
    return *owner;
}
ColoredLogRing& console_output_ring(ConsoleOutputOwner& owner) noexcept {
    return owner.ring;
}
ColoredLogRing* initialize_console_output_owner(engine::OwnerRegistryBinding lifecycle,
                                                engine::OwnerRegistryBinding callbacks,
                                                ConfigRegistries& registries,
                                                ConsoleOutputConfig& settings,
                                                ColoredLogRing*& active,
                                                const float& frame_time) { //43484E4E
    active_slot = &active;
    auto* storage = allocate_owner(lifecycle.memory, sizeof(ConsoleOutputOwner));
    if (!storage) {
        active = nullptr;
        return nullptr;
    } //43484E65..E6C, restored-source path
    construct_console_output_owner(storage, lifecycle, callbacks, registries, settings, active,
                                   frame_time);
    return active;
}
} //namespace skeet::misc
