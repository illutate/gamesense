#include "skeet/effects/world.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/hooks.hpp"
#include "skeet/engine/native_data_image.hpp"
#include <bit>
#include <cstring>

namespace skeet::effects {
namespace {
using engine::invoke;
template <class T> T read(void* object, std::uint32_t offset) {
    T value;
    std::memcpy(&value, static_cast<std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::uint32_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
std::uint32_t hash_name(const char* text) {
    std::uint32_t value = 0x811C9DC5u;
    if (text)
        for (; *text; ++text)
            value = (value ^ static_cast<std::uint8_t>(*text)) * 0x01000193u;
    return value;
}
void* convar_parent(void* variable) {
    for (;;) {
        void* parent = read<void*>(variable, 0x1C);
        if (parent == variable)
            return variable;
        variable = parent;
    }
}
const esp::EffectsSettings* flash_settings = nullptr;
bool suppress_flash(const esp::EffectsSettings& settings, void* argument) {
    if (settings.remove_flash.value && argument && (read<std::uint8_t>(argument, 0x10) & 1)) {
        void* name = read<void*>(argument, 8);
        if (read<std::uint32_t>(name, 0x10) == 15) {
            const char* text = read<std::uint32_t>(name, 0x14) < 16 ? static_cast<const char*>(name)
                                                                    : read<const char*>(name, 0);
            const auto hash = hash_name(text) ^ 0x92CA5DF1u;
            if (hash == 0x51F0705Bu || hash == 0x77F6FC36u || hash == 0x69F55EF1u)
                return true;
        }
    }
    return false;
}
}

WorldInterfaces::WorldInterfaces(engine::NativeDataImage& data)
    : engine(data.native_cell<void*>(0x4346A514u)),
      material_system(data.native_cell<void*>(0x4346A4E0u)),
      static_props(data.native_cell<void*>(0x43467014u)),
      entity_list(data.native_cell<void*>(0x4346A890u)),
      max_clients(data.native_cell<const std::int32_t*>(0x43469300u)),
      tonemap_client_class(data.native_cell<void*>(0x434684C4u)),
      smoke_registry(data.native_cell<void*>(0x434679C0u)) {}
std::uint8_t classify_world_material(void* material, std::uint32_t group, bool dust2) {
    auto shader = invoke<const char*>(material, 49);
    if (shader) {
        const auto hash = hash_name(shader);
        if (hash == 0x288259D2u || hash == 0x57FE4F24u || hash == 0xC6D3B449u ||
            hash == 0xEC7340B0u || hash == 0xFAE67322u)
            return 0;
    }
    std::uint8_t flags = group == 0xBB2DA711u;
    if (flags || group == 0x2D3D8226u || group == 0xD402D032u) {
        if (invoke<std::int32_t>(material, 69) && !invoke<bool>(material, 17) &&
            !invoke<bool>(material, 30, std::uint32_t(0x200100))) {
            auto parameters = invoke<void**>(material, 41);
            bool first_empty =
                !parameters || !parameters[0] || !read<std::uint32_t>(parameters[0], 8);
            if (first_empty) {
                bool eligible = invoke<bool>(material, 19);
                if (!eligible) {
                    shader = invoke<const char*>(material, 49);
                    if (shader) {
                        const auto hash = hash_name(shader) ^ 0x92CA5DF1u;
                        eligible = hash == 0x9C79434Fu || hash == 0xFC16AAE8u;
                    }
                }
                if (eligible) {
                    const auto count = invoke<std::int32_t>(material, 40);
                    parameters = invoke<void**>(material, 41);
                    if (parameters && count > 6 && parameters[6] &&
                        invoke<bool>(parameters[6], 18)) {
                        bool phong = false;
                        if (dust2)
                            for (std::int32_t i = 0; i < count; ++i) {
                                void* parameter = parameters[i];
                                if (parameter &&
                                    (hash_name(invoke<const char*>(parameter, 2)) ^ 0x92CA5DF1u) ==
                                        0x5B95D50Au &&
                                    invoke<bool>(parameter, 18) &&
                                    invoke<std::int32_t>(parameter, 27)) {
                                    phong = true;
                                    break;
                                }
                            }
                        if (!phong)
                            flags |= 2;
                    }
                }
            }
        }
        if (invoke<bool>(material, 19))
            flags |= 4;
        if (group == 0x2D3D8226u || group == 0xD402D032u)
            return flags;
    }
    return flags | 8;
}

void apply_world_material(void* material, const WorldMaterialChange& change,
                          const std::array<void*, 9>& excluded) {
    const auto group = hash_name(invoke<const char*>(material, 1));
    if (group == 0x5CE5C520u || group == 0x74C16AE4u || group == 0x7E3CE5C1u ||
        group == 0xA1BEA871u || group == 0xF0813BA7u)
        return;
    for (void* owned : excluded)
        if (owned == material)
            return;
    void* actual = invoke<void*>(material, 99);
    const auto flags = classify_world_material(actual, group, change.dust2);
    if (!flags)
        return;
    if (change.refresh)
        invoke<void>(actual, 37);
    if (change.alpha && (flags & 1))
        invoke<void>(actual, 27, change.wall_alpha);
    if (change.fullbright) {
        if (flags & 2)
            invoke<void>(actual, 29, std::uint32_t(0x40), true);
        if (flags & 4)
            invoke<void>(actual, 29, std::uint32_t(0x08000000), true);
    }
    if (change.night && (flags & 8))
        invoke<void>(actual, 28, change.color[0], change.color[1], change.color[2]);
}

void WorldRuntime::update() {
    if (suspended)
        return;
    const auto prop = settings.prop_transparency.value;
    if (prop != previous_prop_transparency) {
        const auto multiplied = prop * std::uint32_t(255);
        const auto alpha = std::uint8_t(std::bit_cast<std::int32_t>(multiplied) / 100);
        for (std::int32_t index = 0;; ++index) {
            void* object = invoke<void*>(interfaces.static_props, 4, index);
            if (!object)
                break;
            object = invoke<void*>(object, 13);
            if (!object)
                continue;
            object = invoke<void*>(object, 9);
            if (!object)
                continue;
            if (prop < 100) {
                invoke<void>(object, 5, true);
                invoke<void>(object, 1, alpha);
            } else {
                invoke<void>(object, 1, std::uint8_t(255));
                invoke<void>(object, 5, false);
            }
        }
        previous_prop_transparency = settings.prop_transparency.value;
    }
    if (prop != 100 || previous_prop_transparency_2 != 100)
        previous_prop_transparency_2 = prop;
    const auto brightness = settings.brightness.value;
    const auto wall = settings.wall_transparency.value;
    const float wall_alpha = static_cast<float>(wall) * 0.01f;
    std::uint8_t changes = 0;
    if (bool(material_flags & 1) != (brightness == 1))
        changes = brightness == 1 ? 1 : 2;
    if (bool(material_flags & 4) != (wall != 100) || wall_alpha != previous_wall_alpha)
        changes |= wall == 100 ? 0x20 : 0x10;
    if (bool(material_flags & 2) != (brightness == 2))
        changes |= brightness == 2 ? 4 : 8;
    if (brightness == 2 && previous_night_rgb != (settings.night_color.value & 0xFFFFFFu))
        changes |= 4;
    if (!changes)
        return;
    WorldMaterialChange change;
    change.refresh = (changes & 0x2A) != 0;
    change.alpha = (change.refresh && wall != 100) || (changes & 0x10);
    change.wall_alpha = wall_alpha;
    change.fullbright = (change.refresh && brightness == 1) || (changes & 1);
    change.night = (change.refresh && brightness == 2) || (changes & 4);
    if (change.fullbright) {
        const char* level = invoke<const char*>(interfaces.engine, 53);
        change.dust2 = level && (hash_name(level) ^ 0x92CA5DF1u) == 0x1D7A7522u;
    }
    if (change.night)
        for (unsigned i = 0; i != 3; ++i)
            change.color[i] =
                float(std::uint8_t(settings.night_color.value >> (i * 8))) * 0.003921568859368563f;
    const auto context = invoke<void*>(interfaces.material_system, 118);
    auto handle = invoke<std::uint16_t>(interfaces.material_system, 86);
    while (handle != invoke<std::uint16_t>(interfaces.material_system, 88)) {
        void* material = invoke<void*>(interfaces.material_system, 89, handle);
        if (material && invoke<std::int32_t>(material, 56) > 0 && !invoke<bool>(material, 42) &&
            invoke<std::int32_t>(material, 69))
            apply_world_material(material, change, excluded_materials);
        handle = invoke<std::uint16_t>(interfaces.material_system, 87, handle);
    }
    invoke<void>(interfaces.material_system, 119, context);
    if (changes & 0xC) {
        const auto limit = read<std::int32_t>(interfaces.entity_list, 0x24);
        for (std::int32_t index = *interfaces.max_clients + 1; index < limit; ++index) {
            if (std::uint32_t(index) & 0xFFFF0000u)
                continue;
            void* entity = invoke<void*>(interfaces.entity_list, 3, index);
            if (entity && invoke<void*>(static_cast<std::byte*>(entity) + 8, 2) ==
                              interfaces.tonemap_client_class) {
                invoke<void>(static_cast<std::byte*>(entity) + 8, 6, std::int32_t(1));
                write(entity, offsets.tonemap_disabled,
                      std::uint8_t(settings.brightness.value != 2));
                break;
            }
        }
    }
    if (changes & 0x10)
        previous_wall_alpha = wall_alpha;
    else if (changes >= 0x20)
        previous_wall_alpha = 1.0f;
    if (changes & 4)
        previous_night_rgb = settings.night_color.value & 0xFFFFFFu;
    material_flags = (material_flags & 0xF8) | (settings.brightness.value == 1) |
                     (settings.brightness.value == 2 ? 2 : 0) |
                     (settings.wall_transparency.value == 100 ? 0 : 4);
}

void WorldConvarOverride::set(std::uint32_t value) {
    if (!variable)
        return;
    auto* parent = convar_parent(variable);
    saved =
        read<std::uint32_t>(parent, 0x30) ^ std::uint32_t(reinterpret_cast<std::uintptr_t>(parent));
    active = true;
    parent = convar_parent(variable);
    const auto encoded = std::uint32_t(reinterpret_cast<std::uintptr_t>(parent)) ^ value;
    write(parent, 0x30, encoded);
    write(parent, 0x40, encoded);
}
void WorldConvarOverride::restore() {
    if (!variable || !active)
        return;
    auto* parent = convar_parent(variable);
    const auto encoded = std::uint32_t(reinterpret_cast<std::uintptr_t>(parent)) ^ saved;
    write(parent, 0x30, encoded);
    write(parent, 0x40, encoded);
    active = false;
}
void WorldRuntime::begin_render() {
    if (suspended)
        return;
    const auto index = invoke<std::int32_t>(interfaces.engine, 12);
    void* local = invoke<void*>(interfaces.entity_list, 3, index);
    if (!local)
        return;
    begin_render_for_player(local);
}
void WorldRuntime::begin_render_for_player(void* local) {
    if (settings.disable_post_processing.value)
        post_processing.set(0);
    if (settings.remove_fog.value) {
        const auto handle = read<std::uint32_t>(local, offsets.fog_controller);
        if (handle != UINT32_MAX)
            if (void* fog = invoke<void*>(interfaces.entity_list, 4, handle)) {
                saved_fog_enabled = read<std::uint8_t>(fog, offsets.fog_enabled);
                write(fog, offsets.fog_enabled, std::uint8_t(0));
                material_flags |= 8;
            }
    } else if (fog_overridden()) {
        const auto handle = read<std::uint32_t>(local, offsets.fog_controller);
        if (handle != UINT32_MAX)
            if (void* fog = invoke<void*>(interfaces.entity_list, 4, handle))
                write(fog, offsets.fog_enabled, saved_fog_enabled);
        material_flags &= 0xF7;
    }
    if (settings.remove_sky.value)
        for (unsigned i = 0; i != sky.size(); ++i)
            sky[i].set(i == 3);
}
void WorldRuntime::end_render() {
    post_processing.restore();
    for (auto& value : sky)
        value.restore();
}
void WorldRuntime::smoke_before_render() {
    smoke_before_render({interfaces.engine, interfaces.entity_list, interfaces.smoke_registry});
}
void WorldRuntime::smoke_before_render(const SmokeRuntimeBindings& bindings) {
    if (suspended || !settings.remove_smoke.value)
        return;
    const auto index = invoke<std::int32_t>(bindings.engine, 12);
    if (invoke<void*>(bindings.entity_list, 3, index) && bindings.smoke_registry)
        write(bindings.smoke_registry, 0xC, std::uint32_t(0));
}
void WorldRuntime::smoke_after_render() {
    smoke_after_render({interfaces.engine, interfaces.entity_list, interfaces.smoke_registry});
}
void WorldRuntime::smoke_after_render(const SmokeRuntimeBindings& bindings) {
    if (!suspended && settings.remove_smoke.value && bindings.smoke_registry)
        write(bindings.smoke_registry, 0xC, std::uint32_t(0));
}
bool filter_flash_effect(const esp::EffectsSettings& settings, void* receiver,
                         void** original_vtable, void* argument) {
    if (suppress_flash(settings, argument))
        return true;
    return reinterpret_cast<engine::MemberFunction<bool, void*>>(original_vtable[13])(receiver,
                                                                                      argument);
}
void bind_flash_effect_runtime(const esp::EffectsSettings& settings) noexcept {
    flash_settings = &settings;
}
#if defined(_M_IX86) || defined(__i386__)
bool __fastcall flash_effect_hook(void* receiver, void*, void* argument) {
#else
bool flash_effect_hook(void* receiver, void*, void* argument) {
#endif
    if (suppress_flash(*flash_settings, argument))
        return true;
    const auto table = *static_cast<void***>(receiver);
    const auto metadata = static_cast<engine::VtableHookMetadata*>(table[-2]);
    return reinterpret_cast<engine::MemberFunction<bool, void*>>(metadata->original_table[13])(
        receiver, argument);
}

} // namespace skeet::effects
