#include "skeet/engine/game_window_proxy.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/esp/map_entity_parser.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <new>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
#if defined(_M_IX86) || defined(__i386__)
#define WINDOW_MEMBER __thiscall
#define WINDOW_SCALAR __vectorcall
#define WINDOW_TEXT __fastcall
#else
#define WINDOW_MEMBER
#define WINDOW_SCALAR
#define WINDOW_TEXT
#endif
using WindowOwnerEntry = void(WINDOW_MEMBER*)(void*);
struct GameWindowProxy {
    const std::uintptr_t* table = nullptr;
    void* original = nullptr;
    GameWindowProxyOwner* owner = nullptr;
};
struct GameWindowProxyCells {
    const std::byte* const* guarded_return_site;
    void** const* game_interface_slot;
    std::uintptr_t* original_rtti;
    GameWindowProxyImports imports;
};
namespace {
void WINDOW_MEMBER destroy(void*);
void WINDOW_MEMBER initialize(void*);
void WINDOW_MEMBER restore(void*);
void WINDOW_MEMBER empty(void*) {} //433CCE8E
void WINDOW_SCALAR install(void*, float);
void WINDOW_TEXT map_text_empty(void*, const char*) {} //433CCE8E
const std::array<WindowOwnerEntry, 9> primary_table{destroy, initialize, empty, empty, restore,
                                                    empty,   empty,      empty, empty};
const esp::MapEntityParserTable map_table{install, map_text_empty};
std::array<std::uintptr_t, 26> source_proxy_table();
}

struct GameWindowProxyOwner {
    const WindowOwnerEntry* table = nullptr;
    std::uint32_t untouched_4 = 0;
    esp::MapEntityParserEndpoint maps{};
    const std::byte* continuation = nullptr;
    GameWindowProxy proxy{};
    std::uint32_t untouched_1c = 0;
    void** target = nullptr;
    void* saved = nullptr;
    GameWindowProxy* installed = nullptr;
    // Source context follows the complete native2C-byte object prefix.
    GameWindowProxyCells bindings;
    JobMemory memory;
    std::array<std::uintptr_t, 26> proxy_table;
    GameWindowProxyOwner(const GameWindowProxyRegistries& registries,
                         GameWindowProxyBindings context)
        : bindings{&context.guarded_return_site, &context.game_interface_slot,
                   &context.original_rtti, context.imports},
          memory(registries.lifecycle.memory), proxy_table(source_proxy_table()) {
        registries.lifecycle.append(this);
        esp::register_map_entity_parser(registries.maps, &maps);
        table = primary_table.data();
        maps.vtable = &map_table;
    }
};
#if UINTPTR_MAX == UINT32_MAX
static_assert(sizeof(GameWindowProxy) == 0xC);
static_assert(offsetof(GameWindowProxyOwner, maps) == 8);
static_assert(offsetof(GameWindowProxyOwner, continuation) == 0xC);
static_assert(offsetof(GameWindowProxyOwner, proxy) == 0x10);
static_assert(offsetof(GameWindowProxyOwner, proxy) + offsetof(GameWindowProxy, original) == 0x14);
static_assert(offsetof(GameWindowProxyOwner, proxy) + offsetof(GameWindowProxy, owner) == 0x18);
static_assert(offsetof(GameWindowProxyOwner, target) == 0x20);
static_assert(offsetof(GameWindowProxyOwner, saved) == 0x24);
static_assert(offsetof(GameWindowProxyOwner, installed) == 0x28);
static_assert(offsetof(GameWindowProxyOwner, bindings) == 0x2C);
#endif

namespace {
void exchange_interface(GameWindowProxyOwner& owner, void** target, void* value) {
    if (!owner.bindings.imports.exchange)
        throw std::logic_error("Game-window proxy requires the genuine InterlockedExchange import");
    const auto raw = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
    owner.bindings.imports.exchange(reinterpret_cast<volatile std::int32_t*>(target),
                                    std::bit_cast<std::int32_t>(raw));
}
void WINDOW_MEMBER destroy(void* object) { //433B1A7A ->433837D0
    if (!object)
        return;
    auto* const owner = static_cast<GameWindowProxyOwner*>(object);
    const auto memory = owner->memory;
    owner->~GameWindowProxyOwner();
    release_owner_storage(memory, object);
}
void WINDOW_MEMBER initialize(void* object) {
    initialize_game_window_proxy(*static_cast<GameWindowProxyOwner*>(object));
}
void WINDOW_MEMBER restore(void* object) {
    restore_game_window_proxy(*static_cast<GameWindowProxyOwner*>(object));
}
void WINDOW_SCALAR install(void* receiver, float) {
    auto* const owner = reinterpret_cast<GameWindowProxyOwner*>(
        static_cast<std::byte*>(receiver) - offsetof(GameWindowProxyOwner, maps));
    install_game_window_proxy(*owner);
}
#if defined(_WIN32)
template <class Function> Function required_export(const char* module_name, const char* name) {
    const auto module = GetModuleHandleA(module_name);
    const auto address = module ? GetProcAddress(module, name) : nullptr;
    if (!address)
        throw std::runtime_error("Required game-window proxy export is missing");
    return reinterpret_cast<Function>(address);
}
#endif
}

