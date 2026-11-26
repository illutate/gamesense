#include "skeet/render/beam_geometry.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>
#include <immintrin.h>

namespace skeet::render {
namespace {
using esp::Vector3;
float add(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float sub(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float mul(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float divide(float a, float b) noexcept {
    return _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
Vector3 subtract(Vector3 a, Vector3 b) noexcept {
    return {sub(a.x, b.x), sub(a.y, b.y), sub(a.z, b.z)};
}
Vector3 normalize(Vector3 value) noexcept {
    const auto squared =
        add(add(mul(value.x, value.x), mul(value.y, value.y)), mul(value.z, value.z));
    const auto length = _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(squared)));
    const auto reciprocal = divide(1.0f, add(length, 0x1p-23f)); // native 43472108
    return {mul(reciprocal, value.x), mul(reciprocal, value.y), mul(reciprocal, value.z)};
}
Vector3 width_axis(Vector3 view, Vector3 to_previous) noexcept {
    return normalize({sub(mul(view.z, to_previous.y), mul(view.y, to_previous.z)),
                      sub(mul(view.x, to_previous.z), mul(view.z, to_previous.x)),
                      sub(mul(view.y, to_previous.x), mul(view.x, to_previous.y))});
}
Vector3 tangent_axis(Vector3 view, Vector3 width) noexcept {
    return normalize({sub(mul(width.z, view.y), mul(width.y, view.z)),
                      sub(mul(width.x, view.z), mul(width.z, view.x)),
                      sub(mul(view.x, width.y), mul(view.y, width.x))});
}
Vector3 average(Vector3 previous, Vector3 current) noexcept {
    return normalize({mul(add(previous.x, current.x), 0.5f), mul(add(previous.y, current.y), 0.5f),
                      mul(add(previous.z, current.z), 0.5f)});
}
Vector3 offset(Vector3 position, Vector3 axis, float distance) noexcept {
    return {add(mul(distance, axis.x), position.x), add(mul(distance, axis.y), position.y),
            add(mul(axis.z, distance), position.z)};
}
std::uint32_t bgra(std::uint32_t rgba) noexcept {
    return (rgba & 0xFF00FF00u) | ((rgba & 0x000000FFu) << 16) | ((rgba & 0x00FF0000u) >> 16);
}
void attribute(BeamMeshDescriptor& description, unsigned channel, std::int32_t vertex,
               const void* source, std::size_t size) {
    auto* destination = static_cast<std::byte*>(description.vertex_data[channel]);
    std::memcpy(destination + std::int64_t(description.vertex_layout[channel]) * vertex, source,
                size);
}
}

BeamSegmentBuilder::BeamSegmentBuilder(BeamGeometry& output, std::int32_t segment_count,
                                       Vector3 camera, bool indexed) noexcept
    : output_(output), camera_(camera), segment_count_(segment_count),
      first_vertex_(std::uint16_t(output.vertices.size())), indexed_(indexed) {
    output_.indexed = indexed;
}
void BeamSegmentBuilder::emit_pair(Vector3 width, Vector3 tangent) {
    const auto positive = mul(pending_.width, 0.5f);
    const auto negative =
        mul(std::bit_cast<float>(std::bit_cast<std::uint32_t>(pending_.width) ^ 0x80000000u), 0.5f);
    const auto color = bgra(pending_.rgba);
    output_.vertices.push_back({offset(pending_.position, width, positive),
                                color,
                                {0.0f, pending_.texcoord},
                                {0.0f, pending_.texcoord},
                                width,
                                tangent});
    output_.vertices.push_back({offset(pending_.position, width, negative),
                                color,
                                {1.0f, pending_.texcoord},
                                {1.0f, pending_.texcoord},
                                width,
                                tangent});
}
void BeamSegmentBuilder::append_quad() {
    const auto end =
        std::uint16_t(std::uint32_t(first_vertex_) + std::uint32_t(segments_seen_) * 2u);
    const std::array<std::uint16_t, 6> quad{std::uint16_t(end - 4u), std::uint16_t(end - 3u),
                                            std::uint16_t(end - 2u), std::uint16_t(end - 3u),
                                            std::uint16_t(end - 1u), std::uint16_t(end - 2u)};
    output_.indices.insert(output_.indices.end(), quad.begin(), quad.end());
}
void BeamSegmentBuilder::next_segment(const BeamSegment& segment) { // 43320D77..43321CA9
    if (segments_seen_ > 0) {
        const auto view = subtract(pending_.position, camera_);
        const auto current_width = width_axis(view, subtract(pending_.position, segment.position));
        const auto smoothed_width =
            segments_seen_ < 2 ? current_width : average(previous_width_axis_, current_width);
        previous_width_axis_ = current_width;
        emit_pair(smoothed_width, tangent_axis(view, smoothed_width));
        if (indexed_ && segments_seen_ >= 2)
            append_quad();
    }
    pending_ = segment;
    segments_seen_ = std::bit_cast<std::int32_t>(std::uint32_t(segments_seen_) + 1u);
    if (segments_seen_ == segment_count_) {
        const auto view = subtract(pending_.position, camera_);
        emit_pair(previous_width_axis_, tangent_axis(view, previous_width_axis_));
        if (indexed_ && segments_seen_ > 1)
            append_quad();
    }
}
void build_beam_geometry(std::span<const BeamSegment> segments, Vector3 camera,
                         BeamGeometry& output, bool indexed) {
    output.vertices.clear();
    output.indices.clear();
    output.vertices.reserve(segments.size() * 2);
    if (indexed && segments.size() > 1)
        output.indices.reserve((segments.size() - 1) * 6);
    BeamSegmentBuilder builder(output, std::int32_t(segments.size()), camera, indexed);
    for (const auto& segment : segments)
        builder.next_segment(segment);
}
void draw_beam_geometry(void* mesh, const BeamGeometry& geometry) {
    if (geometry.vertices.empty())
        return;
    const auto vertex_count = std::int32_t(geometry.vertices.size());
    const auto index_count =
        geometry.indexed ? std::int32_t(geometry.indices.size()) : vertex_count;
    engine::invoke<void>(mesh, 10, std::int32_t(geometry.indexed ? 2 : 3));
    BeamMeshDescriptor description;
    engine::invoke<void>(mesh, 17, vertex_count, index_count, &description, false);
    for (std::int32_t i = 0; i < vertex_count; ++i) {
        const auto& vertex = geometry.vertices[std::size_t(i)];
        attribute(description, 0, i, &vertex.position, sizeof(vertex.position));
        attribute(description, 4, i, &vertex.bgra, sizeof(vertex.bgra));
        attribute(description, 6, i, vertex.uv0.data(), sizeof(vertex.uv0));
        attribute(description, 7, i, vertex.uv1.data(), sizeof(vertex.uv1));
        attribute(description, 14, i, &vertex.tangent_s, sizeof(vertex.tangent_s));
        attribute(description, 15, i, &vertex.tangent_t, sizeof(vertex.tangent_t));
    }
    std::int32_t indices_written = 0;
    if (description.index_size) {
        for (std::int32_t i = 0; i < index_count; ++i) {
            const auto index =
                geometry.indexed ? geometry.indices[std::size_t(i)] : std::uint16_t(i);
            description.indices[indices_written] =
                std::uint16_t(std::uint32_t(description.first_vertex) + index);
            indices_written = std::bit_cast<std::int32_t>(std::uint32_t(indices_written) +
                                                          std::uint32_t(description.index_size));
        }
    }
    engine::invoke<void>(mesh, 20, vertex_count, indices_written, &description);
    engine::invoke<void>(mesh, 12, std::int32_t(-1), std::int32_t(0));
}

} // namespace skeet::render
