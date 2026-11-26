#include "skeet/network/startup_user_messages.hpp"
#include "skeet/network/user_message_runtime.hpp"
#include "skeet/rage/feedback_owner.hpp"
#include "skeet/script/say_text_message.hpp"
#include "skeet/esp/user_message_handlers.hpp"
#include <cstring>
#include <stdexcept>

namespace skeet::network {
namespace {
template <class T> T read(const std::byte* bytes, std::size_t offset = 0) {
    T value;
    std::memcpy(&value, bytes + offset, sizeof value);
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
extern "C" __attribute__((naked)) void skeet_user_message_destroy() {
    __asm__("andl $0,8(%ecx)\n\tmovl 4(%ecx),%ecx\n\tmovl (%ecx),%eax\n\tjmp *(%eax)");
}
#define MESSAGE_FORWARD(slot, offset)                                                              \
    extern "C" __attribute__((naked)) void skeet_user_message_forward_##slot() {                   \
        __asm__("movl 4(%ecx),%ecx\n\tmovl (%ecx),%eax\n\tjmp *" #offset "(%eax)");                \
    }
#elif defined(_M_IX86) && defined(_MSC_VER)
extern "C" __declspec(naked) void skeet_user_message_destroy(){
    __asm and dword ptr[ecx + 8], 0 __asm mov ecx, dword ptr[ecx + 4] __asm mov eax,
    dword ptr[ecx] __asm jmp dword ptr[eax]}
#define MESSAGE_FORWARD(slot, offset)                                                              \
    extern "C" __declspec(naked) void skeet_user_message_forward_##slot() {                        \
        __asm mov ecx, dword ptr[ecx + 4] __asm mov eax,                                           \
            dword ptr[ecx] __asm jmp dword ptr[eax + offset]                                       \
    }
#else
#error Startup user-message wrappers require the actual x86 member ABI
#endif
MESSAGE_FORWARD(1, 4)
MESSAGE_FORWARD(3, 12)
MESSAGE_FORWARD(4, 16)
#undef MESSAGE_FORWARD

void* __thiscall decode(ClientStateScopedPatch* wrapper, std::uintptr_t first,
                        std::uintptr_t second, std::uintptr_t third,
                        std::uintptr_t fourth) { //433D26FB
    using Decode =
        void*(__thiscall*)(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t);
    auto* const original = wrapper->saved;
    const auto method = (*reinterpret_cast<Decode**>(original))[2];
    auto* const result = method(original, first, second, third, fourth);
    if (result && wrapper->field_0c)
        reinterpret_cast<void(__cdecl*)(const void*)>(std::uintptr_t(wrapper->field_0c))(result);
    return result;
}
const std::array<std::uintptr_t, 5> table{
    reinterpret_cast<std::uintptr_t>(&skeet_user_message_destroy),
    reinterpret_cast<std::uintptr_t>(&skeet_user_message_forward_1),
    reinterpret_cast<std::uintptr_t>(&decode),
    reinterpret_cast<std::uintptr_t>(&skeet_user_message_forward_3),
    reinterpret_cast<std::uintptr_t>(&skeet_user_message_forward_4)};
}
std::span<const StartupUserMessageDescriptor, 4> startup_user_message_descriptors() {
    static const std::array<StartupUserMessageDescriptor, 4> descriptors{
        {{64, 0x433A00C4u, 0x43469034u, rage::feedback_position_hook},
         {6, 0x433C05B4u, 0x4346A6E0u, script::say_text_message_hook},
         {69, 0x433E7234u, 0x4346A5C4u, esp::weapon_sound_user_message_hook},
         {25, 0x433FCBD6u, 0x43468394u, esp::spotted_user_message_hook}}};
    return descriptors;
}
std::span<const std::uintptr_t, 5> startup_user_message_wrapper_table() {
    return table;
}

StartupUserMessageTargets discover_startup_user_message_targets(engine::NativeDataImage& data) {
    const auto* const module = data.native_cell<const std::byte*>(0x4346A55Cu);
    auto* const client = data.native_cell<void*>(0x434683E0u);
    require(module && client, "Startup user messages require actual client.dll and VClient018");
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    require(read<std::uint16_t>(module) == 0x5A4D && read<std::uint32_t>(module, 0x3C) == 0x150,
            "Unsupported startup user-message DOS header");
    const auto* const nt = module + 0x150;
    require(
        read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt, 4) == 0x14C &&
            read<std::uint16_t>(nt, 0x18) == 0x10B && read<std::uint32_t>(nt, 8) == 0x65272025 &&
            read<std::uint32_t>(nt, 0x50) == 0x5509000 && read<std::uint32_t>(nt, 0x58) == 0xFA635F,
        "Unsupported startup user-message client build");
    const auto* const dispatch = (*reinterpret_cast<const std::byte* const* const*>(client))[38];
    require(dispatch == module + 0x262130, "VClient018 user-message dispatch method differs");
    const auto address = [&](std::size_t offset) { return read<std::uint32_t>(dispatch, offset); };
    const auto relative = [&](std::size_t offset) {
        return reinterpret_cast<std::uintptr_t>(dispatch) + offset + 5 +
               read<std::int32_t>(dispatch, offset + 1);
    };
    const unsigned char prefix[]{0x55, 0x8B, 0xEC, 0xA1};
    const unsigned char gate[]{0xA8, 0x01, 0x75, 0x1A, 0x83, 0xC8, 0x01, 0xA3};
    const unsigned char ending[]{0x83, 0xC4, 0x04, 0xB9};
    require(!std::memcmp(dispatch, prefix, sizeof prefix) && address(4) == base + 0x5362700 &&
                !std::memcmp(dispatch + 8, gate, sizeof gate) &&
                address(0x10) == base + 0x5362700 && dispatch[0x14] == std::byte{0xE8} &&
                relative(0x14) == base + 0x377D50 && dispatch[0x19] == std::byte{0x68} &&
                address(0x1A) == base + 0xB1CF10 && dispatch[0x1E] == std::byte{0xE8} &&
                relative(0x1E) == base + 0xAC74D7 &&
                !std::memcmp(dispatch + 0x23, ending, sizeof ending) &&
                address(0x27) == base + 0x5362704 && dispatch[0x2B] == std::byte{0x5D} &&
                dispatch[0x2C] == std::byte{0xE9} && relative(0x2C) == base + 0x377DA0,
            "Startup SDK message map producer operands differ");
    const auto* const map = reinterpret_cast<const std::byte*>(std::uintptr_t(address(0x27)));
    using Less = bool(__cdecl*)(const std::int32_t*, const std::int32_t*);
    const auto less = read<Less>(map);
    constexpr unsigned char comparison[]{0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08, 0x8B, 0x08, 0x8B,
                                         0x45, 0x0C, 0x3B, 0x08, 0x0F, 0x9C, 0xC0, 0x5D, 0xC3};
    require(reinterpret_cast<std::uintptr_t>(less) == base + 0x197570 &&
                !std::memcmp(reinterpret_cast<const void*>(less), comparison, sizeof comparison),
            "Startup SDK message map comparator differs");
    const auto* const nodes = read<const std::byte*>(map, 4);
    const auto capacity = read<std::int32_t>(map, 8);
    require(nodes && capacity > 0 && capacity <= 0xFFFF &&
                read<const std::byte*>(map, 0x18) == nodes,
            "Startup SDK message map pool is invalid");
    StartupUserMessageTargets targets{};
    const auto descriptors = startup_user_message_descriptors();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto identifier = descriptors[index].identifier;
        auto node = read<std::uint16_t>(map, 0x10);
        std::size_t visited = 0;
        for (;;) {
            require(node != 0xFFFF && node < capacity && visited++ < std::size_t(capacity),
                    "A required SDK user-message tree node is missing or cyclic");
            const auto* const item = nodes + std::size_t(node) * 0x30;
            const auto key = read<std::int32_t>(item, 8);
            // The independently verified SDK comparator above is precisely a
            // signed integer comparison; its ordinary semantics are source.
            if (identifier < key) {
                node = read<std::uint16_t>(item);
                continue;
            }
            if (key < identifier) {
                node = read<std::uint16_t>(item, 2);
                continue;
            }
            targets[index] = read<ClientStateScopedPatch**>(item, 0x14);
            require(targets[index] && read<std::int32_t>(item, 0x20) > 0 && *targets[index],
                    "A required SDK user-message binding vector is empty");
            break;
        }
    }
    return targets;
}
void prepare_startup_user_message_patches(engine::NativeDataImage& data,
                                          const StartupUserMessageTargets& targets) {
    auto*& head = data.native_cell<ClientStateScopedPatch*>(0x43476900u);
    if (head)
        throw std::logic_error("Startup user-message patch list is already prepared");
    const auto descriptors = startup_user_message_descriptors();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto& patch =
            data.native_cell<ClientStateScopedPatch>(descriptors[index].patch_address);
        if (patch.field_00 || patch.saved || patch.target || patch.field_0c || patch.identifier ||
            patch.next || !targets[index] || !*targets[index])
            throw std::logic_error("Startup user-message patch or actual SDK binding is invalid");
    }
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto& patch = data.native_cell<ClientStateScopedPatch>(descriptors[index].patch_address);
        patch.field_00 = std::uint32_t(reinterpret_cast<std::uintptr_t>(table.data()));
        patch.target = targets[index];
        patch.field_0c =
            std::uint32_t(reinterpret_cast<std::uintptr_t>(descriptors[index].callback));
        patch.identifier = descriptors[index].identifier;
        patch.next = head;
        head = &patch;
    }
}
void prepare_startup_user_message_patches(engine::NativeDataImage& data) {
    require(user_message_contexts_ready(data),
            "SDK user-message callbacks require their initialized source contexts");
    prepare_startup_user_message_patches(data, discover_startup_user_message_targets(data));
}
static_assert(sizeof(ClientStateScopedPatch) == 24);
} //namespace skeet::network