// The helper receives the actual caller stack word. The x86 veneer then tail
// jumps into the real virtual9 with its original receiver and argument stack.
// Engine callers provide the Win32 four-byte stack contract. Realign only the
// helper's private frame when the source compiler uses SSE stack alignment.
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define WINDOW_ALIGN __attribute__((force_align_arg_pointer))
#else
#define WINDOW_ALIGN
#endif
extern "C" WINDOW_ALIGN void* __cdecl skeet_game_window_proxy_prepare(void* receiver,
                                                                      std::uintptr_t* return_slot) {
    auto& proxy = *static_cast<GameWindowProxy*>(receiver);
    auto* owner = proxy.owner;
    if (*return_slot == reinterpret_cast<std::uintptr_t>(*owner->bindings.guarded_return_site)) {
        if (owner->target && owner->saved) {
            exchange_interface(*owner, owner->target, owner->saved);
            owner = proxy.owner; //the native entry reloads parent after the import
        }
        *return_slot = reinterpret_cast<std::uintptr_t>(owner->continuation);
    }
    return proxy.original;
}
#undef WINDOW_ALIGN

#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define WINDOW_FORWARD(slot, offset)                                                               \
    extern "C" __attribute__((naked)) void skeet_game_window_forward_##slot() {                    \
        __asm__("movl 4(%ecx),%ecx\n\tmovl (%ecx),%eax\n\tjmp *" #offset "(%eax)");                \
    }
extern "C" __attribute__((naked)) void skeet_game_window_slot_nine() {
    __asm__("pushl %esi\n\t"
            "pushl %edi\n\t"
            "leal 8(%esp),%edi\n\t"
            "movl %ecx,%esi\n\t"
            "pushl %edi\n\t"
            "pushl %esi\n\t"
            "call _skeet_game_window_proxy_prepare\n\t"
            "addl $8,%esp\n\t"
            "movl %eax,%ecx\n\t"
            "popl %edi\n\t"
            "popl %esi\n\t"
            "movl (%ecx),%eax\n\t"
            "jmp *36(%eax)");
}
#elif defined(_M_IX86) && defined(_MSC_VER)
#define WINDOW_FORWARD(slot, offset)                                                               \
    extern "C" __declspec(naked) void skeet_game_window_forward_##slot() {                         \
        __asm mov ecx, dword ptr[ecx + 4] __asm mov eax,                                           \
            dword ptr[ecx] __asm jmp dword ptr[eax + offset]                                       \
    }
extern "C" __declspec(naked) void skeet_game_window_slot_nine() {
    __asm {
        push esi
        push edi
        lea edi,[esp+8]
        mov esi,ecx
        push edi
        push esi
        call skeet_game_window_proxy_prepare
        add esp,8
        mov ecx,eax
        pop edi
        pop esi
        mov eax,dword ptr[ecx]
        jmp dword ptr[eax+36]
    }
}
#endif
#if defined(_M_IX86) || defined(__i386__)
WINDOW_FORWARD(0, 0)
WINDOW_FORWARD(1, 4)
WINDOW_FORWARD(2, 8)
WINDOW_FORWARD(3, 12)
WINDOW_FORWARD(4, 16)
WINDOW_FORWARD(5, 20)
WINDOW_FORWARD(6, 24)
WINDOW_FORWARD(7, 28)
WINDOW_FORWARD(8, 32)
WINDOW_FORWARD(10, 40)
WINDOW_FORWARD(11, 44)
WINDOW_FORWARD(12, 48)
WINDOW_FORWARD(13, 52)
WINDOW_FORWARD(14, 56)
WINDOW_FORWARD(15, 60)
WINDOW_FORWARD(16, 64)
WINDOW_FORWARD(17, 68)
WINDOW_FORWARD(18, 72)
WINDOW_FORWARD(19, 76)
WINDOW_FORWARD(20, 80)
WINDOW_FORWARD(21, 84)
WINDOW_FORWARD(22, 88)
WINDOW_FORWARD(23, 92)
WINDOW_FORWARD(24, 96)
#undef WINDOW_FORWARD
#endif

