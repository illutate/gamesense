#include "skeet/render/device_listener.hpp"
#include <new>

namespace skeet::ui {
RendererContainer* RendererDeviceListener::renderer() const noexcept {
    if (const auto* value = std::get_if<RendererContainer*>(&renderer_))
        return *value;
    return *std::get<RendererContainer* const*>(renderer_);
}
bool RendererDeviceListener::follows(RendererContainer* const& renderer) const noexcept {
    const auto* cell = std::get_if<RendererContainer* const*>(&renderer_);
    return cell && *cell == &renderer;
}
void RendererDeviceListener::restore_base_vtable() noexcept {
    const auto binding = renderer_;
    ::new (static_cast<void*>(this)) RendererDeviceListener(binding);
}
void RendererDeviceListener::release_device_objects() {
    // Native global4347690C's +8 context can be zero after renderer teardown.
    if (auto* owner = renderer())
        if (auto* backend = owner->backend_if_alive())
            backend->release_device_objects();
}
void RendererDeviceListener::set_device(IDirect3DDevice9* device, std::uintptr_t, std::uintptr_t) {
    // This is only the borrowed context+C8 assignment: no AddRef or Release.
    if (auto* owner = renderer())
        if (auto* backend = owner->backend_if_alive())
            backend->set_device(device);
}
// The native slot is exactly RET 8; both notifications are intentionally unused.
void RendererDeviceListener::notification(std::uintptr_t, std::uintptr_t) {}
} // namespace skeet::ui
