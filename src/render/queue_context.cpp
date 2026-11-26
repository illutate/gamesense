#include "skeet/render_queue.hpp"
#include "skeet/render/backend_container.hpp"

namespace skeet::ui {
Point RenderQueue::screen() const {
    // Like native433F49E6, this getter requires the supplied queue's live
    // embedded renderer/context. Standalone command writers have no viewport.
    const auto& context = renderer_->backend();
    return {context.screen_width, context.screen_height};
}
} //namespace skeet::ui
