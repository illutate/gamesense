#include "skeet/effects/model_render.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstring>

namespace skeet::effects {
namespace {
constexpr float byte_scale = 0.0039215688593685626983642578125f;
constexpr std::array<float, 4> night_color{0.35f, 0.34f, 0.36f, 1.0f}; //4346DBF0
template <class T> T read(const void* object, std::size_t offset) {
    T result;
    std::memcpy(&result, static_cast<const std::byte*>(object) + offset, sizeof result);
    return result;
}
template <class... Args> void original(void* object, std::size_t slot, Args... args) {
    auto** table = *static_cast<void***>(object);
    const auto* descriptor = read<void*>(reinterpret_cast<const std::byte*>(table) - 8, 0);
    auto** methods = read<void**>(descriptor, 8);
    reinterpret_cast<engine::MemberFunction<void, Args...>>(methods[slot])(object, args...);
}
std::array<float, 4> prop_color(std::uint32_t packed) {
    return {float(packed & 255) * byte_scale, float((packed >> 8) & 255) * byte_scale,
            float((packed >> 16) & 255) * byte_scale, 1.0f};
}
void multiply(void* record, const std::array<float, 4>& color) {
    auto* bytes = static_cast<std::byte*>(record);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto offset = 0x38 + channel * 4;
        const float result = read<float>(record, offset) * color[channel];
        std::memcpy(bytes + offset, &result, sizeof result);
    }
}
}
void ModelRenderRuntime::draw_prop_records_50(void* renderer, std::uintptr_t argument,
                                              std::int32_t count, void* records,
                                              std::uintptr_t last,
                                              const esp::EffectsSettings& effects,
                                              bool suppressed) {
    if (!suppressed && records && (settings.props.value || effects.brightness.value == 2)) {
        const auto color =
            settings.props.value ? prop_color(settings.props_color.value) : night_color;
        auto* record = static_cast<std::byte*>(records);
        for (std::int32_t n = 0; n < count; ++n, record += 0x50)
            multiply(record, color);
    }
    original(renderer, 47, argument, count, records, last);
}
void ModelRenderRuntime::draw_prop_records_48(void* renderer, std::uintptr_t argument,
                                              std::int32_t count, void* records,
                                              std::int32_t stride, std::uintptr_t last,
                                              const esp::EffectsSettings& effects,
                                              bool suppressed) {
    if (!suppressed && records && stride == 0x48) {
        if (settings.props.value || effects.brightness.value == 2) {
            const auto color =
                settings.props.value ? prop_color(settings.props_color.value) : night_color;
            auto* record = static_cast<std::byte*>(records);
            for (std::int32_t n = 0; n < count; ++n, record += 0x48)
                multiply(record, color);
        }
        if (effects.wall_transparency.value != 100) {
            const float alpha = float(effects.wall_transparency.value) * 0.01f;
            auto* record = static_cast<std::byte*>(records);
            for (std::int32_t n = 0; n < count; ++n, record += 0x48)
                std::memcpy(record + 0x44, &alpha, sizeof alpha);
        }
    }
    original(renderer, 49, argument, count, records, stride, last);
}
void ModelRenderRuntime::draw_prop_batches_60(void* renderer, std::uintptr_t argument,
                                              std::int32_t count, ModelPropBatch* batches,
                                              std::int32_t stride, std::uint32_t flags,
                                              void* ragdoll_renderable_vtable,
                                              const esp::EffectsSettings& effects,
                                              bool suppressed) {
    if (!(flags & 0x200) && !suppressed && batches && stride == 0x60 &&
        (settings.props.value || effects.brightness.value == 2)) {
        const auto color =
            settings.props.value ? prop_color(settings.props_color.value) : night_color;
        for (std::int32_t batch = 0; batch < count; ++batch) {
            auto* record = static_cast<std::byte*>(batches[batch].vertices);
            if (!record)
                continue;
            for (std::int32_t n = 0; n < batches[batch].count; ++n, record += 0x60) {
                if (!settings.props.value) {
                    void* renderable = read<void*>(record, 0x48);
                    if (renderable && read<void*>(renderable, 0) == ragdoll_renderable_vtable)
                        continue;
                }
                multiply(record, color);
            }
        }
    }
    original(renderer, 48, argument, count, batches, stride, flags);
}
void ModelRenderRuntime::draw_prop_studio(void* studio_render, std::uintptr_t first,
                                          std::uintptr_t second, std::uintptr_t third,
                                          const esp::EffectsSettings& effects, bool suppressed) {
    const auto saved = save_state(studio_render);
    if (!suppressed && (settings.props.value || effects.brightness.value == 2)) {
        const auto color =
            settings.props.value ? prop_color(settings.props_color.value) : night_color;
        engine::invoke<void>(studio_render, 27, color.data());
    }
    original(studio_render, 30, first, second, third);
    restore_state(studio_render, saved);
}
} // namespace skeet::effects
