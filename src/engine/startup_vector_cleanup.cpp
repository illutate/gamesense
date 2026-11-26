#include "skeet/engine/startup_vector_cleanup.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/client/shutdown.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace skeet::engine {
namespace {
StartupVectorCleanupBindings* active_cleanup = nullptr;

void release(void* allocation, JobMemory memory) { //433837D0
    if (!allocation)
        return;
    auto* bytes = static_cast<std::byte*>(allocation);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, sizeof displacement);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
constexpr std::array finalizers{
    0x4332F1FDu, 0x4334DA70u, 0x4339D4FEu, 0x433874D2u, 0x4341EFD3u,
    0x433C8C11u, 0x433AFCE8u, 0x433C956Cu, 0x433FBE15u,
};
constexpr std::array initializers{
    0x433C7FAEu, 0x4334C970u, 0x43325025u, 0x433F47E6u, 0x4340BCC1u, 0x433412AFu, 0x4334115Au,
};
template <std::size_t Index> void finalize_entry() {
    if (!active_cleanup)
        throw std::logic_error("Startup vector cleanup has no stable source binding");
    active_cleanup->finalize(finalizers[Index]);
}
template <std::size_t... Index> constexpr auto finalizer_functions(std::index_sequence<Index...>) {
    return std::array<void (*)(), sizeof...(Index)>{&finalize_entry<Index>...};
}
constexpr auto functions = finalizer_functions(std::make_index_sequence<finalizers.size()>{});
template <std::size_t Index> void register_entry(void* owner) {
    auto& binding = *static_cast<StartupVectorCleanupBindings*>(owner);
    (void)binding.callbacks.append(binding.source_finalizer(finalizers[Index]));
}
template <std::size_t... Index>
void bind_entries(client::NativeStartupSources& sources, StartupVectorCleanupBindings& binding,
                  std::index_sequence<Index...>) {
    (sources.bind_initializer({initializers[Index], &binding, &register_entry<Index>}), ...);
}
}

void cleanup_startup_callback_map(StartupCallbackMap& map, JobMemory memory) {
    const auto count = map.bucket_count;
    auto** const buckets = map.buckets;
    for (std::uint32_t index = 0; index < count; ++index) {
        auto* node = buckets[index];
        while (node) {
            auto* const next = node->next;
            release(node, memory);
            node = next;
        }
        buckets[index] = nullptr;
    }
    map.size = 0;
    // The wrapper reloads both fields after the complete clear helper.
    if (map.bucket_count > 1)
        release(map.buckets, memory);
}

void cleanup_startup_v8_slots(StartupV8Slots& slots, DisposeStartupV8Global dispose,
                              JobMemory memory) {
    auto* slot = slots.begin;
    auto* const end = slots.end;
    while (slot != end) {
        if (*slot) {
            dispose(*slot);
            *slot = nullptr;
        }
        ++slot;
    }
    // DisposeGlobal may replace the current header. Native frees the live
    // begin pointer, after traversing the range captured at entry.
    release(slots.begin, memory);
}

void cleanup_startup_v8_handle(StartupV8Handle& global, DisposeStartupV8Global dispose) {
    if (global.owns_handle && global.handle) {
        dispose(global.handle);
        global.handle = nullptr;
    }
}

void cleanup_startup_ui_bindings(StartupVector<StartupUiBinding>& bindings, JobMemory memory) {
    auto* cursor = bindings.begin;
    auto* const end = bindings.end;
    if (cursor != end) {
        do {
            release(cursor->references.begin, memory);
            ++cursor;
        } while (cursor != end);
        cursor = bindings.begin;
    }
    release(cursor, memory);
}

void cleanup_startup_history(lagcomp::LagRecord*& free_head,
                             std::span<client::NativePlayerStateLayout*, 64> players,
                             JobMemory memory) {
    for (auto* const player : players) {
        if (!player)
            continue;
        while (player->history_count) {
            const auto index = (player->history_first + player->history_count - 1u) & 31u;
            auto* record = reinterpret_cast<lagcomp::LagRecord*>(
                std::uintptr_t(player->native_history_links[index]));
            if (record) {
                record->header.field_00 =
                    std::uint32_t(reinterpret_cast<std::uintptr_t>(free_head));
                free_head = record;
            }
            --player->history_count;
        }
    }
    while (free_head) {
        auto* const record = free_head;
        free_head = reinterpret_cast<lagcomp::LagRecord*>(std::uintptr_t(record->header.field_00));
        record->header.field_00 = 0;
        release(record, memory);
    }
    for (std::uint32_t index = 64; index;) {
        auto*& player = players[--index];
        if (player) {
            release(player, memory);
            player = nullptr;
        }
    }
}

