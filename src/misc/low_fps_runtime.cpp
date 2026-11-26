#include "skeet/misc/low_fps_runtime.hpp"
#include "skeet/hud/composite_handlers.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::misc {
namespace {
std::uint32_t startup_word(const void* record) {
    std::uint32_t value;
    std::memcpy(&value, static_cast<const std::byte*>(record) + 4, sizeof value);
    return value;
}
#if defined(_M_IX86) || defined(__i386__)
#define LOW_FPS_MEMBER __thiscall
#else
#define LOW_FPS_MEMBER
#endif
using LifecycleEntry = void(LOW_FPS_MEMBER*)(void*);
void LOW_FPS_MEMBER destroy_owner(void*);
void LOW_FPS_MEMBER bind_owner(void*);
void LOW_FPS_MEMBER reset_owner(void*);
void LOW_FPS_MEMBER unchanged_owner(void*) {} //433CCE8E is RET.
const std::array<LifecycleEntry, 9> lifecycle_table{
    destroy_owner, bind_owner,      unchanged_owner, unchanged_owner, unchanged_owner,
    reset_owner,   unchanged_owner, reset_owner,     unchanged_owner};

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

class LowFpsLifecycleOwner {
public:
    const LifecycleEntry* vtable = nullptr;
    engine::JobMemory memory;
    LowFpsRuntime** active;
    LowFpsRuntime runtime;
    client::NativeCommandPhaseReceiver command_receiver{};
    alignas(hud::PerformanceHudHandler)
        std::array<std::byte, sizeof(hud::PerformanceHudHandler)> hud_storage;

