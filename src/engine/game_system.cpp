#include "skeet/engine/game_system.hpp"
#include "skeet/client/shutdown.hpp"
#include "skeet/effects/view_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/network/update_observers.hpp"
#include "skeet/misc/config.hpp"
#include "game_system_detail.hpp"
#include <atomic>
#include <bit>
#include <cstring>
#include <emmintrin.h>
#include <new>

namespace skeet::engine {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define SYSTEM_MEMBER __thiscall
#else
#define SYSTEM_MEMBER
#endif
using namespace game_system_detail;
GameSystemRuntime& runtime(void* endpoint) {
    return *owner_of<GameSystemOwner>(static_cast<GameSystemEndpoint*>(endpoint),
                                      offsetof(GameSystemOwner, endpoint))
                .runtime;
}
void SYSTEM_MEMBER empty(void*) {} //433CCE8E
bool SYSTEM_MEMBER false0(void*) {
    return false;
} //43408189
bool SYSTEM_MEMBER true0(void*) {
    return true;
} //433422C6
bool SYSTEM_MEMBER false1(void*, std::uintptr_t) {
    return false;
} //43336939, RET4
bool SYSTEM_MEMBER true1(void*, std::uintptr_t) {
    return true;
} //4334C25E, RET4
void SYSTEM_MEMBER empty2(void*, std::uintptr_t, std::uintptr_t) {} //4334A269, RET8
bool SYSTEM_MEMBER false2(void*, std::uintptr_t, std::uintptr_t) {
    return false;
} //43382057
bool SYSTEM_MEMBER true3(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t) {
    return true;
} //433ADABB
std::uintptr_t SYSTEM_MEMBER identity(void*, std::uintptr_t value) {
    return value;
} //433E9F3B
std::uintptr_t SYSTEM_MEMBER zero1(void*, std::uintptr_t) {
    return 0;
} //43418593
std::uint32_t SYSTEM_MEMBER zero0(void*) {
    return 0;
} //43336942
const char* SYSTEM_MEMBER name(void*) {
    static const char empty_name[1] = {0};
    return empty_name;
}
void SYSTEM_MEMBER shutdown(void* endpoint) {
    runtime(endpoint).shutdown.detach();
}
void SYSTEM_MEMBER frame(void* endpoint, std::uint8_t update) {
    auto& state = runtime(endpoint);
    dispatch_frame_update(state.frames, state.frame_counter, update);
}
void SYSTEM_MEMBER owner_phase6(void* endpoint) {
    dispatch_owner_slot_six(runtime(endpoint).shutdown.owners);
}
void SYSTEM_MEMBER owner_phase8(void* endpoint) {
    dispatch_owner_slot_eight(runtime(endpoint).shutdown.owners);
}
void SYSTEM_MEMBER update2(void* endpoint) {
    network::dispatch_update_phase2(runtime(endpoint).updates);
}
void SYSTEM_MEMBER update3(void* endpoint) {
    network::dispatch_update_phase3(runtime(endpoint).updates);
}
void SYSTEM_MEMBER update0(void* endpoint) {
    auto& state = runtime(endpoint);
    network::dispatch_update_phase0(state.updates, state.update_stage);
}
void SYSTEM_MEMBER update1(void* endpoint) {
    auto& state = runtime(endpoint);
    network::dispatch_update_phase1(state.updates, state.update_stage);
}
void SYSTEM_MEMBER update4(void* endpoint) {
    network::dispatch_update_phase4(runtime(endpoint).updates);
}
void SYSTEM_MEMBER camera_capture(void* endpoint, std::uintptr_t, std::uintptr_t,
                                  const std::int32_t* width, const std::int32_t* height) {
    runtime(endpoint).capture_camera(width, height);
}
bool SYSTEM_MEMBER view(void* endpoint, float* origin, float* angles, float* fov) {
    return effects::dispatch_view(runtime(endpoint).updates, origin, angles, fov);
}
void SYSTEM_MEMBER before(void* endpoint, std::int32_t stage) {
    runtime(endpoint).before_stage(stage);
}
void SYSTEM_MEMBER after(void* endpoint, std::int32_t stage) {
    runtime(endpoint).after_stage(stage);
}
void SYSTEM_MEMBER pure_files(void* endpoint) {
    runtime(endpoint).apply_pure_file_override();
}
const GameSystemVtable system_table{name,
                                    false0,
                                    shutdown,
                                    false1,
                                    false1,
                                    empty,
                                    empty,
                                    true1,
                                    empty2,
                                    frame,
                                    {empty, empty, empty, empty, empty, empty, empty, empty},
                                    identity,
                                    zero1,
                                    owner_phase6,
                                    empty,
                                    owner_phase8,
                                    empty,
                                    update2,
                                    update3,
                                    camera_capture,
                                    view,
                                    false1,
                                    true0,
                                    false0,
                                    true0,
                                    false0,
                                    zero1,
                                    empty,
                                    empty,
                                    false2,
                                    true3,
                                    update0,
                                    update1,
                                    empty,
                                    empty,
                                    before,
                                    after,
                                    pure_files,
                                    update4,
                                    zero0};
void SYSTEM_MEMBER destroy_owner(void* object) {
    auto* owner = static_cast<GameSystemOwner*>(object);
    const auto memory = owner->memory;
    owner->~GameSystemOwner();
    release(object, memory);
}
void SYSTEM_MEMBER install_owner(void* object) {
    static_cast<GameSystemOwner*>(object)->install();
}
void SYSTEM_MEMBER remove_owner(void* object) {
    static_cast<GameSystemOwner*>(object)->remove();
}
const std::array<GameSystemEntry<void>, 9> owner_table{
    destroy_owner, empty, empty, install_owner, remove_owner, empty, empty, empty, empty};

std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) +
                                       static_cast<std::uint32_t>(b));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) -
                                       static_cast<std::uint32_t>(b));
}
void grow(GameSystemVector& vector, std::int32_t amount, void* allocator) { //4337CF0C
    if (vector.grow_size < 0)
        return;
    const auto requested = add(vector.capacity, amount);
    std::int32_t capacity;
    if (vector.grow_size) { //433C050D, signed IDIV rounding
        const auto preceding = subtract(requested, 1);
        capacity = add(subtract(preceding, preceding % vector.grow_size), vector.grow_size);
    } else {
        capacity = vector.capacity ? vector.capacity : 8; //(31+native sizeof(pointer))/4
        while (capacity < requested)
            capacity = add(capacity, capacity);
    }
    if (capacity < requested) {
        if (!capacity && requested <= -1)
            capacity = -1;
        else
            do {
                const auto sum = add(capacity, requested);
                capacity = add(sum, sum < 0 ? 1 : 0) >> 1;
            } while (capacity < requested);
    }
    vector.capacity = capacity; //published before the allocator call, including failure
    const auto bytes =
        static_cast<std::uint32_t>(capacity) * std::uint32_t(sizeof(GameSystemEndpoint*));
    vector.data = vector.data ? invoke<GameSystemEndpoint**>(allocator, 3, vector.data, bytes)
                              : invoke<GameSystemEndpoint**>(allocator, 1, bytes);
}
void append(GameSystemVector& vector, GameSystemEndpoint* const* value,
            void* allocator) { //433F4786
    const auto insertion = vector.size;
    auto count = vector.size;
    if (add(insertion, 1) > vector.capacity) {
        grow(vector, add(subtract(insertion, vector.capacity), 1), allocator);
        count = vector.size; //the genuine allocator may reenter the manager
    }
    vector.size = add(count, 1);
    const auto moved = subtract(count, insertion);
    vector.elements = vector.data;
    if (moved > 0)
        std::memmove(vector.data + insertion + 1, vector.data + insertion,
                     static_cast<std::size_t>(moved) * sizeof(GameSystemEndpoint*));
    vector.data[insertion] = *value; //read the caller's pointer after all allocation/copy
}
bool remove(GameSystemVector& vector, GameSystemEndpoint* const* value) { //433B9059
    auto count = vector.size;
    if (count <= 0)
        return false;
    const auto target = *value;
    std::int32_t index = 0;
    while (vector.data[index] != target)
        if (++index >= count)
            return false;
    const auto moved = subtract(subtract(count, index), 1);
    if (moved > 0) {
        std::memmove(vector.data + index, vector.data + index + 1,
                     static_cast<std::size_t>(moved) * sizeof(GameSystemEndpoint*));
        count = vector.size;
    }
    vector.size = subtract(count, 1);
    return true;
}
float multiply(float first, float second) {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(first), _mm_set_ss(second)));
}
}

