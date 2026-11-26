#include "skeet/lagcomp/records.hpp"
#include <bit>
#include <cstring>
#include <xmmintrin.h>

namespace skeet::lagcomp {
namespace {
void copy_words(void* destination, const void* source, std::size_t count) noexcept {
    auto* out = static_cast<std::byte*>(destination);
    const auto* in = static_cast<const std::byte*>(source);
    for (std::size_t i = 0; i != count; ++i) {
        std::uint32_t word;
        std::memcpy(&word, in + i * 4, 4);
        std::memcpy(out + i * 4, &word, 4);
    }
}
void copy_matrices(HitboxMatrices& destination, const HitboxMatrices& source) noexcept {
    for (std::size_t i = 0; i != 19; ++i) {
        // The native loop loads all three rows before storing this matrix.
        const auto a = _mm_loadu_ps(source[i].rows[0]);
        const auto b = _mm_loadu_ps(source[i].rows[1]);
        const auto c = _mm_loadu_ps(source[i].rows[2]);
        _mm_storeu_ps(destination[i].rows[0], a);
        _mm_storeu_ps(destination[i].rows[1], b);
        _mm_storeu_ps(destination[i].rows[2], c);
    }
}
}
void copy_lag_record(LagRecord& destination, const LagRecord& source) noexcept {
    copy_words(&destination.header, &source.header, 0x16);
    destination.header.resolver_index = source.header.resolver_index;
    destination.header.resolver_mode = source.header.resolver_mode;
    destination.header.resolver_state = source.header.resolver_state;
    destination.header.flags = source.header.flags;
    copy_words(&destination.animation, &source.animation, 0x74);
    copy_matrices(destination.matrices, source.matrices);
    for (std::size_t i = 0; i != 3; ++i) {
        destination.alternatives[i].state[0] = source.alternatives[i].state[0];
        destination.alternatives[i].state[1] = source.alternatives[i].state[1];
        copy_matrices(destination.alternatives[i].matrices, source.alternatives[i].matrices);
    }
}
void assign_evaluation(HistoryEvaluation& destination, const HistoryEvaluation& source) noexcept {
    copy_words(destination.words.data(), source.words.data(), 0x22);
    copy_words(destination.state.words.data(), source.state.words.data(), 0x3B);
    copy_lag_record(destination.state.record, source.state.record);
}
void copy_construct_evaluation(HistoryEvaluation& destination,
                               const HistoryEvaluation& source) noexcept {
    copy_words(destination.words.data(), source.words.data(), 0x22);
    std::memcpy(&destination.state, &source.state, sizeof(EvaluationState));
}
void capture_animation(AnimationState& destination, const void* layers, std::int32_t count,
                       const float* pose_parameters) noexcept {
    destination.layer_count = count < 13 ? count : 13;
    const auto* source = static_cast<const std::byte*>(layers);
    for (std::int32_t i = 0; i < destination.layer_count; ++i)
        copy_words(&destination.layers[std::size_t(i)], source + std::size_t(i) * 0x38 + 0x14, 7);
    copy_words(destination.pose_parameters.data(), pose_parameters, 24);
}
void restore_animation(const AnimationState& source, void* layers, std::int32_t count,
                       float* pose_parameters) noexcept {
    if (source.layer_count > 0 && source.layer_count == count) {
        auto* destination = static_cast<std::byte*>(layers);
        for (std::int32_t i = 0; i < source.layer_count; ++i)
            copy_words(destination + std::size_t(i) * 0x38 + 0x14, &source.layers[std::size_t(i)],
                       7);
    }
    copy_words(pose_parameters, source.pose_parameters.data(), 24);
}
std::int32_t time_to_tick(float time, float interval) noexcept {
    return _mm_cvtt_ss2si(
        _mm_add_ss(_mm_div_ss(_mm_set_ss(time), _mm_set_ss(interval)), _mm_set_ss(0.5f)));
}
std::int32_t receive_time_to_tick(float time, float interval) noexcept {
    const float rounded = static_cast<float>(
        static_cast<long double>(time) / static_cast<long double>(interval) + 0.5L);
    return _mm_cvtt_ss2si(_mm_set_ss(rounded));
}
std::int32_t corrected_command_tick(std::int32_t tick, const std::int32_t* previous,
                                    const std::int32_t* current) noexcept {
    if (previous && current) {
        const auto delta = std::uint32_t(*current) - std::uint32_t(*previous);
        if (delta - 1u < 9u)
            tick = std::bit_cast<std::int32_t>(std::uint32_t(tick) + delta);
    }
    return tick;
}
} // namespace skeet::lagcomp