namespace {
std::array<std::uintptr_t, 26> source_proxy_table() {
#if defined(_M_IX86) || defined(__i386__)
#define WINDOW_ENTRY(slot) reinterpret_cast<std::uintptr_t>(&skeet_game_window_forward_##slot)
    return {0,
            WINDOW_ENTRY(0),
            WINDOW_ENTRY(1),
            WINDOW_ENTRY(2),
            WINDOW_ENTRY(3),
            WINDOW_ENTRY(4),
            WINDOW_ENTRY(5),
            WINDOW_ENTRY(6),
            WINDOW_ENTRY(7),
            WINDOW_ENTRY(8),
            reinterpret_cast<std::uintptr_t>(&skeet_game_window_slot_nine),
            WINDOW_ENTRY(10),
            WINDOW_ENTRY(11),
            WINDOW_ENTRY(12),
            WINDOW_ENTRY(13),
            WINDOW_ENTRY(14),
            WINDOW_ENTRY(15),
            WINDOW_ENTRY(16),
            WINDOW_ENTRY(17),
            WINDOW_ENTRY(18),
            WINDOW_ENTRY(19),
            WINDOW_ENTRY(20),
            WINDOW_ENTRY(21),
            WINDOW_ENTRY(22),
            WINDOW_ENTRY(23),
            WINDOW_ENTRY(24)};
#undef WINDOW_ENTRY
#else
    throw std::logic_error("Game-window proxy requires the source x86 tail-transfer ABI");
#endif
}
}

GameWindowProxyImports native_game_window_proxy_imports() {
#if defined(_WIN32)
    return {required_export<GameWindowCrc32>("ntdll.dll", "RtlComputeCrc32"),
            required_export<GameWindowExchange>("kernel32.dll", "InterlockedExchange")};
#else
    throw std::logic_error("Game-window proxy requires genuine Windows imports");
#endif
}
GameWindowProxyBindings native_game_window_proxy_bindings(NativeDataImage& data,
                                                          GameWindowProxyImports imports) {
    return {data.native_cell<const std::byte*>(0x43467288u), data.native_cell<void**>(0x43468B2Cu),
            data.native_cell<std::uintptr_t>(0x43469288u), imports};
}
std::size_t game_window_proxy_owner_size() noexcept {
    return sizeof(GameWindowProxyOwner);
}
GameWindowProxyOwner& construct_game_window_proxy_owner(void* storage,
                                                        const GameWindowProxyRegistries& registries,
                                                        GameWindowProxyBindings bindings) {
    return *::new (storage) GameWindowProxyOwner(registries, bindings);
}
GameWindowProxyOwner* create_game_window_proxy_owner(const GameWindowProxyRegistries& registries,
                                                     GameWindowProxyBindings bindings) {
    auto* const storage = allocate_owner_storage(
        registries.lifecycle.memory, std::max(std::size_t{0x2C}, game_window_proxy_owner_size()));
    if (!storage)
        return nullptr;
    return &construct_game_window_proxy_owner(storage, registries, bindings);
}
void initialize_game_window_proxy(GameWindowProxyOwner& owner) { //4348DD2A
    owner.proxy.owner = &owner;
    const auto& bindings = owner.bindings;
    auto* start = *bindings.guarded_return_site;
    if (!start)
        return;
    if (!bindings.imports.crc32)
        throw std::logic_error("Game-window proxy requires the genuine RtlComputeCrc32 import");
    std::uint32_t offset = 0;
    const std::byte* candidate;
    for (;;) {
        candidate =
            reinterpret_cast<const std::byte*>(reinterpret_cast<std::uintptr_t>(start) + offset);
        if (bindings.imports.crc32(0, candidate, 4) == 0xB94D4C7Eu)
            break;
        ++offset;
        start = *bindings.guarded_return_site;
    }
    owner.continuation = candidate;
    auto* slot = *bindings.game_interface_slot;
    if (!candidate || !slot)
        return;
    const auto* original_table = *static_cast<const std::uintptr_t**>(*slot);
    *bindings.original_rtti = original_table[-1];
    owner.proxy_table[0] = *bindings.original_rtti;
    owner.proxy.original = *slot;
    owner.proxy.table = owner.proxy_table.data() + 1;
    slot = *bindings.game_interface_slot;
    owner.target = slot;
    owner.saved = *slot;
    owner.installed = &owner.proxy;
}
void install_game_window_proxy(GameWindowProxyOwner& owner) { //433689F9, receiver owner+8
    if (owner.target && owner.installed)
        exchange_interface(owner, owner.target, owner.installed);
}
void restore_game_window_proxy(GameWindowProxyOwner& owner) { //4339436B
    if (owner.target && owner.installed && owner.saved)
        exchange_interface(owner, owner.target, owner.saved);
}
#undef WINDOW_MEMBER
#undef WINDOW_SCALAR
#undef WINDOW_TEXT
} // namespace skeet::engine
