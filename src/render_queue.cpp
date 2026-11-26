#include "skeet/render_queue.hpp"
#include <bit>
#include <cstring>
#include <stdexcept>

namespace skeet::ui {
static_assert(std::endian::native == std::endian::little);
namespace {
std::int32_t add(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::int32_t sub(std::int32_t a, std::int32_t b) noexcept {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) - std::uint32_t(b));
}
}

std::uint32_t subtract_color(std::uint32_t color, std::uint32_t amount) noexcept { // 0x433E87BD
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift != 32; shift += 8) {
        const auto a = (color >> shift) & 255;
        const auto b = (amount >> shift) & 255;
        result |= (a > b ? a - b : 0) << shift;
    }
    return result;
}
std::uint32_t add_color(std::uint32_t color, std::uint32_t amount) noexcept { // 0x43383DE6
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift != 32; shift += 8) {
        const auto sum = ((color >> shift) & 255) + ((amount >> shift) & 255);
        result |= (sum < 255 ? sum : 255) << shift;
    }
    return result;
}

// Complete helper 0x433D0F44..0x433D0F91. Alpha is preserved exactly.
std::uint32_t desaturate_color(std::uint32_t color, bool enabled) noexcept {
    if (!enabled)
        return color;
    const auto red = color & 255;
    const auto green = (color >> 8) & 255;
    const auto blue = (color >> 16) & 255;
    const auto intensity = (2 * red + 3 * green + blue) / 6;
    return (color & 0xff000000U) | intensity | (intensity << 8) | (intensity << 16);
}

// 0x433AB0FA. Storage ownership is ordinary C++; the command format is unchanged.
std::byte* RenderQueue::append(std::uint32_t opcode, std::uint32_t payload_size) {
    const auto payload_aligned = (std::uint64_t(payload_size) + 3) & ~std::uint64_t(3);
    const auto offset = commands.size();
    const auto end = offset + 4 + payload_aligned;
    if (opcode > 31 || end > 0x07ffffff)
        throw std::length_error("Render command exceeds its header format");
    commands.resize(static_cast<std::size_t>(end), std::byte{});
    const std::uint32_t header = static_cast<std::uint32_t>(end) | (opcode << 27);
    std::memcpy(commands.data() + offset, &header, sizeof(header));
    return commands.data() + offset + 4;
}

// Complete native writers; field order is the original queue wire format.
void RenderQueue::rectangle(RectangleCommand command) { // 0x433EB76A
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(0, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::rectangle_outline(RectangleCommand c, std::int32_t thickness) { // 0x433C14DB
    rectangle({c.x, c.y, c.width, thickness, c.color});
    rectangle({c.x, c.y, thickness, c.height, c.color});
    rectangle({add(sub(c.x, thickness), c.width), c.y, thickness, c.height, c.color});
    rectangle({c.x, add(sub(c.y, thickness), c.height), c.width, thickness, c.color});
}
void RenderQueue::outlined_rectangle(RectangleCommand c, std::uint32_t border_color,
                                     std::int32_t thickness) { // 0x433F16D1
    rectangle({add(c.x, 1), add(c.y, 1), sub(c.width, 2), sub(c.height, 2), c.color});
    c.color = border_color;
    rectangle_outline(c, thickness);
}
void RenderQueue::four_color_gradient(FourColorGradientCommand command) { // 0x4335BDB5
    for (auto& color : command.colors)
        color = desaturate_color(color, desaturate);
    std::memcpy(append(1, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::vertical_gradient(GradientCommand command) { // 0x433B66C1
    command.first_color = desaturate_color(command.first_color, desaturate);
    command.second_color = desaturate_color(command.second_color, desaturate);
    std::memcpy(append(2, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::horizontal_gradient(GradientCommand command) { // 0x4336AF92
    command.first_color = desaturate_color(command.first_color, desaturate);
    command.second_color = desaturate_color(command.second_color, desaturate);
    std::memcpy(append(3, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::line(LineCommand command) { // 0x433B1863
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(4, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::polyline(std::span<const LinePoint> points, std::uint32_t color) { // 0x43369793
    static_assert(sizeof(LinePoint) == 8);
    if (points.size() > (0x07ffffffU - 20) / sizeof(LinePoint))
        throw std::length_error("Polyline exceeds its command format");
    color = desaturate_color(color, desaturate);
    const auto count = static_cast<std::uint32_t>(points.size());
    auto* payload = append(5, count * sizeof(LinePoint) + 16);
    std::memcpy(payload, &color, sizeof(color));
    std::memcpy(payload + 4, &count, sizeof(count));
    if (!points.empty())
        std::memcpy(payload + 8, points.data(), points.size_bytes());
    // The original reserves eight additional zero bytes after the points.
}
void RenderQueue::triangle(TriangleCommand command) { // 0x433ADAF1
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(6, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::circle(CircleCommand command) { // 0x4338A58B
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(9, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::circle_outline(CircleOutlineCommand command) { // 0x43396E3D
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(16, sizeof(command)), &command, sizeof(command));
}

// Complete texture queue entry 0x433DF2CF..0x433DF33A, native opcode 11.
void RenderQueue::texture(TextureCommand command) {
    command.color = desaturate_color(command.color, desaturate);
    std::memcpy(append(11, sizeof(command)), &command, sizeof(command));
}
void RenderQueue::text(std::int32_t x, std::int32_t y, std::uint32_t color, std::uint32_t flags,
                       std::int32_t maximum_width, std::u16string_view content) { // 0x433E4A54
    if (content.empty())
        return;
    if (content.size() > (0x07ffffffU - 28) / sizeof(char16_t))
        throw std::length_error("Text exceeds its render command format");
    color = desaturate_color(color, desaturate);
    auto* payload = append(10, static_cast<std::uint32_t>(content.size() * 2 + 24));
    prepare_glyphs(fonts, content, flags);
    const std::array<std::uint32_t, 5> header{std::uint32_t(x), std::uint32_t(y), color, flags,
                                              std::uint32_t(maximum_width)};
    std::memcpy(payload, header.data(), sizeof(header));
    const auto length = static_cast<std::uint16_t>(content.size());
    std::memcpy(payload + 20, &length, sizeof(length));
    std::memcpy(payload + 22, content.data(), content.size() * sizeof(char16_t));
}

void RenderQueue::set_bounds(const std::array<std::int32_t, 4>& value) { // 0x4339CB94
    bounds = value;
    std::memcpy(append(12, sizeof(value)), value.data(), sizeof(value));
}
std::array<std::int32_t, 4> RenderQueue::get_bounds() const noexcept { // 0x433F1E34
    return bounds;
}
void RenderQueue::reset_bounds(bool reset) { // 0x4331ECE5
    bounds_reset = reset;
    *append(13, 1) = static_cast<std::byte>(reset);
}
bool RenderQueue::is_bounds_reset() const noexcept { // 0x433F19B9
    return bounds_reset;
}
void RenderQueue::set_alpha(float value) { // 0x433C12DA
    if (alpha != value) {
        alpha = value;
        std::memcpy(append(14, sizeof(value)), &value, sizeof(value));
    }
}
float RenderQueue::get_alpha() const noexcept { // 0x4334A98A
    return alpha;
}
void RenderQueue::set_depth(float value) { // 0x433B360E
    depth = value;
    std::memcpy(append(15, sizeof(value)), &value, sizeof(value));
}
float RenderQueue::get_depth() const noexcept { // 0x433CFD53
    return depth;
}
void RenderQueue::set_desaturation(bool enabled) noexcept { // 0x4338A5FD
    desaturate = enabled;
}
} // namespace skeet::ui
