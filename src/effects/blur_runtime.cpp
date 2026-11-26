#include "skeet/effects/blur_runtime.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace skeet::effects {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define BLUR_MEMBER __thiscall
#define BLUR_FRAME __fastcall
#else
#define BLUR_MEMBER
#define BLUR_FRAME
#endif
using OwnerEntry = void(BLUR_MEMBER*)(void*);
struct BlurInstance;
struct BlurFrameEndpoint : EffectFrameCallback {
    BlurInstance* instance;
};

void BLUR_MEMBER destroy_instance(void*);
void BLUR_MEMBER release_instance(void*);
void BLUR_MEMBER unchanged_event(void*) {} //433CCE8E is exactly RET.
void BLUR_FRAME render_instance(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                std::uintptr_t);
const std::array<OwnerEntry, 9> owner_table{destroy_instance, unchanged_event,  unchanged_event,
                                            unchanged_event,  release_instance, unchanged_event,
                                            unchanged_event,  unchanged_event,  unchanged_event};
const std::array<EffectFrameEntry, 1> frame_table{render_instance};

void* allocate_instance(engine::JobMemory memory, std::size_t size) { //43387448
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
void free_instance(engine::JobMemory memory, void* object) { //433837D0
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
struct BlurInstance {
    const OwnerEntry* vtable = nullptr;
    BlurFrameEndpoint frame{{nullptr}, this};
    BlurEffect** active;
    engine::JobMemory memory;
    alignas(BlurEffect) std::array<std::byte, sizeof(BlurEffect)> state;

    BlurInstance(engine::OwnerRegistryBinding owners, engine::OwnerRegistryBinding callbacks,
                 BlurEffect*& current, BlurBindings bindings)
        : active(&current), memory(owners.memory) {
        owners.append(this); //433676A0, before installing either native table.
        void* endpoint = reinterpret_cast<std::uintptr_t>(this) ==
                                 std::numeric_limits<std::uintptr_t>::max() - 11
                             ? nullptr
                             : &frame;
        engine::append_owner(callbacks.registry, &endpoint, callbacks.memory); //4347E391..E3AB
        vtable = owner_table.data();
        frame.vtable = frame_table.data();
        ::new (state.data()) BlurEffect(bindings);
    }
    BlurEffect& effect() noexcept {
        return *std::launder(reinterpret_cast<BlurEffect*>(state.data()));
    }
    ~BlurInstance() {
        effect().~BlurEffect();
    }
};
void BLUR_MEMBER destroy_instance(void* object) { //433823D9/4332C63C
    auto* instance = static_cast<BlurInstance*>(object);
    const auto memory = instance->memory;
    *instance->active = nullptr;
    instance->~BlurInstance();
    free_instance(memory, instance);
}
void BLUR_MEMBER release_instance(void* object) { //433E2BC0
    static_cast<BlurInstance*>(object)->effect().release_resources();
}
void BLUR_FRAME render_instance(void* object, std::uintptr_t, std::uintptr_t, std::uintptr_t,
                                std::uintptr_t) { //433C8896
    static_cast<BlurFrameEndpoint*>(object)->instance->effect().render();
}
#undef BLUR_MEMBER
#undef BLUR_FRAME
}

void dispatch_effect_frame(const engine::OwnerRegistry& callbacks, std::uintptr_t first,
                           std::uintptr_t second, std::uintptr_t third,
                           std::uintptr_t fourth) { //433B8C46
    const auto entries = callbacks.entries();
    for (auto* object : entries) {
        auto* callback = static_cast<EffectFrameCallback*>(object);
        callback->vtable[0](object, first, second, third, fourth);
    }
}
void initialize_blur(engine::OwnerRegistryBinding owners, engine::OwnerRegistryBinding callbacks,
                     BlurEffect*& active,
                     BlurBindings bindings) { //4347F6DB
    auto* storage = allocate_instance(owners.memory, sizeof(BlurInstance));
    if (!storage) {
        active = nullptr;
        return;
    }
    auto* instance = ::new (storage) BlurInstance(owners, callbacks, active, bindings);
    active = &instance->effect();
}
} // namespace skeet::effects