void cleanup_startup_reference_strings(StartupInlineVector<StartupReferenceString>& strings,
                                       JobMemory memory) {
    auto* cursor = strings.begin;
    auto* const end = strings.end;
    if (cursor != end) {
        do {
            auto& text = cursor->text;
            const auto byte_capacity = std::uint32_t(reinterpret_cast<std::uintptr_t>(text.limit) -
                                                     reinterpret_cast<std::uintptr_t>(text.begin)) &
                                       0xFFFFFFFEu;
            if (std::bit_cast<std::int32_t>(byte_capacity) > 2 && text.begin)
                release(text.begin, memory);
            ++cursor;
        } while (cursor != end);
        cursor = strings.begin;
    }
    if (cursor && cursor != strings.inline_buffer)
        release(cursor, memory);
}

void cleanup_startup_inline_callbacks(StartupInlineVector<std::uint32_t>& callbacks,
                                      JobMemory memory) {
    auto* const begin = callbacks.begin;
    if (begin && begin != callbacks.inline_buffer)
        release(begin, memory);
}

StartupVectorCleanupBindings::StartupVectorCleanupBindings(PreservedInitialization& initialization,
                                                           client::ExitCallbacks& callbacks,
                                                           JobMemory memory,
                                                           DisposeStartupV8Global dispose)
    : StartupVectorCleanupBindings(initialization.image_owner(), callbacks, memory, dispose) {}
StartupVectorCleanupBindings::StartupVectorCleanupBindings(std::shared_ptr<NativeDataImage> image,
                                                           client::ExitCallbacks& callbacks,
                                                           JobMemory memory,
                                                           DisposeStartupV8Global dispose)
    : data(image), callbacks(callbacks), memory(memory), dispose_global(dispose) {
    (void)require_native_data_image(data);
    if (!memory.free || !memory.process_heap || !dispose_global)
        throw std::invalid_argument("Startup vector cleanup requires process heap and V8 imports");
}

void StartupVectorCleanupBindings::finalize(std::uint32_t address) {
    const auto& image = require_native_data_image(data);
    switch (address) {
    case 0x4332F1FDu:
        cleanup_startup_callback_map(image.native_cell<StartupCallbackMap>(0x4346DA60u), memory);
        break;
    case 0x4334DA70u:
        cleanup_startup_callback_map(image.native_cell<StartupCallbackMap>(0x4346DA80u), memory);
        break;
    case 0x4339D4FEu:
        cleanup_startup_v8_slots(image.native_cell<StartupV8Slots>(0x4347963Cu), dispose_global,
                                 memory);
        break;
    case 0x433874D2u:
        cleanup_startup_v8_slots(image.native_cell<StartupV8Slots>(0x4347960Cu), dispose_global,
                                 memory);
        break;
    case 0x4341EFD3u:
        cleanup_startup_v8_handle(image.native_cell<StartupV8Handle>(0x43479600u), dispose_global);
        break;
    case 0x433C8C11u:
        cleanup_startup_ui_bindings(image.native_cell<StartupVector<StartupUiBinding>>(0x43479760u),
                                    memory);
        break;
    case 0x433AFCE8u:
        cleanup_startup_history(
            image.native_cell<lagcomp::LagRecord*>(0x434797B8u),
            image.native_cell<std::array<client::NativePlayerStateLayout*, 64>>(0x434797BCu),
            memory);
        break;
    case 0x433C956Cu:
        cleanup_startup_reference_strings(
            image.native_cell<StartupInlineVector<StartupReferenceString>>(0x4346D720u), memory);
        break;
    case 0x433FBE15u:
        cleanup_startup_inline_callbacks(
            image.native_cell<StartupInlineVector<std::uint32_t>>(0x4346DAA0u), memory);
        break;
    default:
        throw std::out_of_range("Finalizer is not a recovered startup vector cleanup entry");
    }
}

void (*StartupVectorCleanupBindings::source_finalizer(std::uint32_t address))() {
    for (std::size_t index = 0; index < finalizers.size(); ++index) {
        if (finalizers[index] != address)
            continue;
        if (active_cleanup && active_cleanup != this)
            throw std::logic_error(
                "Startup vector exit callbacks require one stable source binding");
        active_cleanup = this;
        return functions[index];
    }
    throw std::out_of_range("Finalizer is not a recovered startup vector cleanup entry");
}

void bind_startup_vector_cleanup(client::NativeStartupSources& sources,
                                 StartupVectorCleanupBindings& binding) {
    if (&sources.data() != binding.data.get())
        throw std::invalid_argument(
            "Startup vector cleanup must borrow the same native data owner");
    bind_entries(sources, binding, std::make_index_sequence<initializers.size()>{});
}
} // namespace skeet::engine