GameSystemRuntime::GameSystemRuntime(client::ShutdownRuntime& shutdown_runtime,
                                     const OwnerRegistry& update_registry,
                                     const OwnerRegistry& frame_registry, std::int32_t& counter,
                                     std::int32_t& stage, GameSystemBindings native,
                                     GameSystemCameraState state)
    : shutdown(shutdown_runtime), updates(update_registry), frames(frame_registry),
      frame_counter(counter), update_stage(stage), bindings(native), view_state(state) {}

void GameSystemRuntime::capture_camera(const std::int32_t* width, const std::int32_t* height) {
    if (!bindings.view_provider)
        return;
    auto* setup = invoke<void*>(bindings.view_provider, 12, -1);
    if (!setup)
        return;
    auto* engine = bindings.engine;
    // Six MOVSD copies precede the FOV load and the external aspect-ratio call.
    const auto* bytes = static_cast<const std::byte*>(setup);
    for (std::ptrdiff_t i = 0; i < 3; ++i)
        std::memcpy(&view_state.origin[i], bytes + 0xB8 + i * 4, 4);
    for (std::ptrdiff_t i = 0; i < 3; ++i)
        std::memcpy(&view_state.angles[i], bytes + 0xC4 + i * 4, 4);
    std::memcpy(&view_state.field_of_view, bytes + 0xB0, 4);
    const auto table = *static_cast<void***>(engine);
    const auto aspect_entry =
        reinterpret_cast<MemberFunction<float, std::int32_t, std::int32_t>>(table[101]);
    const auto screen_height = *height;
    const auto screen_width = *width;
    const auto aspect = aspect_entry(engine, screen_width, screen_height);
    const auto aspect_scale = static_cast<float>(static_cast<long double>(aspect) * 0.75L);
    auto angle = multiply(field<float>(setup, 0xB0), std::bit_cast<float>(0x3C8EFA35u));
    angle = multiply(angle, 0.5f);
    angle = bindings.tangent(angle);
    angle = multiply(angle, aspect_scale);
    angle = bindings.arctangent(angle);
    angle = multiply(angle, std::bit_cast<float>(0x42652EE0u));
    view_state.aspect_field_of_view =
        _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(angle), _mm_set_ss(angle)));
}
void GameSystemRuntime::capture_projection() {
    auto* matrix = invoke<const float*>(bindings.engine, 37); //433C0939
    if (!matrix)
        return;
    std::int32_t width = 0, height = 0;
    invoke<void>(bindings.engine, 5, &width, &height);
    // Four original MOVUPS loads precede all destination stores.
    std::array<float, 16> snapshot;
    std::memcpy(snapshot.data(), matrix, sizeof snapshot);
    std::memcpy(view_state.projection.matrix.data(), snapshot.data(), sizeof snapshot);
    const auto half = [](std::int32_t dimension) {
        const auto value = _mm_cvtepi32_ps(_mm_cvtsi32_si128(dimension));
        return _mm_cvtss_f32(_mm_mul_ss(value, _mm_set_ss(0.5f)));
    };
    view_state.projection.horizontal_scale = half(width);
    view_state.projection.vertical_scale = half(height);
}
void GameSystemRuntime::before_stage(std::int32_t stage) {
    if (stage == 2)
        capture_projection();
    else if (stage == 1) {
        const auto& patch = shutdown.hooks.state.first_patch;
        if (patch.enabled && patch.target)
            std::atomic_ref<void*>(*patch.target)
                .exchange(reinterpret_cast<void*>(patch.enabled), std::memory_order_seq_cst);
    }
}
void GameSystemRuntime::after_stage(std::int32_t stage) {
    if (stage != 1)
        return;
    const auto& patch = shutdown.hooks.state.first_patch;
    if (patch.enabled && patch.target)
        std::atomic_ref<void*>(*patch.target).exchange(patch.saved, std::memory_order_seq_cst);
}
void GameSystemRuntime::apply_pure_file_override() {
    if (!bindings.disable_pure.enabled.value)
        return;
    auto* object = bindings.filesystem;
    const auto table = *static_cast<void***>(object);
    const auto* instruction = table[128];
    if (!instruction || field<std::uint16_t>(instruction, 0) != 0x818Bu)
        return;
    const auto displacement = field<std::uint32_t>(instruction, 2);
    if (displacement - 0xC0u > 0x5Fu)
        return;
    const std::uint32_t enabled = 1;
    std::memcpy(static_cast<std::byte*>(object) + displacement, &enabled, 4);
}

