#include "skeet/misc/network_console_handlers.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include "skeet/font_metrics.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace skeet::misc {
namespace {
NetworkMessageRuntime* active_network_message_runtime = nullptr;
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::size_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::int32_t add(std::int32_t first, std::int32_t second) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(first) + std::uint32_t(second));
}
std::int32_t truncate(float value) noexcept {
    if (!(value >= -2147483648.0f && value < 2147483648.0f))
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
bool contains_payload(const std::byte* source, std::uint32_t length, const std::byte* pattern,
                      std::uint32_t pattern_length) {
    // 433DF260 compares each candidate backwards. Both native callers use a
    // positive pattern length (4 or13), and retain the explicit null guards.
    if (!source || length < pattern_length || !pattern)
        return false;
    const auto maximum = std::numeric_limits<std::uintptr_t>::max() -
                         reinterpret_cast<std::uintptr_t>(source) - pattern_length;
    const auto bounded = std::min<std::uintptr_t>(length, maximum);
    if (bounded < pattern_length)
        return false;
    for (std::uintptr_t offset = 0; offset <= bounded - pattern_length; ++offset) {
        auto index = pattern_length;
        while (index && source[offset + index - 1] == pattern[index - 1])
            --index;
        if (!index)
            return true;
    }
    return false;
}
#if defined(_M_IX86) || defined(__i386__)
void __fastcall disable_pure_entry(void* endpoint, NetworkMessageCall* call) {
#else
void disable_pure_entry(void* endpoint, NetworkMessageCall* call) {
#endif
    apply_disable_sv_pure(static_cast<DisableSvPureHandler*>(endpoint)->config, *call);
}
const std::array<NetworkMessageEntry, 1> disable_pure_table{disable_pure_entry};
#if defined(_M_IX86) || defined(__i386__)
void __fastcall console_output_entry(void* endpoint, ui::RenderQueue* queue, void*, std::int32_t*) {
#else
void console_output_entry(void* endpoint, ui::RenderQueue* queue, void*, std::int32_t*) {
#endif
    auto& handler = *static_cast<ConsoleOutputHandler*>(endpoint);
    draw_console_output(*queue, handler.config, handler.ring, handler.frame_time);
}
const std::array<hud::HudFrameEntry, 1> console_output_table{console_output_entry};
}

DisableSvPureHandler::DisableSvPureHandler(const DisablePureConfig& canonical) noexcept
    : NetworkMessageCallback{nullptr}, config(canonical) {}
DisableSvPureHandler::DisableSvPureHandler(engine::OwnerRegistryBinding callbacks,
                                           const DisablePureConfig& canonical)
    : DisableSvPureHandler(canonical) {
    callbacks.append(this); //4336F73C precedes the secondary-vtable write.
    initialize_vtable();
}
void DisableSvPureHandler::initialize_vtable() noexcept {
    vtable = disable_pure_table.data();
}
void apply_disable_sv_pure(const DisablePureConfig& config, NetworkMessageCall& call) {
    if (config.enabled.value && engine::invoke<std::int32_t>(call.message, 7) == 14)
        call.cancelled = 1;
}
void update_network_message_transition(NetworkMessageTransition& state, void* message) {
    if (!state.active)
        return;
    if (engine::invoke<std::int32_t>(message, 7) == 4) {
        if (read<std::int32_t>(message, 0xC) == *state.sequence) {
            write<std::int32_t>(message, 0xC, -1);
            state.active = 0;
            state.mismatched = 0;
            state.status = 1;
            state.acknowledged_at = *state.current_time;
            return;
        }
        state.mismatched = 1;
    }
    if (state.active && state.mismatched && engine::invoke<std::int32_t>(message, 7) == 9) {
        if (state.reset)
            state.reset();
        state.active = 0;
        state.mismatched = 0;
        state.status = 0;
    }
}
bool send_network_message(NetworkMessageRuntime& runtime, void* channel, void* message,
                          std::uint8_t force_reliable, std::uint8_t voice) {
    update_network_message_transition(runtime.transition, message);
    if (engine::invoke<std::int32_t>(message, 7) == 23 &&
        (!(read<std::uint8_t>(message, 0x1C) & 1) ||
         (read<std::int32_t>(message, 0x10) != 0x23 && read<std::int32_t>(message, 0x10) != 0x44)))
        return true;
    if (engine::invoke<std::int32_t>(message, 7) == 18 && (read<std::uint8_t>(message, 0x14) & 1)) {
        const auto* string = read<const void*>(message, 0xC);
        const auto size = read<std::uint32_t>(string, 0x10);
        if (size) {
            const auto* bytes = read<std::uint32_t>(string, 0x14) < 16
                                    ? static_cast<const std::byte*>(string)
                                    : read<const std::byte*>(string, 0);
            const auto module = runtime.client_module;
            const auto header = module ? reinterpret_cast<std::uintptr_t>(module) +
                                             read<std::uint32_t>(module, 0x3C)
                                       : 0;
            if (contains_payload(bytes, size, reinterpret_cast<const std::byte*>(header + 8), 4) ||
                contains_payload(bytes, size, reinterpret_cast<const std::byte*>("UserExtraData"),
                                 13))
                return true;
        }
    }
    NetworkMessageCall call{channel, message, &force_reliable, &voice, 0};
    const auto callbacks = runtime.callbacks.entries();
    for (auto* object : callbacks) {
        auto* callback = static_cast<NetworkMessageCallback*>(object);
        callback->vtable[0](object, &call);
        if (call.cancelled)
            return true;
    }
    const auto table = *static_cast<void***>(channel);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    using Send = engine::MemberFunction<bool, void*, std::uint8_t, std::uint8_t>;
    return reinterpret_cast<Send>(metadata->original_table[40])(channel, message, force_reliable,
                                                                voice);
}
void bind_network_message_runtime(NetworkMessageRuntime& runtime) noexcept {
    active_network_message_runtime = &runtime;
}
#if defined(_M_IX86) || defined(__i386__)
bool __fastcall send_network_message_hook(void* channel, void*, void* message,
                                          std::uint8_t force_reliable, std::uint8_t voice) {
#else
bool send_network_message_hook(void* channel, void*, void* message, std::uint8_t force_reliable,
                               std::uint8_t voice) {
#endif
    return send_network_message(*active_network_message_runtime, channel, message, force_reliable,
                                voice);
}

ConsoleOutputHandler::ConsoleOutputHandler(const ConsoleOutputConfig& canonical,
                                           ColoredLogRing& shared_ring, const float& delta) noexcept
    : config(canonical), ring(shared_ring), frame_time(delta) {}
ConsoleOutputHandler::ConsoleOutputHandler(engine::OwnerRegistryBinding callbacks,
                                           const ConsoleOutputConfig& canonical,
                                           ColoredLogRing& shared_ring, const float& delta)
    : ConsoleOutputHandler(canonical, shared_ring, delta) {
    hud::register_hud_frame(callbacks, this);
    initialize_vtable();
}
void ConsoleOutputHandler::initialize_vtable() noexcept {
    vtable = console_output_table.data();
    ring.enabled = &config.enabled.value;
}

void draw_console_output(ui::RenderQueue& queue, const ConsoleOutputConfig& config,
                         ColoredLogRing& ring, float frame_time) {
    if (!config.enabled.value) {
        ring.count = 0;
        ring.head = 0;
        return;
    }
    while (ring.count && ring.lines[(ring.head - 1 + ring.count) & 7].lifetime <= 0.0f)
        --ring.count;
    if (!queue.fonts)
        throw std::logic_error("Console output requires the font backend");
    const auto step = add(ui::font_height(*queue.fonts, 1), 1);
    std::int32_t y = 5;
    for (auto index = static_cast<std::int32_t>(ring.count) - 1; index >= 0; --index) {
        auto& line = ring.lines[(ring.head + std::uint32_t(index)) & 7];
        line.lifetime -= frame_time;
        std::uint32_t color = 0xFFFFFFFFu;
        if (0.5f > line.lifetime) {
            // Native MAXSS/MINSS select their second operand when unordered.
            auto fade = line.lifetime > 0.0f ? line.lifetime : 0.0f;
            fade = fade < 0.5f ? fade : 0.5f;
            fade *= 2.0f;
            color = (std::uint32_t(truncate(fade * 255.0f)) << 24) | 0x00FFFFFFu;
            if (index == 0 && fade < 0.2f)
                y = truncate(static_cast<float>(y) -
                             (1.0f - fade / 0.2f) * static_cast<float>(step));
        }
        queue.text(8, y, color, 9, 0, line.text);
        y = add(y, step);
    }
}
} // namespace skeet::misc
