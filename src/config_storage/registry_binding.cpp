#include "skeet/config.hpp"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace skeet {
struct ConfigRegistries::BlobBinding {
    ConfigDescriptor* native;
    ConfigBlob* canonical;
};
namespace {
struct BlobIndexEntry {
    ConfigRegistries* registry;
    ConfigDescriptor* native;
    ConfigBlobValue* value;
};
// These indices contain identity pointers only. The registry owns each
// binding, while the actual native ring and buffer remain their own storage.
struct RegistryIndex {
    std::unordered_map<ConfigLink*, ConfigRegistries*> descriptors;
    std::unordered_map<HotkeyLink*, ConfigRegistries*> hotkeys;
    std::unordered_map<const ConfigDescriptor*, BlobIndexEntry> blobs;
};
RegistryIndex& index() {
    static RegistryIndex value;
    return value;
}
}
ConfigRegistries::ConfigRegistries(ConfigLink& configs, HotkeyLink& keys)
    : ConfigRegistries(configs, keys, false) {}
ConfigRegistries::ConfigRegistries(ConfigLink& configs, HotkeyLink& keys, bool standalone)
    : descriptors(configs), hotkeys(keys), standalone_(standalone) {
    auto& views = index();
    if (views.descriptors.contains(&configs) || views.hotkeys.contains(&keys))
        throw std::logic_error("Configuration sentinels already have their canonical source view");
    views.descriptors.emplace(&configs, this);
    try {
        views.hotkeys.emplace(&keys, this);
    } catch (...) {
        views.descriptors.erase(&configs);
        throw;
    }
}
ConfigRegistries::~ConfigRegistries() {
    auto& views = index();
    for (const auto& binding : blobs_) {
        views.blobs.erase(binding->native);
        views.blobs.erase(binding->canonical);
    }
    views.hotkeys.erase(&hotkeys);
    views.descriptors.erase(&descriptors);
}
ConfigRegistries& ConfigRegistries::for_descriptors(ConfigLink& sentinel) {
    const auto found = index().descriptors.find(&sentinel);
    if (found == index().descriptors.end())
        throw std::logic_error(
            "Configuration registration requires its explicit canonical registry view");
    return *found->second;
}
void ConfigRegistries::bind_blob(ConfigDescriptor& native, ConfigBlob& canonical) {
    auto& entries = index().blobs;
    const auto previous = entries.find(&native);
    if (previous != entries.end()) {
        if (previous->second.registry == this && previous->second.value == &canonical.value)
            return;
        throw std::logic_error("Native configuration descriptor already has another blob view");
    }
    if (entries.contains(&canonical))
        throw std::logic_error("Canonical blob view is already bound to another descriptor");
    if ((!standalone_ || &native != static_cast<ConfigDescriptor*>(&canonical)) &&
        !canonical.value.native_cells_are(reinterpret_cast<std::byte*>(&native) +
                                          sizeof(ConfigDescriptor)))
        throw std::invalid_argument(
            "Native blob view must reference the actual pointer triple immediately after its header");
    auto binding = std::make_unique<BlobBinding>(BlobBinding{&native, &canonical});
    blobs_.push_back(std::move(binding));
    try {
        entries.emplace(&native, BlobIndexEntry{this, &native, &canonical.value});
        if (&native != static_cast<ConfigDescriptor*>(&canonical))
            entries.emplace(&canonical, BlobIndexEntry{this, &native, &canonical.value});
    } catch (...) {
        entries.erase(&native);
        entries.erase(&canonical);
        blobs_.pop_back();
        throw;
    }
}
void ConfigRegistries::unbind_blob(ConfigBlob& canonical) noexcept {
    const auto found = std::find_if(blobs_.begin(), blobs_.end(), [&](const auto& binding) {
        return binding->canonical == &canonical;
    });
    if (found == blobs_.end())
        return;
    index().blobs.erase((*found)->native);
    index().blobs.erase(&canonical);
    blobs_.erase(found);
}
ConfigBlob::~ConfigBlob() {
    const auto found = index().blobs.find(this);
    if (found != index().blobs.end())
        found->second.registry->unbind_blob(*this);
}
ConfigDescriptor& config_native_header(ConfigDescriptor& descriptor) noexcept {
    const auto found = index().blobs.find(&descriptor);
    return found == index().blobs.end() ? descriptor : *found->second.native;
}
const ConfigDescriptor& config_native_header(const ConfigDescriptor& descriptor) noexcept {
    return config_native_header(const_cast<ConfigDescriptor&>(descriptor));
}
ConfigBlobValue* config_blob_value(const ConfigDescriptor& descriptor) noexcept {
    const auto found = index().blobs.find(&descriptor);
    return found == index().blobs.end() ? nullptr : found->second.value;
}
} // namespace skeet