void GameSystemOwner::install() {
    if (!manager)
        return;
    installed = this;
    saved_index = manager->current;
    auto* value = &endpoint;
    append(manager->systems, &value, *allocator);
    manager->current = subtract(manager->systems.size, 1);
}
void GameSystemOwner::remove() {
    if (!installed || manager->systems.size == 0)
        return;
    manager->current = saved_index;
    auto* value = &endpoint;
    engine::remove(manager->systems, &value);
    // Native leaves installed/saved_index and the unused tail slot untouched.
}
GameSystemOwner* create_game_system_owner(OwnerRegistryBinding owners, GameSystemManager* manager,
                                          void* const* allocator, GameSystemRuntime& state) {
    constexpr auto size = sizeof(GameSystemOwner) > 0x44u ? sizeof(GameSystemOwner) : 0x44u;
    auto* storage = allocate(owners.memory, size);
    if (!storage)
        return nullptr;
    auto* owner = ::new (storage) GameSystemOwner;
    owners.append(owner);
    owner->vtable = owner_table.data();
    owner->manager = manager;
    owner->saved_index = -1;
    owner->installed = nullptr;
    owner->endpoint.vtable = &system_table;
    owner->runtime = &state;
    owner->allocator = allocator;
    owner->memory = owners.memory;
    return owner;
}
#undef SYSTEM_MEMBER
} //namespace skeet::engine
