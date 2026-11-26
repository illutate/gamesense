#include "skeet/animation/remote_setup_bones.hpp"
#include "skeet/animation/remote_animation_preparation.hpp"
#include <cstring>
#include <intrin.h>
#include <xmmintrin.h>

namespace skeet::animation {
namespace {
RemoteSetupBonesRuntime* active_runtime;
template <class T> T read(const void* object, std::ptrdiff_t offset) noexcept {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, const T& value) noexcept {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof value);
}
void copy_vector(RageVector& destination, const RageVector& source) noexcept {
    // Three sequential MOVSD instructions, including their alias behavior.
    for (std::size_t i = 0; i < 3; ++i)
        write(&destination, i * 4, read<std::uint32_t>(&source, i * 4));
}
void copy_bones(Matrix3x4* destination, const Matrix3x4* source, std::int32_t count) noexcept {
    // All three 16-byte rows are loaded before the first store for each bone.
    // A whole-range memcpy/memmove would change native overlap behavior.
    for (std::int32_t i = 0; i < count; ++i) {
        const auto first = _mm_loadu_ps(source[i].rows[0]);
        const auto second = _mm_loadu_ps(source[i].rows[1]);
        const auto third = _mm_loadu_ps(source[i].rows[2]);
        _mm_storeu_ps(destination[i].rows[0], first);
        _mm_storeu_ps(destination[i].rows[1], second);
        _mm_storeu_ps(destination[i].rows[2], third);
    }
}
}

std::uint8_t& native_remote_bone_thread_guard() noexcept {
    return *reinterpret_cast<std::uint8_t*>(std::uintptr_t(__readfsdword(0x18)) + 0x1B8);
}

void RemoteSetupBonesRuntime::refresh_attachments(void* player) const {
    const auto& b = bindings;
    if (!b.update_attachments)
        return;
    const auto flags = b.bones.fields.entity_flags_83a6cf2b;
    write(player, flags, read<std::uint32_t>(player, flags) | 8u);
    const auto saved_time = read<std::uint32_t>(&b.frame_time, 0);
    write(&b.frame_time, 0, std::uint32_t(0));
    const auto update = b.update_attachments;
    auto* wrapper = read<void*>(player, b.bones.fields.studio_wrapper_32e4daf1);
    if (wrapper && !read<void*>(wrapper, 0))
        wrapper = nullptr;
    update(player, wrapper);
    auto handle = read<std::uint32_t>(player, b.fields.transform_guard_49d93e62 - 0x14);
    while (handle != UINT32_MAX) {
        auto* linked = engine::invoke<void*>(b.bones.entity_list, 4, handle);
        if (!linked)
            break;
        write(linked, flags, read<std::uint32_t>(linked, flags) | 0x800u);
        engine::invoke<void>(linked, 10);
        handle = read<std::uint32_t>(linked, b.fields.transform_guard_49d93e62 - 0x10);
    }
    write(player, flags, read<std::uint32_t>(player, flags) & ~8u);
    write(&b.frame_time, 0, saved_time);
}

