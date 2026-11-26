#include "skeet/render/startup_renderer.hpp"
#include "skeet/engine/overlay_build.hpp"
#include "skeet/menu_window.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <stdexcept>

namespace skeet::ui {
namespace {
constexpr std::array<StartupFontRecipe, 7> initial_fonts{{{u"Verdana", 9, 2},
                                                          {u"Verdana", 9, 3},
                                                          {u"Calibri", 24, 3},
                                                          {u"Segoe UI Symbol", 12, 2},
                                                          {u"Small Fonts", 7, 4},
                                                          {u"Segoe UI Symbol", 24, 2},
                                                          {u"Malgun Gothic", 10, 2}}};
template <class T> T read(const std::byte* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof value);
    return value;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Region {
    std::byte* first;
    std::size_t size;
};
struct Image {
    std::byte* base;
    Region text{}, rdata{}, data{};
    static bool contains(Region region, const void* pointer, std::size_t size) noexcept {
        const auto at = reinterpret_cast<std::uintptr_t>(pointer);
        const auto begin = reinterpret_cast<std::uintptr_t>(region.first);
        return at >= begin && size <= region.size && at - begin <= region.size - size;
    }
};
Image checked_image(std::byte* base, std::uint32_t nt_offset, std::uint32_t timestamp,
                    std::uint32_t image_size, std::uint32_t checksum) {
    require(base && read<std::uint16_t>(base) == 0x5A4D,
            "Renderer module is not a loaded DOS image");
    require(read<std::uint32_t>(base + 0x3C) == nt_offset,
            "Unsupported renderer module NT-header location");
    const auto* nt = base + nt_offset;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 20) == 0xE0 && read<std::uint16_t>(nt + 24) == 0x10B,
            "Renderer bindings require the observed PE32 module layout");
    require(read<std::uint32_t>(nt + 8) == timestamp &&
                read<std::uint32_t>(nt + 80) == image_size &&
                read<std::uint32_t>(nt + 88) == checksum,
            "Unsupported renderer module build identity");
    Image result{base};
    const auto count = read<std::uint16_t>(nt + 6);
    require(count == 6, "Unsupported renderer module section count");
    for (std::uint16_t index = 0; index < count; ++index) {
        const auto* section = nt + 0xF8 + index * 40;
        const auto rva = read<std::uint32_t>(section + 12), size = read<std::uint32_t>(section + 8);
        require(rva <= image_size && size <= image_size - rva,
                "Renderer PE section exceeds its loaded image");
        const auto flags = read<std::uint32_t>(section + 36);
        if (!std::memcmp(section, ".text\0\0\0", 8)) {
            require((flags & 0x22000000u) == 0x20000000u,
                    "Renderer text is not executable and nondiscardable");
            result.text = {base + rva, size};
        } else if (!std::memcmp(section, ".rdata\0\0", 8))
            result.rdata = {base + rva, size};
        else if (!std::memcmp(section, ".data\0\0\0", 8))
            result.data = {base + rva, size};
    }
    require(result.text.first && result.rdata.first && result.data.first,
            "Renderer PE sections are missing");
    return result;
}
bool matches(const std::byte* code, std::span<const int> pattern) {
    for (std::size_t i = 0; i < pattern.size(); ++i)
        if (pattern[i] >= 0 && static_cast<unsigned char>(code[i]) != pattern[i])
            return false;
    return true;
}
IDirect3DDevice9** shader_device_slot(const engine::NativeDataImage& data) {
    // ShaderDevice001::IsUsingGraphics: XOR EAX,EAX; CMP [device],EAX;
    // SETNE AL; RET. Decode its live absolute operand, rather than retaining
    // the old module address5BA762C0 or the working capture's6A8262C0.
    const auto image = checked_image(data.native_cell<std::byte*>(0x4346A604), 0x140, 0x63DC1158,
                                     0xCE000, 0x9DC6B);
    auto* object = data.native_cell<void*>(0x4346A33C);
    require(Image::contains(image.data, object, sizeof(void*)),
            "ShaderDevice001 is outside its loaded module data");
    auto** table = *static_cast<void***>(object);
    require(Image::contains(image.rdata, table, 7 * sizeof(void*)),
            "ShaderDevice001 vtable is outside module RTTI data");
    const auto* code = static_cast<const std::byte*>(table[6]);
    static constexpr std::array<int, 12> pattern{0x33, 0xC0, 0x39, 0x05, -1,   -1,
                                                 -1,   -1,   0x0F, 0x95, 0xC0, 0xC3};
    require(Image::contains(image.text, code, pattern.size()) && matches(code, pattern),
            "ShaderDevice001 virtual6 is not the proven graphics-state accessor");
    auto** slot =
        reinterpret_cast<IDirect3DDevice9**>(std::uintptr_t(read<std::uint32_t>(code + 4)));
    require(Image::contains(image.data, slot, sizeof(void*)),
            "Shader device operand is outside module data");
    require(*slot != nullptr, "The game D3D9 device has not been created");
    return slot;
}
void** overlay_bridge_holder(const engine::NativeDataImage& data) {
    auto* module = data.native_cell<std::byte*>(0x43468D08);
    if (!module)
        return nullptr; //43384182 explicitly accepts a missing bridge.
    const auto identity = engine::require_overlay_build_identity(module);
    const auto image =
        checked_image(module, 0x130, identity.timestamp, 0x190000, identity.checksum);
    // CD3D9Renderer's genuine frame producer reads the active renderer slot,
    // tests it, and publishes EBX to that SAME slot before copying+A4/+A8.
    static constexpr std::array<int, 32> pattern{
        0x8B, 0x0D, -1, -1, -1, -1, 0x85, 0xC9, 0x0F, 0x85, -1,   -1, -1, -1, 0xDD, 0xD8,
        0x89, 0x1D, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x83, 0xA4, 0,  0,  0,  0xF3, 0x0F};
    const std::byte* found = nullptr;
    for (std::size_t i = 0; i + pattern.size() <= image.text.size; ++i) {
        const auto* code = image.text.first + i;
        if (!matches(code, pattern))
            continue;
        require(!found, "Active overlay renderer producer is ambiguous");
        found = code;
    }
    require(found, "Active overlay renderer producer was not found");
    const auto address = read<std::uint32_t>(found + 2);
    require(address == read<std::uint32_t>(found + 18),
            "Overlay renderer load/store operands differ");
    auto** holder = reinterpret_cast<void**>(std::uintptr_t(address));
    require(Image::contains(image.data, holder, 12),
            "Overlay renderer holder is outside module data");
    // Its current value is nullable and remains owned by Steam. FrameRuntime
    // rereads the holder each invocation; no captured object is instantiated.
    return holder;
}
std::shared_ptr<engine::NativeDataImage>
checked_owner(std::shared_ptr<engine::NativeDataImage> data, GuiDpiState& dpi) {
    auto& image = engine::require_native_data_image(data);
    if (&dpi.selection != &image.native_cell<std::int32_t>(0x43475A94))
        throw std::invalid_argument("Renderer and menu must share the canonical DPI selection");
    if (image.native_cell<RendererContainer*>(0x4347690C))
        throw std::logic_error("The canonical startup renderer already exists");
    return data;
}
void initialize_window_size(RendererBackend& backend) { //4348F887..4348F8D1
    D3DDEVICE_CREATION_PARAMETERS parameters{};
    (void)backend.device()->GetCreationParameters(&parameters);
    RECT rectangle;
    if (GetClientRect(parameters.hFocusWindow, &rectangle)) {
        backend.screen_width = std::bit_cast<std::int32_t>(std::uint32_t(rectangle.right) -
                                                           std::uint32_t(rectangle.left));
        backend.screen_height = std::bit_cast<std::int32_t>(std::uint32_t(rectangle.bottom) -
                                                            std::uint32_t(rectangle.top));
    } else {
        backend.screen_width = 1024;
        backend.screen_height = 768;
    }
}
}
std::span<const StartupFontRecipe, 7> startup_font_recipes() noexcept {
    return initial_fonts;
}
void initialize_startup_fonts(RendererContainer& renderer) { //font portion4348F2C1..4348FA7D
    std::array<FontDefinition, 7> definitions;
    for (std::size_t i = 0; i < definitions.size(); ++i)
        definitions[i] = {std::u16string(initial_fonts[i].face), initial_fonts[i].height,
                          initial_fonts[i].flags};
    renderer.configure_fonts(definitions);
    // Native makes these edits AFTER all seven default FontRuntime objects.
    // They affect subsequent scaled fonts; the defaults above stay intact.
    renderer.font_definitions[4] = {u"Verdana", 7, 2};
    renderer.font_definitions[6].height = 9;
}
BuildRendererBindings discover_build_renderer_bindings(const engine::NativeDataImage& data) {
    return {shader_device_slot(data), overlay_bridge_holder(data)};
}
StartupRenderer::StartupRenderer(std::shared_ptr<engine::NativeDataImage> data, GuiDpiState& dpi)
    : data_(checked_owner(std::move(data), dpi)), dpi_(dpi),
      bindings_(discover_build_renderer_bindings(*data_)), renderer_(*bindings_.device_slot),
      owned_listener_(std::in_place, renderer_), listener_(&*owned_listener_),
      frame_(renderer_, bindings_.bridge_holder, data_->native_cell<std::uint32_t>(0x4347C9FC)) {
    initialize();
}
StartupRenderer::StartupRenderer(std::shared_ptr<engine::NativeDataImage> data, GuiDpiState& dpi,
                                 RendererDeviceListener& listener)
    : data_(checked_owner(std::move(data), dpi)), dpi_(dpi),
      bindings_(discover_build_renderer_bindings(*data_)), renderer_(*bindings_.device_slot),
      listener_(&listener),
      frame_(renderer_, bindings_.bridge_holder, data_->native_cell<std::uint32_t>(0x4347C9FC)) {
    if (!listener.follows(data_->native_cell<RendererContainer*>(0x4347690C)))
        throw std::invalid_argument(
            "Startup renderer must borrow its canonical early device listener");
    initialize();
}
void StartupRenderer::initialize() {
    initialize_window_size(renderer_.backend());
    initialize_startup_fonts(renderer_);
    data_->native_cell<void**>(0x43468350) = bindings_.bridge_holder;
    data_->native_cell<RendererContainer*>(0x4347690C) = &renderer_;
}
StartupRenderer::~StartupRenderer() {
    // Existing post-initialization/unload owns device-listener registration.
    // This owner must outlive that registration and the main menu bindings.
    auto& current = data_->native_cell<RendererContainer*>(0x4347690C);
    if (current == &renderer_) {
        renderer_.destroy(); //43336C83: destroy the front before clearing it.
        current = nullptr;
    }
}
}
