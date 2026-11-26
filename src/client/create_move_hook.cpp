#include "skeet/client/command_orchestrator.hpp"

//A C-linkage bridge gives both supported x86 assemblers one stable concrete
//source symbol. The outer saved-EBX address remains unchanged while these four
//arguments are marshalled; the bridge calls the complete433D2E40 source body.
extern "C" std::uint8_t __cdecl skeet_create_move_dispatch(void* player, std::uint8_t* send,
                                                           float sample_time,
                                                           skeet::client::Command* command) {
    return skeet::client::create_move_command_hook(player, send, sample_time, command);
}

namespace skeet::client {
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
__attribute__((naked)) std::uint8_t __fastcall player_create_move_hook(void*, void*, float,
                                                                       Command*) {
    __asm__("pushl %ebp\n\t"
            "movl %esp, %ebp\n\t"
            "pushl %ebx\n\t"
            "movl %esp, %edx\n\t"
            "pushl 12(%ebp)\n\t"
            "pushl 8(%ebp)\n\t"
            "pushl %edx\n\t"
            "pushl %ecx\n\t"
            "call _skeet_create_move_dispatch\n\t"
            "addl $16, %esp\n\t"
            "popl %ebx\n\t"
            "movl %ebp, %esp\n\t"
            "popl %ebp\n\t"
            "retl $8");
}
#elif defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) std::uint8_t __fastcall player_create_move_hook(void*, void*, float, Command*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        mov edx,esp
        push dword ptr [ebp+12]
        push dword ptr [ebp+8]
        push edx
        push ecx
        call skeet_create_move_dispatch
        add esp,16
        pop ebx
        mov esp,ebp
        pop ebp
        ret 8
    }
}
#else
#error The native4334F53D saved-EBX handoff requires a32-bit x86 target.
#endif
} //namespace skeet::client