bool RemoteSetupBonesRuntime::setup_bones(void* renderable, Matrix3x4* output, std::int32_t maximum,
                                          std::uint32_t mask, float /*time*/) {
    const auto& b = bindings;
    auto* player = static_cast<std::byte*>(renderable) - 4;
    if (read<std::int32_t>(player, b.bones.fields.model_index_a25d8271) == -1)
        return false;
    const auto* wrapper = read<const void*>(player, b.bones.fields.studio_wrapper_32e4daf1);
    if (!wrapper)
        return false;
    const auto* header = read<const void*>(wrapper, 0);
    if (!header)
        return false;
    if (read<std::int32_t>(header, 0x150) && !read<const void*>(wrapper, 4))
        return false;
    if (b.prepare_on_engine_thread) {
        const auto predicate = b.on_engine_thread;
        if (predicate && predicate())
            b.prepare_on_engine_thread(player);
    }
    if (!output && maximum == -1 && mask == 0x200u)
        return (read<std::uint32_t>(player, b.bones.fields.bone_accessor_54783141 + 8) & 0x200u) !=
               0;

    const auto index = engine::invoke<std::uint32_t>(player + 8, 10);
    auto& state = b.players.get(index)->native;
    const auto counter = b.current_bone_counter;
    if (!counter || read<std::int32_t>(player, b.fields.recent_bone_counter_2b554ad0) != *counter) {
        const auto requested_frame = read<std::int32_t>(&state, 0x168);
        if (read<std::int32_t>(&state, 0xE0) != requested_frame) {
            if (!b.thread_guard()) {
                b.thread_guard() = 1;
                b.preparation.prepare(player);
                auto* origin = engine::invoke<RageVector*>(renderable, 1);
                auto& cached_origin =
                    *reinterpret_cast<RageVector*>(reinterpret_cast<std::byte*>(&state) + 0xD4);
                copy_vector(cached_origin, *origin);
                write(&state, 0xE0, requested_frame);
                auto* pitch = player + b.fields.view_angles_e23a9be1;
                const auto saved_pitch = read<std::uint32_t>(pitch, 0);
                write(pitch, 0, read<std::uint32_t>(&state, 0x1A8));
                const auto saved_yaw = read<std::uint32_t>(pitch, 4);
                write(pitch, 4, read<std::uint32_t>(&state, 0x1AC));
                std::int32_t bone_count =
                    read<std::int32_t>(player, b.fields.bone_cache_17978090 + 0xC);
                const auto options =
                    read<std::uint32_t>(player, b.bones.fields.setup_flags_bb78dc43);
                auto* bones = read<Matrix3x4*>(player, b.fields.bone_cache_17978090);
                const auto angles = *engine::invoke<RageVector*>(renderable, 2);
                const auto build_origin = *engine::invoke<RageVector*>(renderable, 1);
                const BoneRequest request{angles, build_origin, 0.0f, 0xFFF00u, -1, options, true};
                const auto succeeded = b.bones.build_bone_matrices(
                    player, request, {bones, maximum_bones}, &bone_count);
                if (!succeeded) {
                    b.thread_guard() = 0;
                    write(&state, 0xE0, std::int32_t(-1));
                    write(pitch, 4, saved_yaw);
                    write(pitch, 0, saved_pitch);
                    return false;
                }
                refresh_attachments(player);
                b.thread_guard() = 0;
                write(player, b.bones.fields.bone_accessor_54783141 + 8, 0xFFF00u);
                write(player, b.bones.fields.bone_accessor_54783141 + 0xC, 0xFFF00u);
                if (b.current_bone_counter)
                    write(player, b.fields.recent_bone_counter_2b554ad0, *b.current_bone_counter);
                write(pitch, 4, saved_yaw);
                write(pitch, 0, saved_pitch);
            }
        } else {
            if (counter)
                write(player, b.fields.recent_bone_counter_2b554ad0, *counter);
            const auto* origin = engine::invoke<RageVector*>(renderable, 1);
            auto& cached_origin =
                *reinterpret_cast<RageVector*>(reinterpret_cast<std::byte*>(&state) + 0xD4);
            const float x = (*origin)[0] - cached_origin[0];
            const float y = (*origin)[1] - cached_origin[1];
            const float z = (*origin)[2] - cached_origin[2];
            const float distance = (y * y + x * x) + z * z;
            if (distance > 0.00009999999747378752f) {
                auto* bones = read<Matrix3x4*>(player, b.fields.bone_cache_17978090);
                const auto count = read<std::int32_t>(player, b.fields.bone_cache_17978090 + 0xC);
                for (std::int32_t i = 0; i < count; ++i) {
                    bones[i].rows[0][3] = bones[i].rows[0][3] + x;
                    bones[i].rows[1][3] = y + bones[i].rows[1][3];
                    bones[i].rows[2][3] = bones[i].rows[2][3] + z;
                }
                copy_vector(cached_origin, *origin);
                refresh_attachments(player);
            }
        }
    }
    if (output) {
        const auto count = read<std::int32_t>(player, b.fields.bone_cache_17978090 + 0xC);
        if (maximum < count)
            return false;
        const auto* bones = read<const Matrix3x4*>(player, b.fields.bone_cache_17978090);
        copy_bones(output, bones, count);
    }
    return true;
}

#if defined(_M_IX86) || defined(__i386__)
#define SKEET_REMOTE_BONES_ENTRY __fastcall
#else
#define SKEET_REMOTE_BONES_ENTRY
#endif
namespace {
bool SKEET_REMOTE_BONES_ENTRY setup_bones_entry(void* renderable, void*, Matrix3x4* output,
                                                std::int32_t maximum, std::uint32_t mask,
                                                float time) {
    return active_runtime->setup_bones(renderable, output, maximum, mask, time);
}
}
void RemoteSetupBonesRuntime::bind(engine::VtableHookMetadata& descriptor) noexcept {
    active_runtime = this;
    descriptor.replacement_table[13] = source_entry();
}
void* RemoteSetupBonesRuntime::source_entry() noexcept {
    return reinterpret_cast<void*>(&setup_bones_entry);
}
#undef SKEET_REMOTE_BONES_ENTRY
} // namespace skeet::animation
