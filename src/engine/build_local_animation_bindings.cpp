#include "skeet/engine/build_local_animation_bindings.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace skeet::engine {
namespace {
template <class T> T read(const void* address) {
    T result;
    std::memcpy(&result, address, sizeof result);
    return result;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::invalid_argument(message);
}
struct Module {
    std::byte* base;
    std::uint32_t text_end;
    std::byte* code(std::uint32_t rva, std::initializer_list<int> pattern) const {
        require(rva >= 0x1000 && rva < text_end && pattern.size() <= text_end - rva,
                "Local animation code leaves its verified module text");
        auto* const address = base + rva;
        auto* cursor = address;
        for (auto value : pattern) {
            require(value < 0 || std::uint8_t(*cursor) == value,
                    "Local animation engine instructions differ");
            ++cursor;
        }
        return address;
    }
    void call(std::uint32_t rva, std::uint32_t target) const {
        code(rva, {0xE8, -1, -1, -1, -1});
        require(std::int64_t(rva) + 5 + read<std::int32_t>(base + rva + 1) == target,
                "Local animation engine direct call target differs");
    }
};
Module checked(void* object, std::uint32_t timestamp, std::uint32_t size, std::uint32_t checksum,
               std::uint32_t text_size) {
    require(object != nullptr, "Local animation requires its actual loaded module");
    auto* const base = static_cast<std::byte*>(object);
    require(read<std::uint16_t>(base) == 0x5A4D && read<std::uint32_t>(base + 0x3C) == 0x150,
            "Unsupported local animation module DOS header");
    const auto* const nt = base + 0x150;
    require(
        read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
            read<std::uint16_t>(nt + 6) == 7 && read<std::uint16_t>(nt + 0x14) == 0xE0 &&
            read<std::uint16_t>(nt + 0x18) == 0x10B && read<std::uint32_t>(nt + 8) == timestamp &&
            read<std::uint32_t>(nt + 0x50) == size && read<std::uint32_t>(nt + 0x58) == checksum,
        "Unsupported local animation engine build");
    const auto* const text = nt + 0xF8;
    require(std::memcmp(text, ".text\0\0\0", 8) == 0 &&
                read<std::uint32_t>(text + 8) == text_size &&
                read<std::uint32_t>(text + 12) == 0x1000 &&
                read<std::uint32_t>(text + 36) == 0x60000020,
            "Unsupported local animation text section");
    return {base, 0x1000 + text_size};
}
float layer_weight(void* entry, void* state, std::int32_t layer) {
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    float result;
    __asm__ volatile("pushl %[index]\n\txorl %%edx,%%edx\n\tcall *%%eax\n\tmovss %%xmm0,%[result]"
                     : [result] "=m"(result), "+a"(entry), "+c"(state)
                     : [index] "m"(layer)
                     : "edx", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                       "memory", "cc");
    return result;
#elif defined(_MSC_VER) && defined(_M_IX86)
    float result;
    __asm {push layer mov ecx,state xor edx,edx mov eax,entry call eax movss result,xmm0}
    return result;
#else
    (void)entry;
    (void)state;
    (void)layer;
    throw std::logic_error("Local layer weight requires the native x86 SIMD ABI");
#endif
}
}
BuildLocalAnimationBindings initialize_build_local_animation_bindings(NativeDataImage& data) {
    require(sizeof(void*) == 4, "Local animation binding requires x86 pointers");
    const auto client =
        checked(data.native_cell<void*>(0x4346A55Cu), 0x65272025, 0x5509000, 0xFA635F, 0xB38409);
    const auto engine =
        checked(data.native_cell<void*>(0x43468D20u), 0x64EE7E86, 0x39C0000, 0x64174E, 0x451C32);
    auto* const mapping = client.code(
        0x36CAC0, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x81, 0xEC, 0xAC, 0, 0, 0, 0x53, 0x56,
                   0x57, 0x8B, 0xF9, 0x8B, 0x17, 0x83, 0xBA, 0x50, 1,    0, 0, 0, 0x74, 0x34});
    client.code(0x36CD15, {0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3});
    client.code(0x36CD20, {0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC3});
    auto* const select =
        client.code(0x19D140, {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC, 0x34, 0x53, 0x56,
                               0x8B, 0x75, 8,    0x8B, 0xD9, 0x57, 0x89, 0x5C, 0x24, 0x20});
    for (const auto rva : {0x19D199u, 0x19D215u, 0x19D227u, 0x19D242u, 0x19D468u, 0x19D47Fu})
        client.code(rva, {0xC2, 0x10, 0});
    auto* const find = client.code(0x9A2C40, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x56, 0x8B, 0x75,
                                              0x0C, 0x57, 0x8B, 0xF9, 0x85, 0xF6, 0x75, 0x13, 0x8B,
                                              0x45, 8,    0xB9, 0xFF, 0xFF, 0,    0});
    auto* const add =
        client.code(0x9A2CF0, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x0C, 0x53, 0x8B, 0x5D, 0x0C,
                               0x57, 0x8B, 0xF9, 0x89, 0x7D, 0xF4, 0x85, 0xDB, 0x75, 0x13});
    for (const auto rva : {0x9A2C61u, 0x9A2C9Eu, 0x9A2D14u, 0x9A2E03u})
        client.code(rva, {0xC2, 8, 0});
    client.code(0x36C98B, {0xB9, -1, -1, -1, -1, 0x50});
    client.call(0x36C991, 0x9A2CF0);
    auto* const symbols =
        reinterpret_cast<void*>(std::uintptr_t(read<std::uint32_t>(client.base + 0x36C98C)));
    require(symbols == client.base + 0xDF7FC0, "Local animation symbol-table operand differs");
    auto* const activity = data.native_cell<std::byte*>(0x4346A460u);
    require(activity == client.base + 0xB8F4FC,
            "Local animation requires the actual resolved BasePlayerAnimState vtable");
    require(read<void*>(activity + 0x5C) == client.base + 0x1A3620 &&
                read<void*>(activity + 0x64) == client.base + 0x1A17D0,
            "Local activity interface methods differ");
    // Both supported methods read only receiver+1C. Words1..6 of the native
    // temporary receiver are uninitialized and have no semantic consumer.
    client.code(0x1A3620, {0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x71, 0x1C});
    client.code(0x1A3663, {0xC2, 4, 0});
    client.code(0x1A18A6, {0xC3});
    auto* const weight = client.code(
        0x442740, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 8,    0x53, 0x56, 0x8B, 0x35, -1, -1, -1, -1,
                   0x57, 0x8B, 0xF9, 0x8B, 0xCE, 0x8B, 6,    0xFF, 0x90, 0x84, 0,  0,  0});
    client.code(0x44289B,
                {0xF3, 0x0F, 0x10, 0x45, 0xFC, 0x5F, 0x5E, 0x5B, 0x8B, 0xE5, 0x5D, 0xC2, 4, 0});
    client.code(0x44138C, {0x33, 0xD2, 0x8B, 0x4E, 0x60, 0xFF, 0x74, 0x24, 0x1C, 0x52,
                           0x8B, 1,    0x8B, 0x80, 0x78, 3,    0,    0,    0xFF, 0xD0});
    client.code(0x2F10CB, {0x8B, 1, 0xFF, 0x90, 0x30, 4, 0, 0, 0x85, 0xC0, 0x74, 0x0A});
    client.code(0x430D06, {0x8B, 6, 0xFF, 0x90, 0x30, 4, 0, 0, 0x85, 0xC0, 0x74, 0x16});
    auto* const process =
        engine.code(0xD99C0, {0x53, 0x8A, 0xD9, 0x8B, 0x0D, -1, -1,   -1,   -1,   0x56, 0x57,
                              0x8B, 0xB9, 0x0C, 0x10, 0,    0,  0x85, 0xFF, 0x74, 0x16});
    engine.code(0xD9C66, {0x5F, 0x5E, 0x5B, 0xC3});
    engine.call(0xD9A13, 0x229070);
    engine.code(0xD9A18, {0x84, 0xC0, 0x0F, 0x84, 0x33, 2, 0, 0});
    const auto* const velocity = data.native_cell<const std::byte*>(0x43467F0Cu);
    require(velocity == client.base + 0x443481,
            "Local physics requires the earlier verified velocity-code producer");
    const auto* const branch = client.code(0x4434F1, {0x0F, 0x86, 0x8B, 0, 0, 0});
    auto* const continuation = const_cast<std::byte*>(branch + 6 + read<std::int32_t>(branch + 2));
    require(continuation == client.base + 0x443582, "Local velocity branch target differs");
    client.code(0x443582, {0xF3, 0x0F, 0x7E, 0x6C, 0x24, 0x20});
    // Exact slot88 query scopes: one native hook inspects its caller's outer
    // stack word, the other checks the direct team-query return address.
    // Both helpers independently reach virtual88 at byte offset160.
    client.code(0x4123A5, {0x8B, 0xCE});
    client.call(0x4123A7, 0x1B72A0);
    client.code(0x4123AC, {0x84, 0xC0, 0x75, 0x37, 0x38, 0x86, 0x34, 0x36, 0, 0});
    client.code(0x1B72A0, {0x56, 0x8B, 0xF1, 0x8B, 6, 0xFF, 0x90, 0x60, 1, 0, 0, 0x83, 0xF8, 1});
    client.code(0x5B1E25, {0x8B, 0xCB, 0x8B, 0xF0});
    client.call(0x5B1E29, 0x1B7230);
    client.code(0x5B1E2E, {0x3B, 0xC6, 0x74, 0x27, 0x8B, 0xCB});
    client.code(0x1B7244, {0x8B, 6, 0x8B, 0xCE, 0xFF, 0x90, 0x60, 1, 0, 0, 0x83, 0xF8, 1});
    // The separate sent-angle consumer calls virtual170 and copies all three
    // returned float words. This is an instruction continuation, not a getter.
    client.code(0x52EFA0, {0x8B, 6, 0x8B, 0xCE, 0x8B, 0x80, 0xA8, 2, 0, 0, 0xFF, 0xD0});
    client.code(0x52EFAC, {0x8B, 8,    0x89, 0x8F, 0x94, 0, 0, 0,    0x8B, 0x48,
                           4,    0x89, 0x8F, 0x98, 0,    0, 0, 0x8B, 0x40, 8});
    const BuildLocalAnimationBindings result{
        reinterpret_cast<animation::InitializeSequenceMapping>(mapping),
        reinterpret_cast<animation::SelectSymbolSequence>(select),
        reinterpret_cast<animation::LookupAnimationSymbol>(find),
        reinterpret_cast<animation::LookupAnimationSymbol>(add),
        symbols,
        activity,
        weight,
        reinterpret_cast<misc::NetworkFixEngineEntry>(process),
        reinterpret_cast<std::uintptr_t>(client.base + 0x4413A0),
        reinterpret_cast<std::uintptr_t>(client.base + 0x2F10D3),
        reinterpret_cast<std::uintptr_t>(client.base + 0x430D0E),
        reinterpret_cast<std::uintptr_t>(engine.base + 0xD9A18),
        reinterpret_cast<std::uintptr_t>(client.base + 0x443582)};
    data.native_cell<decltype(result.initialize_mapping)>(0x43469348u) = result.initialize_mapping;
    data.native_cell<decltype(result.select_symbol_sequence)>(0x4346A4F8u) =
        result.select_symbol_sequence;
    data.native_cell<decltype(result.find_symbol)>(0x434691DCu) = result.find_symbol;
    data.native_cell<decltype(result.add_symbol)>(0x4346728Cu) = result.add_symbol;
    data.native_cell<void*>(0x4346830Cu) = symbols;
    data.native_cell<void*>(0x4346A598u) = weight;
    data.native_cell<decltype(result.network_process)>(0x4346A454u) = result.network_process;
    data.native_cell<std::uintptr_t>(0x434688C4u) = result.strafe_call_site;
    data.native_cell<std::uintptr_t>(0x434689A0u) = result.proxy_call_site;
    data.native_cell<std::uintptr_t>(0x434692F4u) = result.sequence_call_site;
    data.native_cell<std::uintptr_t>(0x4346A64Cu) = result.proxy_context;
    data.native_cell<std::uintptr_t>(0x434757C4u) = result.velocity_continuation;
    data.native_cell<std::uintptr_t>(0x434674E0u) =
        reinterpret_cast<std::uintptr_t>(client.base + 0x4123AC);
    data.native_cell<std::uintptr_t>(0x4346756Cu) =
        reinterpret_cast<std::uintptr_t>(client.base + 0x5B1E2E);
    data.native_cell<std::uintptr_t>(0x43468518u) =
        reinterpret_cast<std::uintptr_t>(client.base + 0x52EFAC);
    return result;
}
animation::LocalLayerWeightEngine native_local_layer_weight_engine(void* entry) {
    return {entry, layer_weight};
}
} //namespace skeet::engine
