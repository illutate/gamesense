#include "skeet/config_storage/config_stream_view.hpp"
#include "skeet/config_storage/preserved.hpp"
#include "skeet/script/shutdown.hpp"
#include <stdexcept>

namespace skeet {
ConfigStreamView ConfigStreamView::native_vector(const NativeConfigVector& cells) noexcept {
    return {Route::native, &cells};
}
ConfigStreamView ConfigStreamView::script_state(const script::ShutdownState& state) noexcept {
    return {Route::script, &state};
}
ConfigStreamView ConfigStreamView::snapshot(std::span<const std::byte> bytes) noexcept {
    return {Route::snapshot, nullptr, bytes};
}
ConfigStreamView ConfigStreamView::unbound() noexcept {
    return {Route::unbound, nullptr};
}
std::span<const std::byte> ConfigStreamView::bytes() const {
    switch (route_) {
    case Route::native: {
        const auto& cells = *static_cast<const NativeConfigVector*>(owner_);
        if (!cells.begin) {
            if (cells.end || cells.capacity)
                throw std::logic_error(
                    "Configuration baseline has no allocation for its published range");
            return {};
        }
        if (cells.end < cells.begin || cells.capacity < cells.end)
            throw std::logic_error("Configuration baseline has an invalid native vector range");
        return {reinterpret_cast<const std::byte*>(std::uintptr_t(cells.begin)),
                std::size_t(cells.end - cells.begin)};
    }
    case Route::script:
        return static_cast<const script::ShutdownState*>(owner_)->current_config;
    case Route::snapshot:
        return captured_;
    case Route::unbound:
        throw std::logic_error("Configuration stream owner has not been bound");
    }
    throw std::logic_error("Unknown configuration stream route");
}
bool ConfigStreamView::is_unbound() const noexcept {
    return route_ == Route::unbound;
}
bool ConfigStreamView::is_snapshot() const noexcept {
    return route_ == Route::snapshot;
}
bool ConfigStreamView::is_script_state(const script::ShutdownState& state) const noexcept {
    return route_ == Route::script && owner_ == &state;
}
bool ConfigStreamView::same_source(const ConfigStreamView& other) const noexcept {
    if (route_ != other.route_ || owner_ != other.owner_)
        return false;
    return route_ != Route::snapshot || (captured_.data() == other.captured_.data() &&
                                         captured_.size() == other.captured_.size());
}
} //namespace skeet
