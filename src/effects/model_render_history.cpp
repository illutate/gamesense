#include "skeet/effects/model_render.hpp"
#include "skeet/effects/model_render_sources.hpp"
#include "skeet/client/player_state.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/startup_convars.hpp"
#include <bit>
#include <cstring>
#include <new>
#include <stdexcept>
#include <xmmintrin.h>

namespace skeet::effects {
namespace {
float distance_square(const RageVector& a, const RageVector& b) {
    const auto y = a[1] - b[1], x = a[0] - b[0], z = a[2] - b[2];
    return (y * y + x * x) + z * z;
}
float maxss(float a, float b) {
    return _mm_cvtss_f32(_mm_max_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
float minss(float a, float b) {
    return _mm_cvtss_f32(_mm_min_ss(_mm_set_ss(a), _mm_set_ss(b)));
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::uint32_t(a) + std::uint32_t(b));
}
std::uint32_t record_count(const lagcomp::LagTimeline& timeline) {
    return timeline.record_count;
}
std::uint32_t record_count(const lagcomp::RecordRing& ring) {
    return ring.count;
}
template <class Timeline, class Timing>
bool select_pose(const Timeline& timeline, Timing timing, ModelShadowPose& output,
                 const lagcomp::RotationMath& math) {
    const auto count = record_count(timeline);
    if (count <= 1)
        return false;
    if (!(distance_square(timeline.at(0).header.origin, timeline.at(count - 1).header.origin) >
          25.0f))
        return false;
    const auto t = timing();
    float now = t.current_time;
    if (t.local_alive && t.prediction_enabled)
        now = ((float(t.tick_base) * t.interval - t.interval) + t.prediction_counter * t.interval) -
              t.prediction_correction;
    const auto interpolation =
        maximum_number(t.interpolation, t.interpolation_ratio / t.update_rate);
    auto requested =
        interpolation + float(timeline.at(count - 1).header.simulation_tick) * t.interval;
    requested = minss(maxss(requested, now - 0.18437500298023224f), now - (-0.18437500298023224f));
    const auto latency = lagcomp::mean_history_timing_ticks(
        {std::uint32_t(t.latency_sample_head), t.latency_sample_count, t.latency_samples});
    const auto corrected =
        lagcomp::corrected_command_tick(t.command_tick, t.previous_command, t.current_command);
    const auto seconds = _mm_cvttss_si32(_mm_set_ss(float(add(latency, corrected)) * t.interval -
                                                    float(t.maximum_unlag_ms) * 0.001f));
    requested = maxss(float(seconds), requested);
    const auto tick = _mm_cvttss_si32(_mm_set_ss(requested / t.interval + 0.5f));
    const lagcomp::LagRecord* previous = nullptr;
    const lagcomp::LagRecord* selected = nullptr;
    for (std::uint32_t n = 0; n < count; ++n) {
        selected = &timeline.at(n);
        if (previous && distance_square(previous->header.origin, selected->header.origin) > 4096.0f)
            return false;
        if (selected->header.simulation_tick <= tick || n + 1 == count)
            break;
        previous = selected;
    }
    ModelShadowPose pose{
        selected, requested, selected->header.origin, {0.0f, selected->header.animation_yaw, 0.0f}};
    if (previous && selected->header.simulation_tick < tick &&
        selected->header.simulation_tick < previous->header.simulation_tick) {
        const auto old_time = float(selected->header.simulation_tick) * t.interval;
        const auto fraction = (requested - old_time) /
                              (float(previous->header.simulation_tick) * t.interval - old_time);
        for (unsigned n = 0; n < 3; ++n)
            pose.origin[n] = (previous->header.origin[n] - selected->header.origin[n]) * fraction +
                             selected->header.origin[n];
        if (previous->header.animation_yaw != selected->header.animation_yaw) {
            const auto first =
                lagcomp::history_angle_quaternion({0, selected->header.animation_yaw, 0}, math);
            const auto second =
                lagcomp::history_angle_quaternion({0, previous->header.animation_yaw, 0}, math);
            pose.angles = lagcomp::history_quaternion_angles(
                lagcomp::slerp_history_quaternion(
                    first, lagcomp::align_history_quaternion(first, second), fraction, math),
                math);
        }
    }
    output = pose;
    return true;
}
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
float convar_float(engine::NativeConVarReference reference) {
    const auto* variable = reference.get();
    for (;;) {
        const auto* parent = field<const void*>(variable, 0x1C);
        if (parent == variable)
            break;
        variable = parent;
    }
    return std::bit_cast<float>(field<std::uint32_t>(variable, 0x2C) ^
                                std::uint32_t(reinterpret_cast<std::uintptr_t>(variable)));
}
}
bool select_model_shadow_pose(const lagcomp::LagTimeline& timeline, const ModelShadowTiming& timing,
                              ModelShadowPose& output, const lagcomp::RotationMath& math) {
    return select_pose(timeline, [&] { return timing; }, output, math);
}
ModelShadowTimingSource::ModelShadowTimingSource(engine::NativeDataImage& data,
                                                 const engine::NativeOffsetGlobals& offsets)
    : data_(data), offsets_(offsets) {}
ModelShadowTiming ModelShadowTimingSource::capture() const {
    ModelShadowTiming timing{};
    const auto variable = [&](std::uint32_t cell, std::uint32_t key) {
        return engine::NativeConVarReference(data_.native_cell<std::uint32_t>(cell), key);
    };
    auto* local = data_.native_cell<const void*>(0x43475808u);
    if (local)
        timing.local_alive =
            field<std::uint8_t>(local, engine::DecodedOffset(offsets_, 0x43468E78u, 0x434684E4u,
                                                             0x9D920E35u, 0)) == 0;
    //43407DE2 uses the float value: unordered (NaN) is enabled, exactly like !=0.
    if (timing.local_alive)
        timing.prediction_enabled = convar_float(variable(0x4346A850u, 0x478720C2u)) != 0.0f;
    timing.interval = *data_.native_cell<const float*>(0x4346A338u);
    if (timing.local_alive && timing.prediction_enabled) {
        timing.tick_base = field<std::int32_t>(
            local, engine::DecodedOffset(offsets_, 0x434675C8u, 0x4346A5B8u, 0x3EDECE61u, 0));
        timing.prediction_counter = field<float>(data_.native_cell<void*>(0x43467B04u), 0x24);
        timing.prediction_correction = data_.native_cell<float>(0x43476928u);
    } else
        timing.current_time = *data_.native_cell<const float*>(0x43467EB8u);
    timing.update_rate = convar_float(variable(0x4346747Cu, 0x03B4819Cu));
    timing.interpolation = convar_float(variable(0x4346A748u, 0x7D9DC86Fu));
    timing.interpolation_ratio = convar_float(variable(0x4346A330u, 0xBE64D223u));
    const auto& samples = data_.native_cell<lagcomp::TickTimingSamples>(0x43478C70u);
    timing.latency_sample_head = std::bit_cast<std::int32_t>(samples.first);
    timing.latency_sample_count = samples.count;
    timing.latency_samples = samples.values;
    timing.command_tick = field<std::int32_t>(data_.native_cell<void*>(0x43475A60u), 4);
    timing.previous_command = data_.native_cell<const std::int32_t*>(0x43468F9Cu);
    timing.current_command = data_.native_cell<const std::int32_t*>(0x434670A8u);
    timing.maximum_unlag_ms = data_.native_cell<std::int32_t>(0x43468230u);
    return timing;
}
ModelShadowSource::ModelShadowSource(client::PlayerStateCache& cache,
                                     const ModelShadowTimingSource& timing) noexcept
    : cache_(&cache), live_(&timing) {}
ModelShadowSource::ModelShadowSource(const std::array<const lagcomp::LagTimeline*, 64>& timelines,
                                     const ModelShadowTiming& timing) noexcept
    : timelines_(&timelines), timing_(&timing) {}
bool ModelShadowSource::select(std::uint32_t index, ModelShadowPose& pose,
                               const lagcomp::RotationMath& math) const {
    if (cache_) {
        auto* state = cache_->get(index);
        // Native433B8A2B's failed allocation is followed by an unchecked load.
        // Source propagation keeps that unresolved OOM edge explicit.
        if (!state)
            throw std::bad_alloc();
        return select_pose(state->history, [&] { return live_->capture(); }, pose, math);
    }
    const auto* timeline = (*timelines_)[index & 63u];
    return timeline && select_model_shadow_pose(*timeline, *timing_, pose, math);
}
} // namespace skeet::effects
