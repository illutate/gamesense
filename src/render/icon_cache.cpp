#include "skeet/render/icon_cache.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/esp/weapon_icon.hpp"
#include "skeet/menu/rage_weapon_selector.hpp"
#include <windows.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace skeet::ui {
namespace {
std::uint16_t normalized_definition(std::uint16_t definition) noexcept {
    return std::uint16_t(definition - 500) <= 25 ? 42 : definition;
}
std::uint32_t swapped_pixel(std::uint32_t value) noexcept {
    return (value & 0xff00ff00u) | ((value & 255) << 16) | ((value >> 16) & 255);
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_IMAGE_MEMBER __thiscall
#else
#define SKEET_IMAGE_MEMBER
#endif
bool SKEET_IMAGE_MEMBER buffer_get_overflow(EngineImageBuffer*, std::int32_t) {
    return false;
} // 43336939
std::int32_t wrap_image(std::uint32_t value) noexcept {
    return std::bit_cast<std::int32_t>(value);
}
bool SKEET_IMAGE_MEMBER buffer_put_overflow(EngineImageBuffer* buffer,
                                            std::int32_t amount) { // 433B36FE
    if (buffer->grow_size < 0) {
        if (!(buffer->flags & 2))
            return false;
        buffer->grow_size = 0; // 43407D76: convert external storage into owned memory.
        if (!buffer->capacity)
            buffer->memory = nullptr;
        else {
            auto* owned = static_cast<std::byte*>(
                engine::invoke<void*>(buffer->allocator, 1, buffer->capacity));
            std::memcpy(owned, buffer->memory, std::uint32_t(buffer->capacity));
            buffer->memory = owned;
        }
    }
    const auto needed = wrap_image(std::uint32_t(buffer->put) - std::uint32_t(buffer->offset) +
                                   std::uint32_t(amount));
    while (buffer->capacity < needed) {
        const auto minimum = wrap_image(std::uint32_t(buffer->capacity) + 1);
        auto next = buffer->capacity;
        if (!buffer->grow_size) {
            if (!next)
                next = 32;
            while (next < minimum)
                next = wrap_image(std::uint32_t(next) * 2);
        } else
            next = wrap_image(std::uint32_t(minimum - 1 - (minimum - 1) % buffer->grow_size) +
                              std::uint32_t(buffer->grow_size));
        if (next < minimum) {
            if (!next && minimum <= -1)
                next = -1;
            else
                do {
                    next = wrap_image(std::uint32_t(next) + std::uint32_t(minimum)) / 2;
                } while (next < minimum);
        }
        buffer->capacity = next;
        buffer->memory =
            buffer->memory
                ? static_cast<std::byte*>(
                      engine::invoke<void*>(buffer->allocator, 3, buffer->memory, next))
                : static_cast<std::byte*>(engine::invoke<void*>(buffer->allocator, 1, next));
    }
    return true;
}

bool invoke_decoder(std::uintptr_t address, ImageFormat format, const std::byte* bytes,
                    std::int32_t length, EngineImageBuffer& buffer, std::int32_t& width,
                    std::int32_t& height) {
    if (!address)
        return false;
#if defined(__i386__) && defined(__GNUC__)
    // The native functions use ECX/EDX for input bytes/length, but leave their
    // stack arguments to the caller; __fastcall would pop them a second time.
    const std::uintptr_t arguments[]{reinterpret_cast<std::uintptr_t>(&buffer),
                                     reinterpret_cast<std::uintptr_t>(&width),
                                     reinterpret_cast<std::uintptr_t>(&height), 0xbf800000u, 0};
    std::uint32_t result;
    if (format == ImageFormat::svg) {
        asm volatile(
            "pushl 16(%%esi); pushl 12(%%esi); pushl 8(%%esi); pushl 4(%%esi); pushl (%%esi); call *%%edi; addl $20,%%esp"
            : "=a"(result), "+c"(bytes), "+d"(length)
            : "S"(arguments), "D"(address)
            : "memory", "cc");
    } else {
        asm volatile("pushl 8(%%esi); pushl 4(%%esi); pushl (%%esi); call *%%edi; addl $12,%%esp"
                     : "=a"(result), "+c"(bytes), "+d"(length)
                     : "S"(arguments), "D"(address)
                     : "memory", "cc");
    }
    return (result & 255) != 0;
#elif defined(_M_IX86)
    std::uint8_t result;
    auto* output = &buffer;
    auto* output_width = &width;
    auto* output_height = &height;
    if (format == ImageFormat::svg) {
        __asm {
            push 0
            push 0bf800000h
            push output_height
            push output_width
            push output
            mov ecx, bytes
            mov edx, length
            call address
            add esp, 20
            mov result, al
        }
    } else {
        __asm {
            push output_height
            push output_width
            push output
            mov ecx, bytes
            mov edx, length
            call address
            add esp, 12
            mov result, al
        }
    }
    return result != 0;
#else
    if (format == ImageFormat::svg) {
        using Function = bool (*)(const std::byte*, std::int32_t, EngineImageBuffer*, std::int32_t*,
                                  std::int32_t*, float, bool);
        return reinterpret_cast<Function>(address)(bytes, length, &buffer, &width, &height, -1.0f,
                                                   false);
    }
    using Function =
        bool (*)(const std::byte*, std::int32_t, EngineImageBuffer*, std::int32_t*, std::int32_t*);
    return reinterpret_cast<Function>(address)(bytes, length, &buffer, &width, &height);
#endif
}
void delay_image_job() {
    using Function = LONG(WINAPI*)(BOOLEAN, LARGE_INTEGER*);
    static const auto function = reinterpret_cast<Function>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "ZwDelayExecution"));
    if (!function)
        throw std::logic_error("ZwDelayExecution is not available");
    LARGE_INTEGER interval{};
    interval.QuadPart = -10000;
    function(FALSE, &interval);
}
class ImageJob {
public:
    ImageJob(const ImageDecoderBindings& bindings, ImageFormat format,
             std::span<const std::byte> bytes, std::int32_t width, std::int32_t height,
             std::vector<std::uint32_t>& output)
        : bindings(bindings), format(format), bytes(bytes), width(width), height(height),
          output(output) {}
    virtual void release(std::uint32_t) {} //433C95B7, engine supplies the deleting flag; RET4
    virtual void run() { // 4339F131
        EngineImageBuffer buffer;
        buffer.allocator = bindings.allocator;
        buffer.get_overflow = buffer_get_overflow;
        buffer.put_overflow = buffer_put_overflow;
        const auto size = std::uint32_t(width) * std::uint32_t(height) * 4;
        buffer.capacity = static_cast<std::int32_t>(size + 1);
        buffer.memory =
            static_cast<std::byte*>(engine::invoke<void*>(bindings.allocator, 1, buffer.capacity));
        try {
            const auto decoder = format == ImageFormat::svg   ? bindings.svg
                                 : format == ImageFormat::png ? bindings.png
                                                              : bindings.jpeg;
            if (format == ImageFormat::svg || format == ImageFormat::png ||
                format == ImageFormat::jpeg)
                success =
                    invoke_decoder(decoder, format, bytes.data(),
                                   static_cast<std::int32_t>(bytes.size()), buffer, width, height);
            if (success) {
                const auto count = std::uint32_t(width) * std::uint32_t(height);
                output.resize(count);
                const auto length = count * 4;
                if (!(buffer.error & 2) && buffer.get >= buffer.offset &&
                    std::int64_t(buffer.get) + length <= buffer.maximum_put &&
                    std::uint64_t(buffer.get - buffer.offset) + length <=
                        std::uint32_t(buffer.capacity)) {
                    if (length)
                        std::memcpy(output.data(), buffer.memory + buffer.get - buffer.offset,
                                    length);
                    buffer.get += static_cast<std::int32_t>(length);
                } else
                    buffer.error |= 2;
            }
        } catch (...) {
            if (buffer.grow_size >= 0 && buffer.memory)
                engine::invoke<void>(bindings.allocator, 5, buffer.memory);
            throw;
        }
        if (buffer.grow_size >= 0 && buffer.memory)
            engine::invoke<void>(bindings.allocator, 5, buffer.memory);
    }
    virtual void complete() {
        done.store(true);
    } // 433CEFA0
    const ImageDecoderBindings& bindings;
    ImageFormat format;
    std::span<const std::byte> bytes;
    std::int32_t width, height;
    std::vector<std::uint32_t>& output;
    bool success = false;
    std::atomic_bool done = false;
};
}

