#include "skeet/render/renderer_assets.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/loaded_modules.hpp"
#include "skeet/engine/pe_exports.hpp"
#include "skeet/engine/netvars.hpp"
#include <array>
#include <cstring>
#include <span>
#include <stdexcept>

namespace skeet::ui {
namespace {
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
bool contains(const std::byte* module, const void* pointer, std::size_t size, std::uint32_t rva,
              std::uint32_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(module) + rva;
    return address >= begin && size <= length && address - begin <= length - size;
}
bool matches(const std::byte* code, std::span<const int> pattern) {
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] >= 0 && static_cast<unsigned char>(code[i]) != pattern[i])
            return false;
    return true;
}
std::uintptr_t code_entry(std::byte* panorama, std::uint32_t rva,
                          std::initializer_list<int> pattern) {
    auto* address = panorama + rva;
    require(contains(panorama, address, pattern.size(), 0x1000, 0x1E5C8D) &&
                matches(address, {pattern.begin(), pattern.size()}),
            "Panorama image-codec/job entry does not match the supported build");
    return reinterpret_cast<std::uintptr_t>(address);
}
ImageDecoderBindings decoder_bindings(engine::NativeDataImage& data,
                                      const engine::LoadedModuleEnvironment& modules,
                                      engine::NativeImageEnvironment images) {
    auto* panorama = data.native_cell<std::byte*>(0x43469234);
    require(panorama && read<std::uint16_t>(panorama) == 0x5A4D &&
                read<std::uint32_t>(panorama + 0x3C) == 0x160,
            "Required Panorama image-codec module is not loaded");
    const auto* nt = panorama + 0x160;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 8 && read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x651F4636 &&
                read<std::uint32_t>(nt + 80) == 0x2C6000 &&
                read<std::uint32_t>(nt + 88) == 0x26E92E,
            "Unsupported Panorama image-codec build identity");
    ImageDecoderBindings result;
    // These build RVAs are independently named by the supplied loader's
    // extra() binding phase and the actual working function bodies. They are
    // genuine Panorama library entries, not recovered client implementations.
    result.enqueue =
        code_entry(panorama, 0x9E220,
                   {0x55, 0x8B, 0xEC, 0x51, 0x8B, 0xC1, 0x53, 0x56, 0x57, 0x8B, 0x98, 0x88, 0, 0,
                    0,    0x89, 0x45, 0xFC, 0x8D, 0x4B, 4,    0xFF, 0x15, -1,   -1,   -1,   -1});
    result.execute =
        code_entry(panorama, 0x9A320,
                   {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0x53, 0x8B, 0xD9, 0x8B, 0x0D, -1, -1,
                    -1,   -1,   0x56, 0x89, 0x5D, 0xF4, 0x8B, 0x81, 0x0C, 0x10, 0,    0});
    result.jpeg = code_entry(panorama, 0xA1B80, {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x20, 0x89, 0x4D,
                                                 0xF4, 0x0F, 0x57, 0xC0, 0x8B, 0x0D, -1,   -1,
                                                 -1,   -1,   0x8B, 0xC2, 0x53, 0x56, 0x0F, 0x11});
    result.png = code_entry(panorama, 0xA3C80,
                            {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x2C, 0x53, 0x56, 0x57, 0x6A, 8,  0x8B,
                             0xF1, 0x8B, 0xFA, 0x6A, 0,    0x56, 0xE8, -1,   -1,   -1,   -1, 0x83});
    result.svg = code_entry(panorama, 0xAB6F0,
                            {0x55, 0x8B, 0xEC, 0x81, 0xEC, 0xB8, 0,    0, 0,    0x53, 0x8B, 0x5D,
                             0x0C, 0x56, 0x57, 0x8B, 0x7D, 0x10, 0x8B, 3, 0x52, 0x89, 0x45, 0xFC});
    auto* tier0 = static_cast<std::byte*>(engine::find_loaded_module(
        modules, engine::loaded_module_name_hash(u"tier0.dll", 9), true, true));
    require(tier0 != nullptr, "Image codecs require the already-loaded tier0.dll");
    auto* slot =
        engine::resolve_pe_export(tier0, engine::netvar_name_hash("g_pMemAlloc"), 0, true, images);
    require(slot != nullptr, "tier0.dll does not export g_pMemAlloc");
    result.allocator = read<void*>(slot);
    require(result.allocator != nullptr, "tier0 g_pMemAlloc is not initialized");
    result.render_context_source = data.native_cell<void*>(0x43468E98);
    require(result.render_context_source != nullptr, "Image codecs require PanoramaUIEngine001");
    return result;
}
IconBindings checked_bindings(client::ClientRuntimeStorage& storage, StartupRenderer& renderer) {
    if (storage.data().native_cell<RendererContainer*>(0x4347690C) != &renderer.renderer() ||
        &renderer.dpi() != &storage.menu_startup_data().dpi)
        throw std::invalid_argument(
            "Renderer assets must borrow the canonical renderer and menu DPI");
    return initialize_build_icon_bindings(storage.data());
}
IconBindings checked_bindings(client::ClientRuntimeStorage& storage, StartupRenderer& renderer,
                              IconBindings bindings) {
    auto& data = storage.data();
    const auto& images = bindings.images;
    auto* const filesystem = data.native_cell<std::byte*>(0x43467F68);
    if (data.native_cell<RendererContainer*>(0x4347690C) != &renderer.renderer() ||
        &renderer.dpi() != &storage.menu_startup_data().dpi || !filesystem ||
        bindings.filesystem != filesystem + 4 ||
        &bindings.item_system != &data.native_cell<void*>(0x43476424) ||
        &bindings.item_system_factory !=
            &data.native_cell<IconBindings::ItemSystemFactory>(0x43468B9C) ||
        !bindings.item_system_factory || !images.render_context_source || !images.allocator ||
        !images.enqueue || !images.execute || !images.svg || !images.png || !images.jpeg ||
        images.render_context_source != data.native_cell<void*>(0x43468E98) ||
        images.allocator != data.native_cell<void*>(0x43467204) ||
        images.enqueue != data.native_cell<std::uintptr_t>(0x4346D67C) ||
        images.execute != data.native_cell<std::uintptr_t>(0x4346D680) ||
        images.jpeg != data.native_cell<std::uintptr_t>(0x4346D684) ||
        images.png != data.native_cell<std::uintptr_t>(0x4346D688) ||
        images.svg != data.native_cell<std::uintptr_t>(0x4346D68C))
        throw std::invalid_argument(
            "Renderer assets require the complete canonical acquired engine bindings");
    return bindings;
}
}
IconBindings initialize_build_icon_bindings(engine::NativeDataImage& data,
                                            const engine::LoadedModuleEnvironment& modules,
                                            engine::NativeImageEnvironment images) {
    auto* filesystem = data.native_cell<std::byte*>(0x43467F68);
    require(filesystem != nullptr, "Renderer icons require VFileSystem017");
    auto& factory = data.native_cell<IconBindings::ItemSystemFactory>(0x43468B9C);
    require(factory != nullptr, "Renderer weapon icons require the actual ItemSystem factory");
    const auto images_bound = decoder_bindings(data, modules, images);
    // Publish genuine acquired functions for the same native-data consumers.
    // ItemSystem remains a live reference shared with4341F57F's publication.
    data.native_cell<void*>(0x43467204) = images_bound.allocator;
    data.native_cell<std::uintptr_t>(0x4346D67C) = images_bound.enqueue;
    data.native_cell<std::uintptr_t>(0x4346D680) = images_bound.execute;
    data.native_cell<std::uintptr_t>(0x4346D684) = images_bound.jpeg;
    data.native_cell<std::uintptr_t>(0x4346D688) = images_bound.png;
    data.native_cell<std::uintptr_t>(0x4346D68C) = images_bound.svg;
    return {filesystem + 4, data.native_cell<void*>(0x43476424), factory, images_bound};
}
IconBindings initialize_build_icon_bindings(engine::NativeDataImage& data) {
    return initialize_build_icon_bindings(data, engine::native_loaded_module_environment(),
                                          engine::native_image_environment());
}
struct RendererAssets::Data {
    client::ClientRuntimeStorage& storage;
    StartupRenderer* startup = nullptr;
    std::optional<RendererDeviceListener> owned_listener;
    RendererDeviceListener* listener;
    IconCache cache;
    StartupIcons icons;
    explicit Data(client::ClientRuntimeStorage& owner)
        : storage(owner),
          owned_listener(std::in_place, owner.data().native_cell<RendererContainer*>(0x4347690C)),
          listener(&*owned_listener), icons{owner.data().native_cell<std::int32_t>(0x43468E3C),
                                            owner.data().native_cell<std::int32_t>(0x4346842C),
                                            owner.data().native_cell<std::int32_t>(0x43467DA8)} {}
    Data(client::ClientRuntimeStorage& owner, StartupRenderer& renderer)
        : Data(owner, renderer, checked_bindings(owner, renderer)) {}
    Data(client::ClientRuntimeStorage& owner, StartupRenderer& renderer, IconBindings acquired)
        : storage(owner), startup(&renderer), listener(&renderer.device_listener()),
          cache(renderer.renderer().backend(), checked_bindings(owner, renderer, acquired)),
          icons{owner.data().native_cell<std::int32_t>(0x43468E3C),
                owner.data().native_cell<std::int32_t>(0x4346842C),
                owner.data().native_cell<std::int32_t>(0x43467DA8)} {}
};
RendererAssets::RendererAssets(client::ClientRuntimeStorage& storage)
    : data_(std::make_unique<Data>(storage)) {}
