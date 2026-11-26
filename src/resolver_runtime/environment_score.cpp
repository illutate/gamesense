#include "skeet/resolver_runtime/environment_score.hpp"
#include "skeet/resolver_runtime/attempts.hpp"
#include "skeet/resolver.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace skeet::resolver_runtime {
namespace {
RageVector transform(const RageVector& point, const Matrix3x4& matrix) noexcept {
    RageVector result;
    for (std::size_t axis = 0; axis != 3; ++axis) {
        const auto& row = matrix.rows[axis];
        result[axis] = ((point[0] * row[0] + point[1] * row[1]) + point[2] * row[2]) + row[3];
    }
    return result;
}
Matrix3x4 concatenate(const Matrix3x4& left, const Matrix3x4& right) noexcept {
    Matrix3x4 result;
    for (std::size_t row = 0; row != 3; ++row)
        for (std::size_t column = 0; column != 4; ++column)
            result.rows[row][column] = ((left.rows[row][2] * right.rows[2][column] +
                                         left.rows[row][1] * right.rows[1][column]) +
                                        left.rows[row][0] * right.rows[0][column]) +
                                       (column == 3 ? left.rows[row][3] : 0.0f);
    return result;
}
Matrix3x4 angle_matrix(float yaw, const RageVector& origin, const RageTrigonometry& trig) {
    const float radians = yaw * 0.01745329238474369f;
    const float sy = trig.sine(radians), cy = trig.cosine(radians);
    const float sp = trig.sine(0.0f), cp = trig.cosine(0.0f);
    const float sr = trig.sine(0.0f), cr = trig.cosine(0.0f);
    const float srcy = sr * cy, srsy = sr * sy, crcy = cr * cy, crsy = cr * sy;
    return {{{cp * cy, srcy * sp - crsy, crcy * sp + srsy, origin[0]},
             {cp * sy, srsy * sp + crcy, crsy * sp - srcy, origin[1]},
             {-sp, sr * cp, cr * cp, origin[2]}}};
}
Matrix3x4 rebase_matrix(float original_yaw, const EnvironmentRebase& context,
                        const RageTrigonometry& trig) {
    float yaw = original_yaw + context.yaw_delta;
    if (std::isnan(yaw))
        yaw = 0.0f;
    else if (std::fabs(yaw) > 180.0f) {
        const float turns = round_half_away_from_zero(std::fabs(yaw / 360.0f));
        const float correction = turns * 360.0f;
        yaw = yaw < 0.0f ? yaw + correction : yaw - correction;
    }
    const auto old = angle_matrix(original_yaw, context.original_origin, trig);
    Matrix3x4 inverse;
    for (std::size_t row = 0; row != 3; ++row) {
        for (std::size_t column = 0; column != 3; ++column)
            inverse.rows[row][column] = old.rows[column][row];
        inverse.rows[row][3] = -((inverse.rows[row][1] * context.original_origin[1] +
                                  inverse.rows[row][0] * context.original_origin[0]) +
                                 inverse.rows[row][2] * context.original_origin[2]);
    }
    return concatenate(angle_matrix(yaw, context.current_origin, trig), inverse);
}
EnvironmentSelection score(const lagcomp::LagRecord& record,
                           const std::array<EnvironmentCapsule, 2>& hitboxes,
                           const RageVector& direction, const RageVector& origin,
                           const EnvironmentRebase* rebase, const RageTrigonometry* trig,
                           bool strict) {
    EnvironmentSelection selection{std::numeric_limits<float>::max(), 0.0f, 0};
    const EnvironmentSelection invalid{std::numeric_limits<float>::infinity(), 0.0f, 0};
    if (strict) {
        const float squared =
            direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
        if (!(record.header.flags & 0x80u) || !std::isfinite(squared) || squared < 1e-8f)
            return invalid;
        for (float value : origin)
            if (!std::isfinite(value))
                return invalid;
        for (const auto& box : hitboxes)
            if (box.matrix_index >= 19 || !(box.radius > 0.0f) || !std::isfinite(box.radius))
                return invalid;
    }
    std::array<float, 2> scores{};
    std::size_t score_index = 0;
    for (const std::size_t variant_index : {0U, 2U}) {
        const auto& variant = record.alternatives[variant_index];
        Matrix3x4 correction{};
        if (rebase != nullptr)
            correction = rebase_matrix(std::bit_cast<float>(variant.state[0]), *rebase, *trig);
        float maximum = -std::numeric_limits<float>::max();
        for (const auto& hitbox : hitboxes) {
            const auto& original_matrix = variant.matrices[hitbox.matrix_index];
            const auto matrix =
                rebase != nullptr ? concatenate(correction, original_matrix) : original_matrix;
            const auto first = transform(hitbox.minimum, matrix);
            const auto second = transform(hitbox.maximum, matrix);
            const float facing =
                ((second[1] - first[1]) * direction[1] + (second[0] - first[0]) * direction[0]) +
                (second[2] - first[2]) * direction[2];
            const auto& endpoint = facing >= 0.0f ? second : first;
            const float length =
                std::sqrt((direction[0] * direction[0] + direction[1] * direction[1]) +
                          direction[2] * direction[2]);
            const float scale = hitbox.radius / length;
            const float y = (scale * direction[1] + endpoint[1]) - origin[1];
            const float x = (scale * direction[0] + endpoint[0]) - origin[0];
            const float inverse =
                1.0f / (std::sqrt((x * x + y * y) + 0.0f) + 1.1920928955078125e-7f);
            const float value = ((inverse * y) * direction[1] + (inverse * x) * direction[0]) +
                                (inverse * 0.0f) * direction[2];
            if (strict && !std::isfinite(value))
                return invalid;
            if (value > maximum)
                maximum = value;
        }
        scores[score_index++] = maximum;
        if (selection.score > maximum) {
            selection.score = maximum;
            selection.pose_yaw = std::bit_cast<float>(variant.state[1]);
        }
    }
    // Equal projections do not identify a side. Do not let enumeration order
    // become apparent geometric evidence.
    if (strict && std::fabs(scores[0] - scores[1]) <=
                      1e-5f * std::max(1.0f, std::max(std::fabs(scores[0]), std::fabs(scores[1]))))
        return invalid;
    selection.attempt = classify_winning_pose(selection.pose_yaw);
    return selection;
}
}
EnvironmentSelection score_environment_hypotheses(const lagcomp::LagRecord& record,
                                                  const std::array<EnvironmentCapsule, 2>& hitboxes,
                                                  const RageVector& direction,
                                                  const RageVector& origin, bool strict) noexcept {
    return score(record, hitboxes, direction, origin, nullptr, nullptr, strict);
}
EnvironmentSelection score_environment_hypotheses(const lagcomp::LagRecord& record,
                                                  const std::array<EnvironmentCapsule, 2>& hitboxes,
                                                  const RageVector& direction,
                                                  const RageVector& origin,
                                                  const EnvironmentRebase& rebase,
                                                  const RageTrigonometry& trig, bool strict) {
    return score(record, hitboxes, direction, origin, &rebase, &trig, strict);
}
} // namespace skeet::resolver_runtime
