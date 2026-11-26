#include "skeet/esp/projection.hpp"
#include "detail.hpp"

namespace skeet::esp {
using namespace detail;

bool project_point(const Projection& p, Vector3 v, Point& output) noexcept { // 0x433F44F5
    const auto& m = p.matrix;
    const float w = ((v.x * m[12] + v.y * m[13]) + v.z * m[14]) + m[15];
    if (0.001f > w)
        return false;
    const float reciprocal = 1.0f / w;
    const float horizontal = ((v.x * m[0] + v.y * m[1]) + v.z * m[2]) + m[3];
    const float vertical = ((v.x * m[4] + v.y * m[5]) + v.z * m[6]) + m[7];
    output.x = _mm_cvtss_si32(_mm_set_ss((reciprocal * horizontal - -1.0f) * p.horizontal_scale));
    output.y = _mm_cvtss_si32(_mm_set_ss((1.0f - vertical * reciprocal) * p.vertical_scale));
    return true;
}

bool player_box(const Projection& p, Vector3 first, Vector3 second,
                Rectangle& output) noexcept { // 0x433404E2
    Point a, b;
    if (!project_point(p, first, a) || !project_point(p, second, b))
        return false;
    const auto delta = sub(b.y, a.y);
    const auto height = delta < 0 ? sub(0, delta) : delta;
    const auto width = height / 2;
    output = {sub(add(b.x, a.x) / 2, width / 2), a.y, width, height};
    return true;
}

float convert_field_of_view(float degrees, float aspect) noexcept { // 0x433ED56D
    const float radians = degrees * std::bit_cast<float>(0x3C8EFA35u);
    const float half = std::tan(radians * 0.5f) * aspect;
    const float converted = std::atan(half) * std::bit_cast<float>(0x42652EE0u);
    return converted + converted;
}

float angle_to_target(Vector3 origin, Vector3 target, Vector3 axis) noexcept { // 0x433B4B55
    const float x = target.x - origin.x, y = target.y - origin.y, z = target.z - origin.z;
    const float length = std::sqrt((x * x + y * y) + z * z);
    if (length == 0.0f)
        return 0.0f;
    const float dot = (y * axis.y + x * axis.x) + z * axis.z;
    return std::acos(dot / length) * std::bit_cast<float>(0x42652EE0u);
}

} // namespace skeet::esp
