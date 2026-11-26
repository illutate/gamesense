#include "skeet/core.hpp"
#include <stdexcept>

namespace skeet {
// Original dispatch: 0x434721C4. Its external call is ntdll!RtlTestBit.
bool hotkey_active(const HotkeyBinding& binding, InputBitmap input) {
    switch (binding.mode) {
    case HotkeyMode::always: // 0x433422C6
        return true;
    case HotkeyMode::toggle: // 0x433AE8A5
        return (binding.packed & 2) != 0;
    case HotkeyMode::hold: // 0x43386E1B
    case HotkeyMode::inverse_hold: { // 0x433A1464
        const auto bit = binding.packed >> 2;
        if (bit / 32 >= input.words.size())
            throw std::out_of_range("Activation bit is outside bitmap storage");
        // RtlTestBit reads the buffer directly; SizeOfBitMap is not a bounds gate.
        const bool set = ((input.words[bit / 32] >> (bit % 32)) & 1) != 0;
        return binding.mode == HotkeyMode::hold ? set : !set;
    }
    }
    throw std::invalid_argument("Unknown activation mode");
}

} // namespace skeet
