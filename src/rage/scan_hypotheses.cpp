#include "skeet/rage/scan_hypotheses.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/movement/steering_math.hpp"
#include <bit>
#include <cstring>

namespace skeet::rage {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
float negate(float value) noexcept {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) ^ 0x80000000u);
}
}
float mean_scan_history_yaw_change(const client::NativePlayerStateLayout& history) noexcept {
    const auto count = history.field_238;
    if (count < 2)
        return 0.0f;
    const auto first = read<std::uint32_t>(&history, 0x234);
    float total = 0.0f, differences = 0.0f;
    for (std::uint32_t ordinal = 1; ordinal < count; ++ordinal) {
        const auto previous = (first + ordinal - 1u) % 6u, current = (first + ordinal) % 6u;
        const auto delta = read<float>(&history, 0x258 + previous * 0x20) -
                           read<float>(&history, 0x258 + current * 0x20);
        auto difference = normalize_movement_angle(delta);
        if (difference < 0.0f)
            difference = negate(difference);
        total += difference;
        differences += 1.0f;
    }
    return total / differences;
}

void refresh_scan_hypothesis_sets(const ScanHypothesisContext& context, void* target,
                                  const lagcomp::LagRecord& selected,
                                  std::span<HitboxMatrices, 3> matrices,
                                  std::span<Matrix3x4, 256> scratch) {
    const auto index = engine::invoke<std::int32_t>(static_cast<std::byte*>(target) + 8, 10);
    auto& history = context.players.get(std::uint32_t(index))->native;
    const auto& common = context.preparation.entity.offsets;
    const auto first = read<std::uint32_t>(target, common.field_432b1f5b);
    const auto second = read<std::uint32_t>(target, common.field_0d560fac);
    const auto eye = read<std::array<std::uint32_t, 2>>(target, common.eye_angles);
    (void)lagcomp::prepare_entity_angles(context.players, target, selected, false,
                                         context.preparation);
    auto* collision = engine::invoke<void*>(target, 3);
    auto* maximum = engine::invoke<lagcomp::Vector*>(collision, 2);
    const auto height = (*maximum)[2];
    (*maximum)[2] = selected.header.collision_max_z;
    const auto duck = read<std::uint32_t>(target, common.duck_amount);
    write(target, common.duck_amount, history.cached_duck_amount);
    const auto saved_6e = read<std::uint32_t>(target, context.offsets.field_6e9852d6);
    const auto saved_2a = read<lagcomp::Vector>(target, context.offsets.field_2a671b42);
    write(target, context.offsets.field_2a671b42, selected.header.origin);
    const auto saved_4e = read<lagcomp::Vector>(target, context.offsets.field_4e66bb34);
    write(target, context.offsets.field_4e66bb34, selected.header.origin);
    const auto flags = read<std::uint32_t>(target, context.offsets.field_83a6cf2b);
    write(target, context.offsets.field_83a6cf2b, flags & 0xFFFFF7FFu);
    for (auto& set : matrices)
        legit::refresh_hitbox_attachments(target, context.model, set, scratch, common.bone_accessor,
                                          context.update_attachments);
    write(target, context.offsets.field_83a6cf2b, flags);
    write(target, context.offsets.field_4e66bb34, saved_4e);
    write(target, context.offsets.field_2a671b42, saved_2a);
    write(target, context.offsets.field_6e9852d6, saved_6e);
    write(target, common.duck_amount, duck);
    (*maximum)[2] = height;
    write(target, common.eye_angles + 4, eye[1]);
    write(target, common.eye_angles, eye[0]);
    write(target, common.field_0d560fac, second);
    write(target, common.field_432b1f5b, first);
}

void build_scan_hypotheses(const ScanHypothesisContext& context, void* target,
                           const lagcomp::LagRecord& selected, const lagcomp::LagRecord& source,
                           ScanHypothesisMatrices& matrices, std::span<Matrix3x4, 256> scratch) {
    constexpr std::array<float, 3> offsets{-60.0f, 0.0f, 60.0f};
    const auto eye_yaw = context.preparation.entity.offsets.eye_angles + 4;
    for (std::size_t group = 0; group < offsets.size(); ++group) {
        const auto yaw = read<float>(target, eye_yaw);
        write(target, eye_yaw, offsets[group] + yaw);
        for (std::size_t variant = 0; variant < 3; ++variant)
            std::memcpy(&matrices[group][variant], &source.alternatives[variant].matrices,
                        sizeof(HitboxMatrices));
        refresh_scan_hypothesis_sets(context, target, selected, matrices[group], scratch);
        write(target, eye_yaw, yaw);
    }
}

bool build_optional_scan_hypotheses(const ScanHypothesisContext& context, void* target,
                                    std::uint32_t entity_index, const lagcomp::LagRecord& selected,
                                    const lagcomp::LagRecord* source, bool enabled,
                                    std::uint32_t safe_hitboxes, ScanHypothesisMatrices& matrices,
                                    std::span<Matrix3x4, 256> scratch) {
    if (!enabled || !safe_hitboxes || !source || !(source->header.flags & 0x80u))
        return false;
    const auto& history = context.players.get(entity_index)->native;
    if (!(mean_scan_history_yaw_change(history) > 30.0f))
        return false;
    build_scan_hypotheses(context, target, selected, *source, matrices, scratch);
    return true;
}
} // namespace skeet::rage
