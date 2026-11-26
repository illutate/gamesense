#include "skeet/engine/initialization_failure.hpp"
#include <cstring>

namespace skeet::engine {
std::int32_t fail_initialization(InitializationFailureState state,
                                 TerminateInitializationThread terminate_thread,
                                 std::int32_t exit_code) {
    // Native 0x4337F065 reads both image fields before clearing the buffer.
    const auto size = state.image_size;
    const auto base = state.image_base;
    if (size)
        std::memset(reinterpret_cast<void*>(base), 0, size);
    state.requested_mode = 0x100;
    return terminate_thread(reinterpret_cast<void*>(std::intptr_t(-2)), exit_code);
}
}