bool ImageRasterizer::decode(ImageFormat format, std::span<const std::byte> bytes,
                             std::int32_t& width, std::int32_t& height,
                             std::vector<std::uint32_t>& rgba) { // 4332B8B2
    if (!bindings_.enqueue || !bindings_.execute || !bindings_.png)
        return false;
    const auto renderer = engine::invoke<void*>(bindings_.render_context_source, 11);
    if (!renderer)
        return false;
    const auto context = engine::invoke<void*>(renderer, 29);
    if (!context)
        return false;
    void* pool = nullptr;
    std::memcpy(&pool, static_cast<std::byte*>(context) + 0x88, sizeof(pool));
    if (!pool)
        return false;
    ImageJob job(bindings_, format, bytes, width, height, rgba);
#if defined(_M_IX86) || defined(__i386__)
    using Enqueue = void(__thiscall*)(void*, ImageJob*);
    using Execute = void(__thiscall*)(void*);
#else
    using Enqueue = void (*)(void*, ImageJob*);
    using Execute = void (*)(void*);
#endif
    reinterpret_cast<Enqueue>(bindings_.enqueue)(context, &job);
    while (!job.done.load()) {
        reinterpret_cast<Execute>(bindings_.execute)(pool);
        if (!job.done.load())
            delay_image_job();
    }
    width = job.width;
    height = job.height;
    return job.success;
}

