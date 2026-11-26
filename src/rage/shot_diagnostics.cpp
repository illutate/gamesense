#include "skeet/rage/shot_diagnostics.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/rage/pending_shot_publication.hpp"
#include "skeet/client/buffered_shot_lifecycle.hpp"
#include "skeet/engine/interface_call.hpp"
#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <xmmintrin.h>

namespace skeet::rage {
namespace {
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
std::int32_t diagnostic_tick(float time, float interval) noexcept {
    const auto quotient = _mm_div_ss(_mm_set_ss(time), _mm_set_ss(interval));
    return _mm_cvtt_ss2si(_mm_add_ss(quotient, _mm_set_ss(0.5f)));
}
std::uint32_t handle(void* entity) {
    return *engine::invoke<const std::uint32_t*>(entity, 2);
}
std::uint32_t handle_index(std::uint32_t value) noexcept {
    return value == UINT32_MAX ? 0x1FFFu : value & 0xFFFFu;
}
void* networkable(void* entity) noexcept {
    return static_cast<std::byte*>(entity) + 8;
}
// Complete43364942 with the actual ClientClass creation function and engine
// registration calls. No reconstructed entity lifecycle is supplied as a callback.
void* create_shot_entity(const char* path, const ShotModelPublicationBindings& b) {
    const auto highest = read<std::int32_t>(b.entity_list, 0x24);
    if (!b.create_entity)
        throw std::logic_error("Shot render entity requires the native ClientClass factory");
    auto* created = b.create_entity(0x7FE, 0x7FF);
    if (!created)
        return nullptr;
    if (b.register_entity && b.unregister_entity) {
        auto* unknown = engine::invoke<void*>(created, 0);
        auto* entity = engine::invoke<void*>(unknown, 7);
        b.register_entity(b.registration_receiver, handle_index(handle(entity)));
        write(b.entity_list, 0x24, highest);
        if (engine::invoke<std::int32_t>(b.model_info, 2, path) != -1 &&
            engine::invoke<bool>(entity, 0x184 / 4, path, false))
            write(entity, 0x64, handle_index(handle(entity)));
        else {
            auto invalid = UINT32_MAX;
            b.unregister_entity(b.registration_receiver, &invalid, entity);
        }
    }
    engine::invoke<void>(created, 3, std::int32_t(0));
    return engine::invoke<void*>(engine::invoke<void*>(created, 0), 7);
}
void publish_vector(void* entity, std::ptrdiff_t offset, const RageVector& value,
                    std::int32_t change, const ShotModelPublicationBindings& b) {
    const auto previous = read<RageVector>(entity, offset);
    // Native ordered comparisons treat every NaN as a changed component.
    if (previous[0] == value[0] && previous[1] == value[1] && previous[2] == value[2])
        return;
    if (b.invalidate_transform)
        b.invalidate_transform(entity, change);
    write(entity, offset, value);
}
void force_opaque(void* entity, const ShotModelPublicationBindings& b) {
    const auto color = read<std::uint32_t>(entity, b.offsets.color);
    if ((color >> 24) == 0xFFu || !b.color_proxy || !*b.color_proxy)
        return;
    // Complete4341AE71. Unwritten proxy-packet bytes retain their stack values.
    std::array<std::byte, 0x30> packet;
    write(packet.data(), 8, color | 0xFF000000u);
    write(packet.data(), 0x14, std::uint32_t(1));
    (*b.color_proxy)(reinterpret_cast<const engine::RecvProxyData*>(packet.data()), entity,
                     static_cast<std::byte*>(entity) + b.offsets.color);
}
struct PreparationFields {
    void* target;
    const lagcomp::EntityNetvars& offsets;
    std::uint32_t collision_first, collision_second;
    std::array<std::uint32_t, 2> angles;
    PreparationFields(void* entity, const lagcomp::EntityNetvars& o) noexcept
        : target(entity), offsets(o),
          collision_first(read<std::uint32_t>(entity, o.field_432b1f5b)),
          collision_second(read<std::uint32_t>(entity, o.field_0d560fac)),
          angles(read<std::array<std::uint32_t, 2>>(entity, o.eye_angles)) {}
    ~PreparationFields() {
        write(target, offsets.eye_angles + 4, angles[1]);
        write(target, offsets.eye_angles, angles[0]);
        write(target, offsets.field_0d560fac, collision_second);
        write(target, offsets.field_432b1f5b, collision_first);
    }
};
}

void capture_shot_diagnostics(const client::CommandFrame& frame, void* local, void* target,
                              const ShotDiagnosticBindings& b) {
    //43329B33 reads43475968 once. This runs after the caller's real entity-list
    // lookup; a binding retains the pointer cell, never an earlier owner/view.
    auto* diagnostics = client::buffered_shot_diagnostics(b.buffered_shot_owner);
    if (!diagnostics)
        return;
    auto& history = *diagnostics;
    const auto incremented = history.count + 1u;
    history.count = incremented < 6u ? incremented : 6u;
    if (history.count != 1u)
        std::memmove(history.entries.data() + 1, history.entries.data(),
                     std::size_t(history.count - 1u) * sizeof(ShotDiagnosticEntry));
    auto& entry = history.entries[0];
    std::memset(&entry, 0, sizeof entry);
    entry.clock = *b.clock;
    entry.command = frame.command_number_ac;
    entry.target_tick = diagnostic_tick(read<float>(target, b.offsets.simulation_time), b.interval);
    const auto index = engine::invoke<std::int32_t>(networkable(target), 10);
    auto& cached = b.players.get(std::uint32_t(index))->native;
    entry.target_field_168 = read<std::uint32_t>(&cached, 0x168);
    entry.target_flags = read<std::uint32_t>(target, b.offsets.player_flags);
    entry.target_height = cached.cached_target_height;
    entry.target_duck = read<float>(target, b.offsets.duck_amount);
    entry.target_reference_yaw = cached.reference_yaw;
    entry.local_field_7f5ed2d0 = read<std::uint32_t>(local, b.offsets.local_field_7f5ed2d0);
    entry.local_tick = diagnostic_tick(read<float>(local, b.offsets.simulation_time), b.interval);
    entry.target_handle = handle(target);
    capture_shot_weapon_bullet(b.weapon_system, std::uint16_t(frame.weapon_id_d8), entry.bullet);
}

ShotModelBoneStorage::ShotModelBoneStorage(engine::JobMemory memory)
    : begin_(standalone_begin_), end_(standalone_end_), limit_(standalone_limit_), memory_(memory) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap)
        throw std::invalid_argument("Shot matrices require genuine process-heap services");
}
ShotModelBoneStorage::ShotModelBoneStorage(engine::NativeDataImage& data, engine::JobMemory memory)
    : begin_(data.native_cell<Matrix3x4*>(0x434797A0u)),
      end_(data.native_cell<Matrix3x4*>(0x434797A4u)),
      limit_(data.native_cell<Matrix3x4*>(0x434797A8u)), memory_(memory) {
    if (!memory_.allocate || !memory_.free || !memory_.process_heap || begin_ || end_ || limit_)
        throw std::invalid_argument(
            "Source shot matrices require an unowned canonical vector and genuine heap services");
}
ShotModelBoneStorage::~ShotModelBoneStorage() {
    finalize();
}
std::span<Matrix3x4> ShotModelBoneStorage::matrices() const noexcept {
    return begin_ ? std::span<Matrix3x4>(begin_, static_cast<std::size_t>(end_ - begin_))
                  : std::span<Matrix3x4>{};
}
void ShotModelBoneStorage::finalize() {
    if (begin_)
        engine::release_owner_storage(memory_, begin_);
    begin_ = end_ = limit_ = nullptr;
}
void ShotModelBoneStorage::resize(std::size_t count) {
    const auto current = begin_ ? static_cast<std::size_t>(end_ - begin_) : 0u;
    if (count <= current) {
        end_ = begin_ ? begin_ + count : nullptr;
        return;
    }
    const auto additional = count - current;
    const auto available = begin_ ? static_cast<std::size_t>(limit_ - end_) : 0u;
    if (additional > available) {
        const auto capacity = (std::max)(count, current ? current * 2u : std::size_t(1));
        if (capacity > (UINT32_MAX - 0x20u) / sizeof(Matrix3x4))
            throw std::length_error(
                "Shot matrix allocation exceeds the native header representation");
        auto* replacement = static_cast<Matrix3x4*>(
            engine::allocate_owner_storage(memory_, capacity * sizeof(Matrix3x4)));
        // The successful native resize assumes allocation succeeds. Report an
        // explicit source OOM failure instead of inventing its null-copy edge.
        if (!replacement)
            throw std::bad_alloc();
        //4336EB90/95 reload the live range after the real heap boundary.
        auto* first = begin_;
        auto* last = end_;
        const auto copied = first ? static_cast<std::size_t>(last - first) : 0u;
        if (copied)
            std::memcpy(replacement, first, copied * sizeof(Matrix3x4));
        std::memset(replacement + copied, 0, additional * sizeof(Matrix3x4));
        auto* next_end = replacement + copied + additional;
        if (begin_)
            engine::release_owner_storage(memory_, begin_);
        // Native publication order is first, capacity, then end.
        begin_ = replacement;
        limit_ = replacement + capacity;
        end_ = next_end;
    } else {
        std::memset(end_, 0, additional * sizeof(Matrix3x4));
        end_ += additional;
    }
}