    LowFpsLifecycleOwner(engine::OwnerRegistryBinding lifecycle,
                         client::CommandPhaseRegistry& commands,
                         engine::OwnerRegistryBinding hud_owners, ConfigRegistries& registry,
                         rage::PerformanceSettings& settings, const rage::GlobalSettings& rage,
                         LowFpsBindings bindings, script::Bridge& scripts, const InputBitmap& input,
                         const esp::IndicatorLayout& layout, const std::uint32_t& life_offset,
                         LowFpsRuntime*& current)
        : memory(lifecycle.memory), active(&current), runtime(settings, rage, bindings) {
        lifecycle.append(this); //4347E6FF ->433676A0
        commands.append(command_receiver, &runtime); //4347E70C ->43345EBC, secondary+8
        auto* const callback = ::new (hud_storage.data()) hud::PerformanceHudHandler(
            make_low_fps_hud_binding(runtime, scripts, input, layout, life_offset));
        hud::register_hud_frame(hud_owners, callback); //4347E719 ->433D20B5
        vtable = lifecycle_table.data(); //4347E721, table4346EF74
        commands.publish(command_receiver, client::CommandOwnerId::owner_43472148); //4347E72A
        callback->publish(); //4347E730, table4346F010
        skeet::rage::initialize_performance_settings(registry, settings);
        runtime.initialize();
    }
    ~LowFpsLifecycleOwner() {
        std::destroy_at(
            std::launder(reinterpret_cast<hud::PerformanceHudHandler*>(hud_storage.data())));
    }
};

namespace {
void LOW_FPS_MEMBER destroy_owner(void* object) { //433B1A7A ->433837D0
    auto* owner = static_cast<LowFpsLifecycleOwner*>(object);
    const auto memory = owner->memory;
    *owner->active = nullptr;
    owner->~LowFpsLifecycleOwner();
    free_owner(memory, owner);
}
void LOW_FPS_MEMBER bind_owner(void* object) {
    static_cast<LowFpsLifecycleOwner*>(object)->runtime.bind_dependencies();
}
void LOW_FPS_MEMBER reset_owner(void* object) {
    static_cast<LowFpsLifecycleOwner*>(object)->runtime.reset();
}
#undef LOW_FPS_MEMBER
}

LowFpsClock native_low_fps_clock() {
    const auto library = GetModuleHandleW(L"ntdll.dll");
    LowFpsClock result{
        reinterpret_cast<LowFpsClockQuery>(GetProcAddress(library, "RtlQueryPerformanceCounter")),
        reinterpret_cast<LowFpsClockQuery>(
            GetProcAddress(library, "RtlQueryPerformanceFrequency"))};
    if (!result.counter || !result.frequency)
        throw std::logic_error("Low FPS requires the native ntdll performance clock");
    return result;
}

LowFpsRuntime::LowFpsRuntime(rage::PerformanceSettings& settings,
                             const rage::GlobalSettings& global, LowFpsBindings engine)
    : config(settings), rage(global), bindings(engine) {}

void LowFpsRuntime::initialize() {
    state.level = 0.0f;
    state.rage_enabled = nullptr;
    state.low_fps = 0;
    std::int64_t frequency = 0;
    bindings.clock.frequency(&frequency); //native return value is ignored
    state.counter_frequency = static_cast<double>(frequency); //433EC47E, signed64
}

void LowFpsRuntime::bind_dependencies() {
    //4347E0FB resolves23060EC5 and takes its+10 value. This is that same live
    //descriptor, supplied by the typed aggregate instead of a second lookup.
    state.rage_enabled = &rage.enabled.value;
    if (bindings.startup_first)
        state.startup_first_word = startup_word(bindings.startup_first);
    if (bindings.startup_second)
        state.startup_second_word = startup_word(bindings.startup_second);
}

void LowFpsRuntime::reset() noexcept {
    state.level = 0.0f;
    state.frame_number = -1;
    state.low_fps = 0;
    state.accumulated_seconds = 0.0;
}

bool LowFpsRuntime::active(InputBitmap input) const {
    return config.mitigations.value != 0 && *state.rage_enabled != 0 &&
           hotkey_active(rage.activation.value.binding, input);
}

void LowFpsRuntime::phase1(InputBitmap input) {
    if (!active(input))
        return;
    std::int64_t counter = 0;
    bindings.clock.counter(&counter);
    state.started_counter = static_cast<std::uint64_t>(counter);
}

void LowFpsRuntime::phase4(client::CommandFrame& frame) {
    std::uint32_t mitigations = 0;
    if (state.level > 0.10000000149011612f) {
        const auto selected = static_cast<std::uint32_t>(config.mitigations.value);
        if (selected & 0x80u) {
            frame.flags |= 0x20000u;
            mitigations = 0x80u;
        }
        mitigations |= selected & 1u;
        if (state.level > 0.30000001192092896f)
            mitigations |= selected & 0x1eu;
    }
    frame.field_1a0 = mitigations;
    state.low_fps = mitigations != 0;
}

void LowFpsRuntime::phase7(const client::CommandFrame& frame, InputBitmap input) {
    if (!active(input)) {
        reset();
        return;
    }
    const auto x = frame.origin_10c[0] - state.previous_origin[0];
    const auto y = frame.origin_10c[1] - state.previous_origin[1];
    const auto z = frame.origin_10c[2] - state.previous_origin[2];
    if ((y * y + x * x) + z * z > 64.0f) {
        const auto decay = bindings.tick_interval * 0.33000001311302185f;
        state.level = maximum_number(state.level - decay, 0.0f);
    }
    if (bindings.frame_number != state.frame_number) {
        state.accumulated_seconds = 0.0;
        state.frame_number = bindings.frame_number;
    }
    std::int64_t counter = 0;
    bindings.clock.counter(&counter);
    const auto elapsed = static_cast<std::uint64_t>(counter) - state.started_counter;
    state.accumulated_seconds =
        static_cast<double>(elapsed) / state.counter_frequency +
        state.accumulated_seconds; //433EC43E converts the wrapped unsigned64 delta
    if (state.accumulated_seconds >= static_cast<double>(bindings.tick_interval)) {
        state.level = minimum_number(state.level + 0.33000001311302185f, 1.0f);
        state.previous_origin = frame.origin_10c;
    }
}

hud::PerformanceHudBinding make_low_fps_hud_binding(LowFpsRuntime& runtime, script::Bridge& scripts,
                                                    const InputBitmap& input,
                                                    const esp::IndicatorLayout& layout,
                                                    const std::uint32_t& life_offset) {
    return {scripts,
            runtime.config,
            runtime.state.low_fps,
            runtime.state.rage_enabled,
            runtime.rage,
            input,
            layout,
            life_offset};
}

LowFpsLifecycleOwner* initialize_low_fps_owner(
    engine::OwnerRegistryBinding lifecycle, client::CommandPhaseRegistry& commands,
    engine::OwnerRegistryBinding hud_owners, ConfigRegistries& registry,
    rage::PerformanceSettings& settings, const rage::GlobalSettings& rage, LowFpsBindings bindings,
    script::Bridge& scripts, const InputBitmap& input, const esp::IndicatorLayout& layout,
    const std::uint32_t& life_offset, LowFpsRuntime*& active) {
    auto* storage =
        allocate_owner(lifecycle.memory, std::max(std::size_t(0x78), sizeof(LowFpsLifecycleOwner)));
    if (!storage) {
        // The original allocation-failure edge reaches unresolved INT1; source allocation failure terminates this factory with bad_alloc.
        throw std::bad_alloc{};
    }
    auto* owner = ::new (storage)
        LowFpsLifecycleOwner(lifecycle, commands, hud_owners, registry, settings, rage, bindings,
                             scripts, input, layout, life_offset, active);
    active = &owner->runtime;
    return owner;
}
} // namespace skeet::misc
