#include "skeet/misc/reveal_ranks.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>
#include <new>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::misc {
namespace {
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::uint32_t command_name_hash(const char* text) { //43359C70
    std::uint32_t hash = 0x811C9DC5;
    if (text)
        for (auto* byte = reinterpret_cast<const unsigned char*>(text); *byte; ++byte)
            hash = (hash ^ *byte) * 0x01000193;
    return hash ^ 0x92CA5DF1;
}
void exchange_command(RevealRanksCommand* target, RevealRanksCommand value) {
    // Recorded kernel32!InterlockedExchange75BFD980 exchanges a native x86
    // pointer. Use the pointer-sized Windows operation for the semantic view.
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(target),
                               reinterpret_cast<void*>(value));
}
RevealRanksRuntime* command_owner = nullptr;

#if defined(_M_IX86) || defined(__i386__)
#define RANKS_MEMBER __thiscall
using RankDispatch = void(__cdecl*)(std::uint32_t*);
#else
#define RANKS_MEMBER
using RankDispatch = void (*)(std::uint32_t*);
#endif
using OwnerEntry = void(RANKS_MEMBER*)(void*);
struct RevealRanksInstance;
void RANKS_MEMBER destroy_owner(void*);
void RANKS_MEMBER install_owner(void*);
void RANKS_MEMBER restore_owner(void*);
void RANKS_MEMBER unchanged_owner(void*) {} //433CCE8E is exactly RET.
const std::array<OwnerEntry, 9> owner_table{destroy_owner,   unchanged_owner, unchanged_owner,
                                            install_owner,   restore_owner,   unchanged_owner,
                                            unchanged_owner, unchanged_owner, unchanged_owner};

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
    const auto displacement = field<std::uint32_t>(bytes, -8);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
struct RevealRanksInstance {
    const OwnerEntry* vtable = nullptr;
    RevealRanksRuntime** active;
    engine::JobMemory memory;
    alignas(RevealRanksRuntime) std::array<std::byte, sizeof(RevealRanksRuntime)> state;

    RevealRanksInstance(engine::OwnerRegistryBinding owners, ConfigRegistries& registry,
                        RevealRanksConfig& configuration, RevealRanksRuntime*& current,
                        RevealRanksBindings bindings)
        : active(&current), memory(owners.memory) {
        owners.append(this); //433676A0 precedes vtable/configuration initialization.
        vtable = owner_table.data();
        initialize_reveal_ranks_config(registry, configuration);
        ::new (state.data()) RevealRanksRuntime(configuration, bindings);
    }
    RevealRanksRuntime& runtime() noexcept {
        return *std::launder(reinterpret_cast<RevealRanksRuntime*>(state.data()));
    }
    ~RevealRanksInstance() {
        runtime().~RevealRanksRuntime();
    }
};
void RANKS_MEMBER destroy_owner(void* object) { //433F1E4F
    auto* instance = static_cast<RevealRanksInstance*>(object);
    const auto memory = instance->memory;
    *instance->active = nullptr;
    command_owner = nullptr;
    instance->~RevealRanksInstance();
    free_owner(memory, instance);
}
void RANKS_MEMBER install_owner(void* object) {
    static_cast<RevealRanksInstance*>(object)->runtime().install_hooks();
}
void RANKS_MEMBER restore_owner(void* object) {
    static_cast<RevealRanksInstance*>(object)->runtime().restore_hooks();
}
#undef RANKS_MEMBER
}

void* find_reveal_ranks_command(void* cvar, std::uint32_t name_hash) {
    for (auto* command = field<void*>(cvar, 0x30); command; command = field<void*>(command, 4)) {
        if (command_name_hash(engine::invoke<const char*>(command, 6)) == name_hash)
            return command;
    }
    return nullptr;
}

RevealRanksRuntime::RevealRanksRuntime(RevealRanksConfig& configuration,
                                       RevealRanksBindings bindings)
    : enabled(&configuration.enabled.value), bindings(bindings) {
    if (!bindings.context)
        return;
    commands[0] = find_reveal_ranks_command(bindings.cvar, 0x48DAD9F1); //+showscores
    commands[1] = find_reveal_ranks_command(bindings.cvar, 0x667FF061); //+score
}

void RevealRanksRuntime::install_hooks() {
    if (!bindings.context || !bindings.rank_data)
        return;
    // The supported engine dispatches these modern ConCommand callbacks with
    // a CCommand reference. Validate both union tags before changing either.
    for (auto* command : commands)
        if (command && (field<std::uint8_t>(command, 0x20) & 6u) != 2u)
            throw std::runtime_error("Score command requires the CCommand callback ABI");
    for (std::size_t index = 0; index != commands.size(); ++index) {
        if (!commands[index])
            continue;
        auto& hook = hooks[index];
        hook.target =
            reinterpret_cast<RevealRanksCommand*>(static_cast<std::byte*>(commands[index]) + 0x18);
        hook.saved = *hook.target;
        hook.replacement = reveal_ranks_score_command;
        exchange_command(hook.target, hook.replacement);
    }
}

void RevealRanksRuntime::restore_hooks() {
    for (auto& hook : hooks) {
        if (hook.target && hook.saved)
            exchange_command(hook.target, hook.saved);
        hook = {};
    }
}

void RevealRanksRuntime::on_score_command(const void* command_arguments) {
    // Native uses4347BDCC for both installed commands. Its zero-argument
    // declaration loses the current engine's required CCommand argument.
    hooks[0].saved(command_arguments);
    if (!*enabled || !bindings.context || !bindings.rank_data || !bindings.game_rules)
        return;
    auto* rules = *bindings.game_rules;
    if (!rules || !field<std::uint8_t>(rules, bindings.valve_matchmaking))
        return;
    auto* callbacks = field<void*>(bindings.context, 0x74);
    if (!callbacks)
        return;
    const auto dispatch = field<RankDispatch>(callbacks, 4);
    if (!dispatch)
        return;
    std::array<std::uint32_t, 64> ranks;
    ranks.fill(10);
    auto* data = *bindings.rank_data;
    ranks[2] =
        data ? static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(data) + 0x140u) : 0;
    dispatch(ranks.data());
}

void reveal_ranks_score_command(const void* command_arguments) {
    command_owner->on_score_command(command_arguments);
}

void initialize_reveal_ranks(engine::OwnerRegistryBinding owners, ConfigRegistries& registry,
                             RevealRanksConfig& configuration, RevealRanksRuntime*& active,
                             RevealRanksBindings bindings) {
    auto* storage = allocate_owner(owners.memory, sizeof(RevealRanksInstance));
    if (!storage) {
        active = nullptr;
        command_owner = nullptr;
        return;
    }
    auto* instance =
        ::new (storage) RevealRanksInstance(owners, registry, configuration, active, bindings);
    active = &instance->runtime();
    command_owner = active;
}
} //namespace skeet::misc
