#include "skeet/misc/network_fix.hpp"
#include "skeet/client/prediction_history_owner.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/unload.hpp"
#include <cstring>
#include <emmintrin.h>
#include <intrin.h>
#include <stdexcept>

namespace skeet::misc {
namespace {
const NetworkFixBindings* active_network_fix;
const NetworkFixProxyRuntime* active_proxy_runtime;
struct ProxyThreadState {
    const NetworkFixProxyRuntime* owner = nullptr;
    std::uint32_t continuation = 0;
    NetworkFixProxyTransfer transfer{};
};
thread_local ProxyThreadState proxy_thread;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void put(void* object, std::ptrdiff_t offset, const T& value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
}
extern "C" const NetworkFixProxyTransfer* __cdecl skeet_network_fix_proxy_prepare() {
    if (!proxy_thread.owner)
        throw std::logic_error("Network-fix return proxy has no recorded source owner");
    return proxy_thread.owner->prepare_proxy_transfer();
}
extern "C" std::uint32_t __cdecl skeet_network_fix_proxy_guard(std::uintptr_t outer_return_site) {
    if (!active_proxy_runtime)
        throw std::logic_error("Network-fix singlestep guard has no source owner");
    return active_proxy_runtime->finish_singlestep(outer_return_site) ? 1u : 0u;
}
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
extern "C" __attribute__((naked)) void skeet_network_fix_proxy_entry() {
    __asm__("call _skeet_network_fix_proxy_prepare\n\t"
            "pushl (%eax)\n\t"
            "movl 8(%eax),%ecx\n\t"
            "movl 4(%eax),%eax\n\t"
            "testl %eax,%eax\n\t"
            "ret");
}
extern "C" __attribute__((naked)) void skeet_network_fix_singlestep_entry() {
    __asm__("pushl 4(%esp)\n\t"
            "call _skeet_network_fix_proxy_guard\n\t"
            "addl $4,%esp\n\t"
            "testl %eax,%eax\n\t"
            "jz 1f\n\t"
            "popl %eax\n\t"
            "xorl %eax,%eax\n\t"
            "1: ret");
}
#elif defined(_M_IX86) && defined(_MSC_VER)
extern "C" __declspec(naked) void skeet_network_fix_proxy_entry() {
    __asm {
        call skeet_network_fix_proxy_prepare
        push dword ptr [eax]
        mov ecx,dword ptr [eax+8]
        mov eax,dword ptr [eax+4]
        test eax,eax
        ret
    }
}
extern "C" __declspec(naked) void skeet_network_fix_singlestep_entry() {
    __asm {
        push dword ptr [esp+4]
        call skeet_network_fix_proxy_guard
        add esp,4
        test eax,eax
        jz unchanged_return
        pop eax
        xor eax,eax
    unchanged_return:
        ret
    }
}
#endif

void NetworkFixProxyRuntime::record_original_proxy(std::uint32_t raw_entry) const {
    // MOVD XMM0 followed by MOVAPS XMM14 only preserves these32 raw bits;
    // the other96 bits are zero and the getter consumes the low word alone.
    proxy_thread.owner = this;
    proxy_thread.continuation = raw_entry;
}
void* NetworkFixProxyRuntime::proxy_entry() const {
#if defined(_M_IX86) || defined(__i386__)
    return reinterpret_cast<void*>(&skeet_network_fix_proxy_entry);
#else
    throw std::logic_error("Network-fix return transfers require their source x86 ABI");
#endif
}
const NetworkFixProxyTransfer* NetworkFixProxyRuntime::prepare_proxy_transfer() const {
#if defined(_M_IX86) || defined(__i386__)
    static_assert(sizeof(void*) == 4 && sizeof(NetworkFixProxyTransfer) == 12);
    if (proxy_thread.owner != this || !proxy_thread.continuation)
        throw std::logic_error("Network-fix return proxy requires its recorded continuation");
    const auto continuation =
        proxy_thread.continuation; //the native getter runs before the engine call
    const auto process = bindings_.process;
    if (!process)
        throw std::logic_error("Network-fix engine entry has not been initialized");
    process(0); //433D6D38, CL0 before any clone lookup/publication
    if (!active_clone_) {
        if (active_proxy_runtime && active_proxy_runtime != this)
            throw std::logic_error(
                "Network-fix singlestep clone already belongs to another source owner");
        auto* variable = reinterpret_cast<std::byte*>(
            std::uintptr_t(bindings_.encoded_singlestep ^ 0x66e946dbu));
        active_clone_ = clone_.data(); //43476410 publication precedes the native memcpy
        std::memcpy(clone_.data(), variable, clone_.size());
        variable = reinterpret_cast<std::byte*>(
            std::uintptr_t(bindings_.encoded_singlestep ^ 0x66e946dbu));
        put(active_clone_, 0x1c, field<void*>(variable, 0x1c));
        variable = reinterpret_cast<std::byte*>(
            std::uintptr_t(bindings_.encoded_singlestep ^ 0x66e946dbu));
        std::memcpy(table_.data(), field<const std::uintptr_t*>(variable, 0) - 1, sizeof table_);
        put(active_clone_, 0, table_.data() + 1); //one RTTI word precedes the21-entry table
        table_[14] =
            reinterpret_cast<std::uintptr_t>(&skeet_network_fix_singlestep_entry); //virtual13
        active_proxy_runtime = this;
    }
    auto* variable =
        reinterpret_cast<std::byte*>(std::uintptr_t(bindings_.encoded_singlestep ^ 0x66e946dbu));
    put(variable, 0x1c, active_clone_);
    proxy_thread.transfer = {
        continuation, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(variable)),
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(active_clone_))};
    return &proxy_thread.transfer;
#else
    throw std::logic_error("Network-fix return transfers require their source x86 ABI");
#endif
}
bool NetworkFixProxyRuntime::finish_singlestep(std::uintptr_t outer_return_site) const {
    if (outer_return_site != bindings_.guarded_return_site)
        return false;
    if (!active_clone_)
        throw std::logic_error("Network-fix singlestep guard requires its initialized clone");
    auto* variable =
        reinterpret_cast<std::byte*>(std::uintptr_t(bindings_.encoded_singlestep ^ 0x66e946dbu));
    put(variable, 0x1c, field<void*>(active_clone_, 0x1c));
    return true;
}
std::uintptr_t process_network_fix(void* receiver, void* supplied_object, std::uintptr_t caller,
                                   const NetworkFixBindings& bindings) {
    if (caller == bindings.proxy_call_site && bindings.proxy_context && bindings.enabled) {
        if (!engine::invoke<bool>(bindings.engine, 82) &&
            !engine::invoke<bool>(bindings.engine, 93) &&
            !engine::invoke<bool>(bindings.engine, 197)) {
            //The veneer supplies the caller's EBP. Its first word is the
            //previous frame, whose+4 slot contains the original continuation.
            auto* const previous_frame = field<std::byte*>(supplied_object, 0);
            const auto previous = field<std::uint32_t>(previous_frame, 4);
            bindings.proxy_runtime.record_original_proxy(previous);
            put(previous_frame, 4, bindings.proxy_runtime.proxy_entry());
        }
    } else if (caller == bindings.sequence_call_site) {
        const auto& offsets = bindings.offsets;
        if (!field<std::uint8_t>(receiver, offsets.byte_9d920e35) &&
            _mm_comige_ss(_mm_set_ss(field<float>(receiver, offsets.scalar_7e9ba7bc)),
                          _mm_set_ss(580.0f)) &&
            !field<std::uint8_t>(receiver, offsets.byte_ac7f2913)) {
            const auto* command = field<void*>(receiver, offsets.pointer_f58e6118);
            if (command) {
                const auto number = field<std::uint32_t>(command, 4);
                client::prediction_history_storage(*bindings.sequence_state)
                    .landing_tags[number % 150u] = number;
            }
        }
    }
    const auto table = *static_cast<void***>(receiver);
    const auto* metadata = static_cast<const engine::VtableHookMetadata*>(table[-2]);
    return reinterpret_cast<engine::MemberFunction<std::uintptr_t>>(metadata->original_table[268])(
        receiver);
}
void bind_network_fix(const NetworkFixBindings& bindings) noexcept {
    active_network_fix = &bindings;
}
#if defined(_M_IX86) || defined(__i386__)
std::uintptr_t __fastcall network_fix_hook(void* receiver, void* supplied_object) {
#else
std::uintptr_t network_fix_hook(void* receiver, void* supplied_object) {
#endif
    return process_network_fix(receiver, supplied_object,
                               reinterpret_cast<std::uintptr_t>(_ReturnAddress()),
                               *active_network_fix);
}
} //namespace skeet::misc