void publish_accepted_shot_model(const client::CommandFrame& frame,
                                 const TargetCandidate& candidate, const lagcomp::LagRecord& record,
                                 const ShotModelPublicationBindings& b) {
    if (!b.settings.on_shot.enabled.value)
        return;
    auto* target = engine::invoke<void*>(b.entity_list, 3, candidate.entity_index);
    const auto* wrapper = read<const std::byte*>(target, b.offsets.studio_header);
    if (!wrapper)
        return;
    if (!read<const void*>(wrapper, 0))
        return;
    const auto& common = b.preparation.entity.offsets;
    std::array<Matrix3x4, animation::maximum_bones> saved_bones;
    lagcomp::EntityRecordScope scope(b.preparation.entity, target, saved_bones);
    const lagcomp::EntityHitboxModelOffsets model_offsets{
        b.bone_runtime.fields.studio_wrapper_32e4daf1, b.bone_runtime.fields.hitbox_set_c2b6fb10};
    scope.apply_live_model(record, model_offsets, b.current_bone_counter);
    PreparationFields saved_fields(target, common);
    auto preparation = b.preparation;
    auto timing = b.timing;
    timing.tick_d4 = std::bit_cast<std::int32_t>(frame.field_d4);
    timing.tick_18c = std::bit_cast<std::int32_t>(frame.field_18c);
    preparation.current_timing = &timing;
    lagcomp::prepare_entity_angles(b.players, target, record, true, preparation);
    //4331F545 retains the original wrapper and rereads its header after the
    // record application and preparation callbacks, before resizing matrices.
    const auto* studio = read<const std::byte*>(wrapper, 0);
    const auto count = read<std::int32_t>(studio, 0x9C);
    if (count < 0 || count > std::int32_t(animation::maximum_bones))
        throw std::length_error("Shot studio bone count must fit the native256-bone workspace");
    b.bones.resize(std::size_t(count));
    const auto output = b.bones.matrices();
    b.published.bones_begin = output.data();
    b.published.bones_end = output.empty() ? output.data() : output.data() + output.size();
    void* clone = b.shot_entity_handle == UINT32_MAX
                      ? nullptr
                      : engine::invoke<void*>(b.entity_list, 4, b.shot_entity_handle);
    if (!clone) {
        const auto model = read<std::int16_t>(target, b.offsets.model_index);
        const auto* model_pointer =
            engine::invoke<const void*>(b.model_info, 1, std::int32_t(model));
        const auto* path = engine::invoke<const char*>(b.model_info, 3, model_pointer);
        if (auto* created = create_shot_entity(path, b))
            b.shot_entity_handle = handle(created);
        if (b.shot_entity_handle != UINT32_MAX)
            clone = engine::invoke<void*>(b.entity_list, 4, b.shot_entity_handle);
        if (!clone)
            return;
        write(clone, b.offsets.entity_flags,
              read<std::uint32_t>(clone, b.offsets.entity_flags) | 0x4858u);
        write(clone, b.offsets.byte_8481ea04, std::uint8_t(0));
        engine::invoke<void>(networkable(clone), 3, std::int32_t(0));
        write(clone, common.simulation_time - 8, std::numeric_limits<float>::max());
        write(clone, b.offsets.interpolation_flags,
              std::uint8_t(read<std::uint8_t>(clone, b.offsets.interpolation_flags) | 2u));
        write(clone, b.offsets.hitbox_set - 0xC, std::uint8_t(0));
    }
    if (engine::invoke<bool>(networkable(clone), 9))
        engine::invoke<void>(networkable(clone), 3, std::int32_t(0));
    const auto model_index = read<std::int16_t>(target, b.offsets.model_index);
    if (read<std::int16_t>(clone, b.offsets.model_index) != model_index) {
        engine::invoke<void>(b.model_cache, 33);
        engine::invoke<void>(clone, 75, std::int32_t(model_index));
        engine::invoke<void>(b.model_cache, 34);
        engine::invoke<void>(clone, 112);
    }
    engine::invoke<void>(clone, 54);
    engine::invoke<void>(clone, 238, false);
    write(clone, b.offsets.hitbox_set, read<std::uint32_t>(target, b.offsets.hitbox_set));
    write(clone, b.offsets.field_950f7b20, read<std::uint32_t>(target, b.offsets.field_950f7b20));
    write(clone, b.offsets.model_instance,
          engine::invoke<std::uint32_t>(static_cast<std::byte*>(target) + 4, 38));
    write(clone, b.offsets.field_a25d8271, std::uint32_t(0));
    publish_vector(clone, b.offsets.local_origin, record.header.origin, 1, b);
    publish_vector(clone, b.offsets.local_angles, {0.0f, record.header.animation_yaw, 0.0f}, 2, b);
    auto* layers = static_cast<std::byte*>(clone) + common.animation_layers;
    lagcomp::restore_animation(
        record.animation, read<void*>(layers, 0), read<std::int32_t>(layers, 0xC),
        reinterpret_cast<float*>(static_cast<std::byte*>(clone) + common.pose_parameters));
    write(clone, common.bone_accessor + 8, 0xFFF00u);
    write(clone, common.bone_accessor + 12, 0xFFF00u);
    if (b.current_bone_counter)
        write(clone, common.recent_bone_counter, *b.current_bone_counter);
    write(clone, b.offsets.entity_flags,
          read<std::uint32_t>(clone, b.offsets.entity_flags) & ~0x20u);
    write(clone, common.effects_bb78dc43, std::uint32_t(4));
    if (read<std::uint8_t>(clone, b.offsets.render_mode) != 3u) {
        write(clone, b.offsets.render_mode, std::uint8_t(3));
        auto* property = engine::invoke<void*>(clone, 9);
        engine::invoke<void>(property, 2,
                             std::uint32_t(read<std::uint8_t>(clone, b.offsets.render_effect)),
                             std::int32_t(3), std::numeric_limits<float>::max(), 0.0f);
    }
    force_opaque(clone, b);
    engine::invoke<void>(clone, 55, false);
    const animation::BoneRequest request{{0.0f, record.header.animation_yaw, 0.0f},
                                         record.header.origin,
                                         read<float>(target, common.simulation_time),
                                         0xFFF00u,
                                         -1,
                                         1u,
                                         false};
    std::int32_t built_count = 0;
    if (b.bone_runtime.build_bone_matrices(target, request, output, &built_count)) {
        b.published.origin = record.header.origin;
        b.published.expires =
            engine::invoke<double>(b.clock, 5) + double(b.settings.on_shot_duration.value) * 0.1;
    }
}
} // namespace skeet::rage
