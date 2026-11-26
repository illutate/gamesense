#include "skeet/config.hpp"
#include "skeet/config_storage/native_blob.hpp"
#include <stdexcept>
#include <utility>

namespace skeet {
namespace {
[[noreturn]] void unbound_blob() {
    throw std::logic_error("Configuration blob has no explicitly selected storage owner");
}
}
ConfigBlobValue::ConfigBlobValue(SemanticBlobStorageTag) : storage_(std::in_place_type<Owned>) {}
void ConfigBlobValue::bind_native(NativeBlobBytes& bytes) {
    if (!std::holds_alternative<std::monostate>(storage_))
        throw std::logic_error("Configuration blob storage cannot be rebound or switched");
    storage_.emplace<Native>(bytes);
}
bool ConfigBlobValue::native_cells_are(const void* cells) const noexcept {
    const auto* native = std::get_if<Native>(&storage_);
    return native && &native->get().cells == cells;
}
void ConfigBlobValue::initialize_empty() {
    if (auto* native = std::get_if<Native>(&storage_)) {
        native->get().reset_empty();
        return;
    }
    if (auto* owned = std::get_if<Owned>(&storage_)) {
        owned->clear();
        return;
    }
    unbound_blob();
}
std::byte* ConfigBlobValue::data() {
    if (auto* native = std::get_if<Native>(&storage_))
        return native->get().data();
    if (auto* owned = std::get_if<Owned>(&storage_))
        return owned->data();
    unbound_blob();
}
const std::byte* ConfigBlobValue::data() const {
    if (const auto* native = std::get_if<Native>(&storage_)) {
        const auto& bytes = native->get();
        return bytes.data();
    }
    if (const auto* owned = std::get_if<Owned>(&storage_))
        return owned->data();
    unbound_blob();
}
std::size_t ConfigBlobValue::size() const {
    if (const auto* native = std::get_if<Native>(&storage_))
        return native->get().size();
    if (const auto* owned = std::get_if<Owned>(&storage_))
        return owned->size();
    unbound_blob();
}
bool ConfigBlobValue::empty() const {
    if (const auto* native = std::get_if<Native>(&storage_))
        return native->get().empty();
    if (const auto* owned = std::get_if<Owned>(&storage_))
        return owned->empty();
    unbound_blob();
}
void ConfigBlobValue::assign(std::span<const std::byte> value) {
    if (auto* native = std::get_if<Native>(&storage_)) {
        native->get().assign(value);
        return;
    }
    if (auto* owned = std::get_if<Owned>(&storage_)) {
        owned->assign(value.begin(), value.end());
        return;
    }
    unbound_blob();
}
void ConfigBlobValue::resize(std::size_t count) {
    if (auto* native = std::get_if<Native>(&storage_)) {
        native->get().resize(count);
        return;
    }
    if (auto* owned = std::get_if<Owned>(&storage_)) {
        owned->resize(count);
        return;
    }
    unbound_blob();
}
void ConfigBlobValue::clear() {
    if (auto* native = std::get_if<Native>(&storage_)) {
        native->get().clear();
        return;
    }
    if (auto* owned = std::get_if<Owned>(&storage_)) {
        owned->clear();
        return;
    }
    unbound_blob();
}
ConfigBlobValue& ConfigBlobValue::operator=(const std::vector<std::byte>& value) {
    assign(value);
    return *this;
}
ConfigBlobValue& ConfigBlobValue::operator=(std::vector<std::byte>&& value) {
    if (auto* native = std::get_if<Native>(&storage_)) {
        native->get().assign(value);
        return *this;
    }
    if (auto* owned = std::get_if<Owned>(&storage_)) {
        *owned = std::move(value);
        return *this;
    }
    unbound_blob();
}
} // namespace skeet