RendererAssets::RendererAssets(client::ClientRuntimeStorage& storage, StartupRenderer& renderer)
    : data_(std::make_unique<Data>(storage, renderer)) {}
RendererAssets::RendererAssets(client::ClientRuntimeStorage& storage, StartupRenderer& renderer,
                               IconBindings bindings)
    : data_(std::make_unique<Data>(storage, renderer, bindings)) {}
void RendererAssets::bind_renderer(StartupRenderer& renderer) {
    bind_renderer(renderer, checked_bindings(data_->storage, renderer));
}
void RendererAssets::bind_renderer(StartupRenderer& renderer, IconBindings bindings) {
    if (data_->startup)
        throw std::logic_error("The canonical renderer assets were already bound");
    if (&renderer.device_listener() != data_->listener)
        throw std::invalid_argument("Late renderer must borrow the same early device listener");
    const auto acquired = checked_bindings(data_->storage, renderer, bindings);
    data_->cache.bind_runtime(renderer.renderer().backend(), acquired);
    data_->startup = &renderer;
}
bool RendererAssets::renderer_bound() const noexcept {
    return data_->startup != nullptr;
}
RendererAssets::~RendererAssets() = default;
client::ClientRuntimeStorage& RendererAssets::storage() const noexcept {
    return data_->storage;
}
StartupRenderer& RendererAssets::startup_renderer() const {
    if (!data_->startup)
        throw std::logic_error("Renderer assets have not acquired their late renderer");
    return *data_->startup;
}
RendererContainer& RendererAssets::renderer() const {
    return startup_renderer().renderer();
}
RendererDeviceListener& RendererAssets::device_listener() const noexcept {
    return *data_->listener;
}
IconCache& RendererAssets::icon_cache() noexcept {
    return data_->cache;
}
StartupIcons& RendererAssets::startup_icons() noexcept {
    return data_->icons;
}
}