IconCache::IconCache(RendererBackend& renderer, IconBindings bindings)
    : runtime_(std::in_place, renderer, bindings) {
    initialize_native_cache();
}
void IconCache::bind_runtime(RendererBackend& renderer, IconBindings bindings) {
    if (runtime_)
        throw std::logic_error("The canonical icon cache renderer was already bound");
    runtime_.emplace(renderer, bindings);
}
IconCache::Runtime& IconCache::runtime() {
    if (!runtime_)
        throw std::logic_error("Icon engine/renderer bindings have not been acquired");
    return *runtime_;
}
ImageRasterizer& IconCache::image_rasterizer() {
    return runtime().rasterizer;
}
void IconCache::initialize_native_cache() noexcept {
    textures_.fill(-1);
} //433689B6

bool IconCache::path(std::uint16_t definition, std::string& output) { // 4332C460 / 433C09AD
    auto& bindings = runtime().bindings;
    if (!definition)
        output = "materials/panorama/images/icons/ui/gungametrbomb.svg";
    else if (definition == 5)
        output = "materials/panorama/images/icons/ui/plus2.svg";
    else {
        if (!bindings.item_system && bindings.item_system_factory)
            bindings.item_system = bindings.item_system_factory();
        if (!bindings.item_system)
            return false;
        const auto schema = engine::invoke<void*>(bindings.item_system, 0);
        if (!schema)
            return false;
        const auto item = engine::invoke<void*>(schema, 4, definition);
        if (!item)
            return false;
        const char* name = nullptr;
        std::memcpy(&name, static_cast<std::byte*>(item) + 0x1dc, sizeof(name));
        if (!name)
            return false;
        if (const auto underscore = std::strchr(name, '_'))
            name = underscore + 1;
        output = "materials/panorama/images/icons/equipment/";
        output += name;
        output += ".svg";
    }
    if (output.size() >= 512)
        output.resize(511);
    return true;
}
std::vector<std::byte> IconCache::read_game_file(const std::string& path) { // 433D25A8
    auto& bindings = runtime().bindings;
    std::vector<std::byte> bytes;
    auto* file = engine::invoke<void*>(bindings.filesystem, 2, path.c_str(), "rb", "GAME");
    if (!file)
        return bytes;
    try {
        const auto length = engine::invoke<std::uint32_t>(bindings.filesystem, 7, file);
        bytes.resize(length);
        if (!engine::invoke<std::int32_t>(bindings.filesystem, 0, bytes.data(), length, file))
            bytes.clear();
    } catch (...) {
        engine::invoke<void>(bindings.filesystem, 3, file);
        throw;
    }
    engine::invoke<void>(bindings.filesystem, 3, file);
    return bytes;
}
std::int32_t IconCache::load_svg(const std::string& path, std::int32_t height,
                                 std::int32_t existing) { // 433B0C29
    auto bytes = read_game_file(path);
    if (bytes.empty() || !runtime().bindings.images.svg)
        return -1;
    std::int32_t width = 0;
    std::vector<std::uint32_t> rgba;
    if (!runtime().rasterizer.decode(ImageFormat::svg, bytes, width, height, rgba))
        return -1;
    for (auto& pixel : rgba)
        pixel = swapped_pixel(pixel);
    if (existing == -1)
        return static_cast<std::int32_t>(
            runtime().renderer.create_texture(0, width, height, std::as_bytes(std::span(rgba))));
    runtime().renderer.update_texture(std::uint32_t(existing), width, height,
                                      std::as_bytes(std::span(rgba)));
    return existing;
}
std::int32_t IconCache::cached_texture(std::uint16_t definition) const noexcept {
    const auto index = normalized_definition(definition);
    return index < textures_.size() ? textures_[index] : -1;
}
bool IconCache::load(std::uint16_t definition, std::int32_t requested_width,
                     std::int32_t requested_height, CachedIcon& output,
                     bool dimensions) { // 433591C2
    auto& renderer = runtime().renderer;
    (void)requested_width; // Native433591C2 always passes zero width to433B0C29.
    const auto index = normalized_definition(definition);
    if (index >= textures_.size())
        return false;
    auto& texture = textures_[index];
    bool reload = texture == -1;
    if (!reload && requested_height) {
        std::int32_t width = 0, height = 0;
        renderer.texture_size(std::uint32_t(texture), &width, &height);
        reload = height != requested_height;
    }
    if (reload) {
        std::string filename;
        if (!path(index, filename))
            return false;
        texture = load_svg(filename, requested_height, texture);
        if (texture == -1)
            return false;
    }
    output.texture = texture;
    if (dimensions)
        renderer.texture_size(std::uint32_t(texture), &output.width, &output.height);
    return true;
}
esp::WeaponIconProvider IconCache::weapon_provider() noexcept {
    return {this, [](void* context, RenderQueue&, std::uint16_t, std::uint16_t actual,
                     std::int32_t height, esp::WeaponIcon& icon) {
                CachedIcon loaded;
                if (!static_cast<IconCache*>(context)->load(actual, 0, height, loaded))
                    return false;
                icon = {loaded.texture, loaded.width, loaded.height};
                return true;
            }};
}
RageSelectorIconProvider IconCache::selector_provider() noexcept {
    return {this, [](void* context, RenderQueue&, std::uint8_t symbol, Point size,
                     RageSelectorIcon& icon) {
                CachedIcon loaded;
                if (!static_cast<IconCache*>(context)->load(symbol, size.x, size.y, loaded))
                    return false;
                icon = {loaded.texture, {loaded.width, loaded.height}};
                return true;
            }};
}
} // namespace skeet::ui
