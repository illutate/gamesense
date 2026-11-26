#include "skeet/render/frame_runtime.hpp"
#include "skeet/engine/interface_call.hpp"

namespace skeet::ui {
void FrameRuntime::render() { // 43384182..433841C8
    if (shutdown_ || !bridge_ || !*bridge_)
        return;
    auto* object = *bridge_;
    engine::invoke<void>(object, 1);
    engine::invoke<void>(object, 10);
    if (!shutdown_)
        renderer_.draw();
    engine::invoke<void>(object, 11);
    engine::invoke<void>(object, 2);
}
} // namespace skeet::ui
