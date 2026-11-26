#include "skeet/effects/blur.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <immintrin.h>

namespace skeet::effects {
using engine::invoke;
namespace {
using i32 = std::int32_t;
i32 sub(i32 a, i32 b) noexcept {
    return std::bit_cast<i32>(std::uint32_t(a) - std::uint32_t(b));
}
i32 truncated(float value) noexcept {
    return _mm_cvttss_si32(_mm_set_ss(value));
}
i32 bounded(i32 value, i32 upper) noexcept {
    if (upper < 0)
        return upper;
    if (value < 0)
        return 0;
    return value > upper ? upper : value;
}
void* find_material(void* system, std::uint32_t wanted) {
    const auto lock = invoke<std::uint32_t>(system, 118);
    auto handle = invoke<std::uint16_t>(system, 86);
    void* found = nullptr;
    while (handle != invoke<std::uint16_t>(system, 88)) {
        auto* material = invoke<void*>(system, 89, handle);
        if (material && !invoke<bool>(material, 42)) {
            const auto* name = invoke<const char*>(material, 0);
            std::uint32_t hash = 0x811C9DC5u;
            if (name)
                for (; *name; ++name)
                    hash = (hash ^ std::uint8_t(*name)) * 0x01000193u;
            if ((hash ^ 0x92CA5DF1u) == wanted) {
                found = material;
                break;
            }
        }
        handle = invoke<std::uint16_t>(system, 87, handle);
    }
    invoke<void>(system, 119, lock);
    return found;
}
void copy_scene(const BlurEffect& effect, i32 width, i32 height) { // 433FA14F
    auto* context = invoke<void*>(effect.bindings.material_system, 115);
    if (context)
        invoke<void>(context, 2);
    i32 actual_width = width, actual_height = height;
    invoke<void>(context, 8, &actual_width, &actual_height);
    auto* texture = effect.textures[2];
    const auto texture_width = invoke<i32>(texture, 3);
    const auto texture_height = invoke<i32>(texture, 4);
    const std::array<i32, 4> source{0, 0, width, height};
    auto destination = source;
    if (actual_width > texture_width || actual_height > texture_height) {
        const float horizontal = float(texture_width) / float(actual_width);
        const float vertical = float(texture_height) / float(actual_height);
        destination[0] = bounded(truncated(float(source[0]) * horizontal), texture_width);
        destination[1] = bounded(truncated(float(source[1]) * vertical), texture_height);
        destination[2] =
            bounded(truncated(float(source[2]) * horizontal), sub(texture_width, destination[0]));
        destination[3] =
            bounded(truncated(float(source[3]) * vertical), sub(texture_height, destination[1]));
    }
    invoke<void>(context, 122, texture, i32(0), source.data(), destination.data());
    invoke<void>(context, 20, texture, i32(0));
    invoke<void>(context, 3);
    invoke<void>(context, 1);
}
void set_target(void* context, void* texture) { // 433CB550
    invoke<void>(context, 6, texture);
    const auto height = invoke<i32>(texture, 4);
    const auto width = invoke<i32>(texture, 3);
    invoke<void>(context, 40, i32(0), i32(0), width, height);
}
void screen_rectangle(void* context, void* material, i32 width, i32 height, float right,
                      float bottom, i32 texture_width, i32 texture_height) { // 4340189C
    invoke<void>(context, 114, material, i32(0), i32(0), width, height, 0.0f, 0.0f, right, bottom,
                 texture_width, texture_height, static_cast<void*>(nullptr), i32(1), i32(1));
}
void attribute(BlurMeshDescriptor& description, unsigned channel, i32 vertex,
               std::span<const float> values) {
    auto* bytes = static_cast<std::byte*>(description.vertex_data[channel]);
    const auto offset = std::int64_t(description.vertex_layout[channel]) * vertex;
    std::memcpy(bytes + offset, values.data(), values.size_bytes());
}
void rectangle_mesh(void* context, void* material, std::span<const BlurRectangle> rectangles,
                    i32 screen_width,
                    i32 screen_height) { // 4334EF43 and its MeshBuilder dependency
    for (i32 matrix = 0; matrix < 2; ++matrix) {
        invoke<void>(context, 22, matrix);
        invoke<void>(context, 23);
        invoke<void>(context, 33);
    }
    invoke<void>(context, 9, material, static_cast<void*>(nullptr));
    auto* mesh = invoke<void*>(context, 62, true, static_cast<void*>(nullptr),
                               static_cast<void*>(nullptr), static_cast<void*>(nullptr));
    const auto vertices = i32(rectangles.size() * 4), indices = i32(rectangles.size() * 6);
    invoke<void>(mesh, 10, i32(2));
    BlurMeshDescriptor description;
    invoke<void>(mesh, 17, vertices, indices, &description, false);
    const float reciprocal_width = 1.0f / float(screen_width),
                reciprocal_height = 1.0f / float(screen_height);
    i32 viewport_x, viewport_y, viewport_width, viewport_height;
    invoke<void>(context, 41, &viewport_x, &viewport_y, &viewport_width, &viewport_height);
    i32 vertex = 0;
    for (const auto rectangle : rectangles) {
        const float x = float(rectangle.x), y = float(rectangle.y);
        const float far_x = float(rectangle.width) + x, far_y = float(rectangle.height) + y;
        const float x_step = rectangle.width > 1 ? (far_x - x) / float(rectangle.width - 1) : 0.0f;
        const float y_step =
            rectangle.height > 1 ? (far_y - y) / float(rectangle.height - 1) : 0.0f;
        const float left_uv = (x + 0.5f) - x_step * 0.5f;
        const float right_uv = (far_x + 0.5f) + x_step * 0.5f;
        const float top_uv = (y + 0.5f) - y_step * 0.5f;
        const float bottom_uv = (far_y + 0.5f) + y_step * 0.5f;
        const float left = ((x - 0.5f) + (x - 0.5f)) / float(viewport_width) - 1.0f;
        const float right = ((float(i32(rectangle.x) + rectangle.width) - 0.5f) +
                             (float(i32(rectangle.x) + rectangle.width) - 0.5f)) /
                                float(viewport_width) -
                            1.0f;
        const float top = 1.0f - ((y - 0.5f) + (y - 0.5f)) / float(viewport_height);
        const float bottom = 1.0f - ((float(i32(rectangle.y) + rectangle.height) - 0.5f) +
                                     (float(i32(rectangle.y) + rectangle.height) - 0.5f)) /
                                        float(viewport_height);
        for (unsigned corner = 0; corner < 4; ++corner, ++vertex) {
            const bool first = corner == 0 || corner == 3;
            const bool lower = (corner & 2) != 0;
            const std::array<float, 3> position{first ? left : right, lower ? bottom : top, 0.0f};
            const std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
            const std::array<float, 2> uv{reciprocal_width * (first ? left_uv : right_uv),
                                          reciprocal_height * (lower ? bottom_uv : top_uv)};
            const std::array<float, 3> tangent_s{0.0f, 1.0f, 0.0f}, tangent_t{1.0f, 0.0f, 0.0f};
            attribute(description, 0, vertex, position);
            attribute(description, 3, vertex, normal);
            attribute(description, 6, vertex, uv);
            attribute(description, 14, vertex, tangent_s);
            attribute(description, 15, vertex, tangent_t);
        }
    }
    i32 indices_written = 0;
    if (description.index_size) {
        if (description.indices) {
            if (!(reinterpret_cast<std::uintptr_t>(description.indices) & 3)) {
                const auto first = std::uint32_t(std::uint16_t(description.first_vertex));
                auto pair = (first | (first << 16)) + 2u;
                for (std::size_t i = 0; i < rectangles.size(); ++i, pair += 0x00040004u) {
                    const std::array<std::uint32_t, 3> quad{pair + 0xFFFEu, pair,
                                                            pair + 0x00030000u};
                    std::memcpy(description.indices + i * 6, quad.data(), sizeof(quad));
                }
            } else {
                auto first = std::uint16_t(description.first_vertex);
                for (std::size_t i = 0; i < rectangles.size();
                     ++i, first = std::uint16_t(first + 4)) {
                    const std::array<std::uint16_t, 6> quad{
                        first, std::uint16_t(first + 1), std::uint16_t(first + 2),
                        first, std::uint16_t(first + 2), std::uint16_t(first + 3)};
                    std::memcpy(description.indices + i * 6, quad.data(), sizeof(quad));
                }
            }
        }
        indices_written =
            std::bit_cast<i32>(std::uint32_t(indices) * std::uint32_t(description.index_size));
    }
    invoke<void>(mesh, 20, vertices, indices_written, &description);
    invoke<void>(mesh, 12, i32(-1), i32(0));
    for (i32 matrix = 0; matrix < 2; ++matrix) {
        invoke<void>(context, 22, matrix);
        invoke<void>(context, 24);
    }
}
}
void BlurEffect::draw(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    if (rectangles_.size() >= 8192)
        return;
    if (rectangles_.size() == rectangles_.capacity())
        rectangles_.reserve(
            std::min<std::size_t>(8192, std::max<std::size_t>(64, rectangles_.capacity() * 2)));
    rectangles_.push_back({std::bit_cast<std::int16_t>(std::uint16_t(x)),
                           std::bit_cast<std::int16_t>(std::uint16_t(y)),
                           std::bit_cast<std::int16_t>(std::uint16_t(width)),
                           std::bit_cast<std::int16_t>(std::uint16_t(height))});
}
bool BlurEffect::initialize_resources() {
    for (unsigned i = 0; i < 3; ++i)
        if (!materials[i])
            materials[i] = find_material(bindings.material_system, material_hashes[i]);
    if (!materials[3]) {
        materials[3] = invoke<void*>(
            bindings.material_system, 84, static_cast<const char*>(nullptr),
            static_cast<const char*>(nullptr), false, static_cast<const char*>(nullptr));
        if (materials[3])
            invoke<void>(materials[3], 12);
    }
    for (unsigned i = 0; i < 3; ++i)
        if (!textures[i]) {
            textures[i] = invoke<void*>(bindings.material_system, 91, texture_names[i].c_str(),
                                        texture_group.c_str(), true, std::int32_t(0));
            if (textures[i])
                invoke<void>(textures[i], 10);
        }
    return materials[0] && materials[1] && materials[2] && materials[3] && textures[0] &&
           textures[1] && textures[2];
}
void BlurEffect::release_resources() {
    for (auto& material : materials)
        if (material) {
            invoke<void>(material, 13);
            material = nullptr;
        }
    for (auto& texture : textures)
        if (texture) {
            invoke<void>(texture, 11);
            texture = nullptr;
        }
}
void BlurEffect::apply() {
    i32 width, height;
    invoke<void>(bindings.engine, 5, &width, &height);
    auto* context = invoke<void*>(bindings.material_system, 115);
    if (context)
        invoke<void>(context, 2);
    auto* previous = invoke<void*>(context, 7);
    invoke<void>(context, 6, static_cast<void*>(nullptr));
    copy_scene(*this, width, height);
    invoke<void>(context, 6, previous);
    invoke<void>(context, 119);
    const auto quarter_width = width / 4, quarter_height = height / 4;
    set_target(context, textures[0]);
    auto texture_height = invoke<i32>(textures[2], 4);
    auto texture_width = invoke<i32>(textures[2], 3);
    screen_rectangle(context, materials[0], quarter_width, quarter_height, float(sub(width, 4)),
                     float(sub(height, 4)), texture_width, texture_height);
    set_target(context, textures[1]);
    const auto tex0_height = invoke<i32>(textures[0], 4);
    const auto tex0_width = invoke<i32>(textures[0], 3);
    screen_rectangle(context, materials[1], quarter_width, quarter_height,
                     float(sub(quarter_width, 1)), float(sub(quarter_height, 1)), tex0_width,
                     tex0_height);
    set_target(context, textures[0]);
    screen_rectangle(context, materials[2], quarter_width, quarter_height,
                     float(sub(quarter_width, 1)), float(sub(quarter_height, 1)), tex0_width,
                     tex0_height);
    invoke<void>(context, 120);
    if (auto** variables = invoke<void**>(materials[3], 41)) {
        if (variables[6])
            invoke<void>(variables[6], 15, textures[0]);
        if (variables[5])
            invoke<void>(variables[5], 4, 1.0f);
    }
    rectangle_mesh(context, materials[3], rectangles_, width, height);
    invoke<void>(context, 3);
    invoke<void>(context, 1);
}
void BlurEffect::render() {
    if (rectangles_.empty())
        return;
    if (initialize_resources())
        apply();
    rectangles_.clear();
}
} // namespace skeet::effects
