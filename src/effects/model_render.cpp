#include "skeet/effects/model_render.hpp"
#include "skeet/effects/model_entity_relationships.hpp"
#include "skeet/engine/native_data_image.hpp"
#include "skeet/engine/interface_call.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <bit>
#include <x86intrin.h>
#include <xmmintrin.h>

namespace skeet::effects {
using engine::invoke;
void*& ModelRenderableTables::operator[](std::size_t index) {
    //4/6 are consumed owner captures at4348F0B5..F0FB; the other seven
    //remain live canonical RTTI/descriptor cells, including clone tables.
    static constexpr std::array<std::uint32_t, 9> cells{0x434758ACu, 0x434759A8u, 0x43468908u,
                                                        0x43467190u, 0u,          0x43475B44u,
                                                        0u,          0x4346A444u, 0x43469284u};
    if (data_ && cells.at(index))
        return data_->native_cell<void*>(cells[index]);
    return standalone_.at(index);
}
void* const& ModelRenderableTables::operator[](std::size_t index) const {
    return const_cast<ModelRenderableTables*>(this)->operator[](index);
}
namespace {
constexpr float byte_scale = 0.0039215688593685626983642578125f; // 0x4346EFA0
template <class T> T read(const void* object, std::size_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
// Host pointer size follows the bound engine. This is 0x2C bytes on x86.
struct KeyValues {
    std::uint32_t symbol_type;
    char* string_value;
    wchar_t* wide_value;
    std::uint32_t value;
    std::uint8_t type, escape;
    std::uint16_t symbol_high;
    void* system;
    bool case_sensitive;
    KeyValues *peer, *sub, *chain;
    std::uint32_t untouched;
};
void* variable(void* material, std::uint32_t& token) {
    const char empty[] = "";
    return invoke<void*>(material, 47, empty, &token);
}
void integer(void* material, std::uint32_t& token, std::int32_t value) {
    if (auto* v = variable(material, token))
        invoke<void>(v, 5, value);
}
void floating(void* material, std::uint32_t& token, float value) {
    if (auto* v = variable(material, token))
        invoke<void>(v, 4, value);
}
void reset(void* material, std::uint32_t& token) {
    if (auto* v = variable(material, token))
        invoke<void>(v, 19);
}
void vector(void* material, std::uint32_t& token, std::uint32_t color) {
    if (auto* v = variable(material, token))
        for (std::int32_t component = 0; component != 3; ++component)
            invoke<void>(v, 26, float((color >> (component * 8)) & 255) * byte_scale, component);
}
void environment(void* material, std::uint32_t& token, void* texture) {
    if (auto* v = variable(material, token)) {
        if (texture)
            invoke<void>(v, 15, texture);
        else
            invoke<void>(v, 6, "env_cubemap");
    }
}
void override_material(void* studio, void* material) {
    invoke<void>(studio, 33, material, std::int32_t(0), std::int32_t(-1));
}
void original_draw(void* studio, const ModelDrawArguments& args) {
    // The hook retains the original interface at the RTTI-adjacent -8 cell.
    const auto vtable = *static_cast<void***>(studio);
    auto* original = read<void*>(reinterpret_cast<const std::byte*>(vtable) - 8, 0);
    auto** original_vtable = read<void**>(original, 8);
    using Draw =
        engine::MemberFunction<void, void*, void*, void*, void*, void*, void*, std::uint32_t>;
    reinterpret_cast<Draw>(original_vtable[29])(studio, args.results, args.info, args.bones,
                                                args.flex_weights, args.delayed_flex_weights,
                                                args.origin, args.flags);
}
}

ModelMaterialInterfaces::ModelMaterialInterfaces(engine::NativeDataImage& data, void* key_values)
    : material_system(data.native_cell<void*>(0x4346A4E0u)), key_values_system(key_values),
      create_variable(data.native_cell<CreateMaterialVariable>(0x43467A68u)),
      clock(data.native_cell<void*>(0x43467118u)) {}
ModelShotState::ModelShotState(engine::NativeDataImage& data)
    : expires(data.native_cell<double>(0x4347BDE8u)),
      bones_begin(data.native_cell<void*>(0x434797A0u)),
      bones_end(data.native_cell<void*>(0x434797A4u)),
      origin(data.native_cell<std::array<float, 3>>(0x4347BDF0u)) {}
ModelRenderRuntime::ModelRenderRuntime(ModelMaterialInterfaces& i,
                                       const esp::ColoredModelsSettings& s)
    : interfaces(i), settings(s) {
    const esp::ModelColorGroup* groups[] = {&s.player,           &s.teammate, &s.local_player,
                                            &s.local_fake,       &s.on_shot,  &s.hands,
                                            &s.weapon_viewmodel, &s.weapons};
    for (std::size_t n = 0; n != materials.size(); ++n) {
        materials[n].kind = &groups[n]->material;
        materials[n].secondary = &groups[n]->secondary_color;
    }
    materials[4].on_shot = true;
}
void ModelRenderRuntime::initialize_resources() {
    void** textures[] = {&base_texture, &metallic_environment, &glow_environment,
                         &fullscreen_texture};
    for (std::size_t n = 0; n != 4; ++n) {
        if (*textures[n] && interfaces.material_system) {
            invoke<void>(*textures[n], 11);
            *textures[n] = nullptr;
        }
        *textures[n] = invoke<void*>(interfaces.material_system, 91, texture_names[n].c_str(), "",
                                     false, std::int32_t(0));
        if (*textures[n]) {
            texture_names[n].clear();
            invoke<void>(*textures[n], 10);
        }
    }
    void** references[] = {&solid_material, &shadow_material};
    for (std::size_t n = 0; n != 2; ++n) {
        if (*references[n] && interfaces.material_system) {
            invoke<void>(*references[n], 13);
            *references[n] = nullptr;
        }
        const auto lock = invoke<std::uint32_t>(interfaces.material_system, 118);
        auto handle = invoke<std::uint16_t>(interfaces.material_system, 86);
        void* found = nullptr;
        while (handle != invoke<std::uint16_t>(interfaces.material_system, 88)) {
            auto* material = invoke<void*>(interfaces.material_system, 89, handle);
            if (material && !invoke<bool>(material, 42)) {
                const auto* name = invoke<const char*>(material, 0);
                std::uint32_t hash = 0x811C9DC5u;
                if (name)
                    for (; *name; ++name)
                        hash = (hash ^ std::uint8_t(*name)) * 0x1000193u;
                if ((hash ^ 0x92CA5DF1u) == material_name_hashes[n]) {
                    found = material;
                    break;
                }
            }
            handle = invoke<std::uint16_t>(interfaces.material_system, 87, handle);
        }
        invoke<void>(interfaces.material_system, 119, lock);
        *references[n] = found;
        if (found) {
            material_name_hashes[n] = 0;
            invoke<void>(found, 12);
        }
    }
}
void* ModelRenderRuntime::create_material() { // 0x4336BB4B
    auto* storage = invoke<void*>(interfaces.key_values_system, 2, std::int32_t(0x2C));
    KeyValues* values = nullptr;
    if (storage) {
        values = static_cast<KeyValues*>(storage);
        values->symbol_type = 0;
        values->string_value = nullptr;
        values->wide_value = nullptr;
        values->value = 0;
        values->type = 0;
        values->escape = 0;
        values->symbol_high = 0;
        values->case_sensitive = false;
        values->peer = nullptr;
        values->sub = nullptr;
        values->chain = nullptr;
        values->system = interfaces.key_values_system;
        std::uint32_t insensitive = UINT32_MAX;
        const auto sensitive =
            invoke<std::uint32_t>(values->system, 10, &insensitive, "VertexLitGeneric", true);
        values->symbol_type = (insensitive & 0xFFFFFFu) | ((sensitive & 255u) << 24);
        values->symbol_high = std::uint16_t(sensitive >> 8);
    }
    char name[64]{};
    std::snprintf(name, sizeof name, "m%llu", static_cast<unsigned long long>(__rdtsc()));
    return invoke<void*>(interfaces.material_system, 83, static_cast<const char*>(name), values);
}
void ModelRenderRuntime::resolve_variable(void* material, const char* name, std::uint32_t& token) {
    if (invoke<void*>(material, 47, name, &token) || !interfaces.create_variable)
        return;
    if (auto* v = interfaces.create_variable(material, name)) {
        invoke<void>(v, 19);
        invoke<void>(v, 5, std::int32_t(1));
        invoke<void>(material, 84, v);
        token = 0;
        invoke<void*>(material, 47, name, &token);
    }
}
void* ModelRenderRuntime::prepare_material(ModelMaterial* cache) { // 0x433C6694
    if (!cache)
        return nullptr;
    const auto kind = cache->kind->value;
    if (kind == 1)
        return solid_material;
    if (kind == 6)
        return nullptr;
    if (!cache->material)
        cache->material = create_material();
    auto* result = cache->material;
    if (!result)
        return solid_material; // explicit native 0x433C7753 branch
    if ((kind == 0 && cache->applied_kind == 0) ||
        (kind != 0 && cache->applied_kind == kind &&
         cache->applied_color == cache->secondary->value))
        return result;

    const auto lock = invoke<std::uint32_t>(interfaces.material_system, 118);
    auto* real = invoke<void*>(result, 99);
    if (cache->material) {
        invoke<void>(invoke<void*>(cache->material, 99), 37);
        auto* variable_material = invoke<void*>(cache->material, 99);
        if (cache->material && invoke<void*>(variable_material, 69)) {
            constexpr const char* names[] = {"$allowdiffusemodulation",
                                             "$envmap",
                                             "$envmaptint",
                                             "$envmapfresnel",
                                             "$envmapcontrast",
                                             "$basemapalphaphongmask",
                                             "$phong",
                                             "$phongboost",
                                             "$phongexponent",
                                             "$phongtint",
                                             "$phongfresnelranges"};
            constexpr std::size_t order[] = {0, 1, 3, 2, 4, 5, 6, 7, 8, 9, 10};
            for (auto n : order)
                resolve_variable(variable_material, names[n], cache->variable_tokens[n]);
        }
    }
    if (invoke<void*>(real, 69)) {
        cache->applied_kind = kind;
        if (kind == 2 || kind == 3 || kind == 4 || kind == 5) {
            auto** parameters = invoke<void**>(real, 41);
            if (parameters && invoke<std::int32_t>(real, 40) > 6 && parameters[6] && base_texture)
                invoke<void>(parameters[6], 15, base_texture);
            auto& t = cache->variable_tokens;
            integer(real, t[0], 1);
            invoke<void>(real, 29, std::int32_t(0x80), kind == 5);
            invoke<void>(real, 29, std::int32_t(0x100000), true);
            const auto secondary = cache->secondary->value;
            if (kind == 2 || kind == 3) {
                integer(real, t[5], 1);
                integer(real, t[6], 1);
                if (kind == 3) {
                    reset(real, t[10]);
                    reset(real, t[9]);
                    floating(real, t[7], 0.0f);
                    environment(real, t[1], metallic_environment);
                    floating(real, t[4], 0.0f);
                    vector(real, t[2], secondary);
                    floating(real, t[3], 1.0f - float(secondary >> 24) * byte_scale);
                } else {
                    floating(real, t[7], 1.0f);
                    integer(real, t[8], std::int32_t(std::max(1u, 255u - (secondary >> 24))));
                    vector(real, t[9], secondary);
                    if (auto* v = variable(real, t[10])) {
                        invoke<void>(v, 26, 1.0f, std::int32_t(0));
                        invoke<void>(v, 26, 0.0f, std::int32_t(1));
                        invoke<void>(v, 26, 0.0f, std::int32_t(2));
                    }
                }
            } else {
                integer(real, t[6], 0);
                environment(real, t[1], kind == 4 ? glow_environment : nullptr);
                floating(real, t[4], kind == 4 ? 0.95f : 0.0f);
                floating(real, t[3], 1.0f);
                vector(real, t[2], secondary);
            }
            cache->applied_color = secondary;
            invoke<void>(real, 53);
        }
    }
    invoke<void>(interfaces.material_system, 119, lock);
    return result;
}
void ModelRenderRuntime::invalidate_material(void* material) {
    auto* real = invoke<void*>(material, 99);
    for (auto& cache : materials)
        if (cache.material && invoke<void*>(cache.material, 99) == real) {
            cache.applied_kind = -1;
            cache.applied_color = UINT32_MAX;
        }
}
void ModelRenderRuntime::modulate(void* studio, std::uint32_t color) {
    const float rgb[] = {float(color & 255) * byte_scale, float((color >> 8) & 255) * byte_scale,
                         float((color >> 16) & 255) * byte_scale, 1.0f};
    invoke<void>(studio, 27, rgb);
}
void ModelRenderRuntime::limit_alpha(void* studio, std::uint32_t color) {
    const auto alpha = float(color >> 24) * byte_scale;
    if (alpha < read<float>(studio, 0x24C))
        invoke<void>(studio, 28, alpha);
}
ModelDrawState ModelRenderRuntime::save_state(void* studio) {
    ModelDrawState result;
    std::memcpy(result.bytes.data(), static_cast<std::byte*>(studio) + 0x240, 0x38);
    return result;
}
void ModelRenderRuntime::restore_state(void* studio, const ModelDrawState& state) {
    std::memcpy(static_cast<std::byte*>(studio) + 0x240, state.bytes.data(), 0x38);
}
RenderedModel ModelRenderRuntime::classify(const void* info, const ModelClassification& b) {
    auto* renderable = read<void*>(info, 0x18);
    if (!renderable)
        return RenderedModel::other;
    auto* table = *static_cast<void**>(renderable);
    const auto& t = b.renderable_vtables;
    if (table == t[0] || table == t[1])
        return RenderedModel::player;
    if (table == t[2])
        return RenderedModel::player_attachment;
    if (table == t[3])
        return RenderedModel::ragdoll;
    if (table == t[4] || table == t[5])
        return RenderedModel::viewmodel;
    const auto name = [&]() -> const char* {
        auto* model = read<const std::byte*>(info, 0);
        if (!model)
            return nullptr;
        const auto relative = read<std::int32_t>(model, 0x190);
        if (relative) {
            const auto second = read<std::int32_t>(model + relative, 0x14);
            if (second)
                return reinterpret_cast<const char*>(model + relative + second);
        }
        return reinterpret_cast<const char*>(model + 0xC);
    };
    if (table == t[6]) {
        if (auto* unknown = invoke<void*>(renderable, 0)) {
            if (auto* entity = invoke<void*>(unknown, 7))
                if (read<std::uint32_t>(entity, b.entity_flags) & 0x810)
                    return RenderedModel::flagged_viewmodel;
        }
        if (const auto* s = name(); s && std::strlen(s) > 23 && s[0] == 'w' && s[8] == 'v' &&
                                    s[9] == '_' && s[10] == 'm' && s[17] == 'a' && s[21] == '/')
            return RenderedModel::hands;
    } else {
        if (table == t[7]) {
            if (auto* unknown = invoke<void*>(renderable, 0)) {
                if (auto* entity = invoke<void*>(unknown, 7); entity && invoke<bool>(entity, 159)) {
                    const auto handle = read<std::uint32_t>(entity, b.owner_handle);
                    if (handle == UINT32_MAX || !invoke<void*>(b.entity_list, 4, handle))
                        return RenderedModel::unowned_weapon;
                    return RenderedModel::owned_weapon;
                }
            }
        }
        if (table == t[8]) {
            if (const auto* s = name();
                s && std::strlen(s) >= 18 && s[0] == 'w' && s[8] == 'w' && s[9] == '_')
                return RenderedModel::world_weapon_model;
        }
    }
    return RenderedModel::other;
}
PlayerModelColors player_model_colors(const esp::ColoredModelsSettings& settings, bool local,
                                      bool teammate, bool history, bool ragdoll) noexcept {
    PlayerModelColors p{};
    if (local) {
        p.flags = std::uint8_t(1 | ((settings.local_player.enabled.value & 1) << 2));
        p.visible_color = settings.local_player.primary_color.value;
    } else {
        const auto& group = teammate ? settings.teammate : settings.player;
        p.flags =
            std::uint8_t(((group.enabled.value << 2) & 4) | ((group.behind_wall.value & 1) << 3));
        if (!teammate && settings.shadow.value && history)
            p.flags |= 16;
        p.visible_color = group.primary_color.value;
        p.occluded_color = group.behind_wall_color.value;
    }
    if (ragdoll)
        p.flags = std::uint8_t((p.flags & ~24u) | 1u);
    return p;
}
ModelPassSettings ModelRenderRuntime::player_pass(bool local, bool teammate, bool history,
                                                  bool ragdoll) noexcept {
    const auto colors = player_model_colors(settings, local, teammate, history, ragdoll);
    return {colors.flags, colors.visible_color, colors.occluded_color,
            &materials[local      ? 2
                       : teammate ? 1
                                  : 0]};
}
void ModelRenderRuntime::apply_single_pass(void* studio, std::size_t group) {
    const esp::ModelColorGroup* groups[] = {
        &settings.player,  &settings.teammate, &settings.local_player,     &settings.local_fake,
        &settings.on_shot, &settings.hands,    &settings.weapon_viewmodel, &settings.weapons};
    if (auto* material = prepare_material(&materials.at(group)))
        override_material(studio, material);
    modulate(studio, groups[group]->primary_color.value);
    limit_alpha(studio, groups[group]->primary_color.value);
}
bool ModelRenderRuntime::draw_viewmodel(void* studio, const ModelDrawArguments& args,
                                        RenderedModel category) {
    if (category != RenderedModel::hands && category != RenderedModel::viewmodel)
        return false;
    const auto saved = save_state(studio);
    if (category == RenderedModel::hands) {
        if (settings.hands.enabled.value)
            apply_single_pass(studio, 5);
    } else if (settings.weapon_viewmodel.enabled.value) {
        const auto* model = read<const std::byte*>(args.info, 0);
        std::uint32_t hash = 0;
        if (model) {
            const auto relative = read<std::int32_t>(model, 0x190);
            const auto second = relative ? read<std::int32_t>(model + relative, 0x14) : 0;
            const auto* name = reinterpret_cast<const char*>(
                relative && second ? model + relative + second : model + 12);
            hash = 0x811C9DC5u;
            for (; *name; ++name)
                hash = (hash ^ std::uint8_t(*name)) * 0x1000193u;
            hash ^= 0x92CA5DF1u;
        }
        auto* material = prepare_material(&materials[6]);
        if (material) {
            if (hash == 0xF8DBA852u || hash == 0x9F053037u) {
                invoke<void>(studio, 33, material, std::int32_t(3), std::int32_t(0));
                invoke<void>(studio, 33, material, std::int32_t(3), std::int32_t(1));
                invoke<void>(studio, 33, hash == 0xF8DBA852u ? material : nullptr, std::int32_t(3),
                             std::int32_t(2));
                invoke<void>(studio, 33, static_cast<void*>(nullptr), std::int32_t(3),
                             std::int32_t(3));
            } else
                override_material(studio, material);
        }
        modulate(studio, settings.weapon_viewmodel.primary_color.value);
        limit_alpha(studio, settings.weapon_viewmodel.primary_color.value);
    }
    original_draw(studio, args);
    restore_state(studio, saved);
    return true;
}
bool ModelRenderRuntime::teammate(void* entity, const ModelEntityBindings& b) {
    return model_teammate(entity, b);
}
bool ModelRenderRuntime::draw_weapon(void* studio, const ModelDrawArguments& args,
                                     RenderedModel category, const ModelEntityBindings& b) {
    if (category != RenderedModel::owned_weapon && category != RenderedModel::world_weapon_model)
        return false;
    const auto saved = save_state(studio);
    auto* renderable = read<std::byte*>(args.info, 0x18);
    auto* entity = renderable ? renderable - 4 : nullptr;
    while (entity) {
        if (invoke<bool>(entity, 158))
            break;
        const auto handle = read<std::uint32_t>(entity, b.move_parent);
        if (handle == UINT32_MAX) {
            entity = nullptr;
            break;
        }
        entity = static_cast<std::byte*>(invoke<void*>(b.entity_list, 4, handle));
    }
    if (entity && entity == b.local_player) {
        if (settings.weapon_viewmodel.enabled.value)
            apply_single_pass(studio, 6);
    } else if (settings.weapons.enabled.value) {
        if (!entity || !teammate(entity, b) || settings.teammate.enabled.value)
            apply_single_pass(studio, 7);
    }
    original_draw(studio, args);
    restore_state(studio, saved);
    return true;
}
void ModelRenderRuntime::draw_on_shot(void* studio, const ModelDrawState& saved,
                                      const ModelDrawArguments& args, const ModelShotState& shot) {
    if (!settings.on_shot.enabled.value)
        return;
    const auto duration = double(settings.on_shot_duration.value) * 0.1;
    const auto remaining = shot.expires - invoke<double>(interfaces.clock, 5);
    if (remaining <= 0.0 || remaining > duration || shot.bones_begin == shot.bones_end)
        return;
    auto* material = prepare_material(&materials[4]);
    if (material)
        invoke<void>(material, 29, std::int32_t(0x8000), true);
    const auto color = settings.on_shot.primary_color.value;
    modulate(studio, color);
    auto alpha = float(color >> 24) * byte_scale;
    if (remaining < 0.25)
        alpha = std::min(float(remaining * 4.0), alpha);
    if (alpha < read<float>(studio, 0x24C))
        invoke<void>(studio, 28, alpha);
    if (material)
        override_material(studio, material);
    auto draw_args = args;
    draw_args.bones = shot.bones_begin;
    draw_args.origin = const_cast<float*>(shot.origin.data());
    draw_args.flags |= 0x824;
    original_draw(studio, draw_args);
    restore_state(studio, saved);
}
void ModelRenderRuntime::draw_local_fake(void* studio, const ModelDrawState& saved,
                                         const ModelDrawArguments& args, const FakeModelPose& pose,
                                         const RageTrigonometry& trig) {
    if (!settings.local_fake.enabled.value)
        return;
    std::array<Matrix3x4, 256> matrices;
    const auto origin = read<RageVector>(args.origin, 0);
    if (!build_fake_model_matrices(pose, origin, matrices, trig))
        return;
    auto* material = prepare_material(&materials[3]);
    const auto color = settings.local_fake.primary_color.value;
    modulate(studio, color);
    const auto alpha = float(color >> 24) * byte_scale;
    if (alpha < read<float>(studio, 0x24C) ||
        (color >> 24) > (settings.local_player.primary_color.value >> 24))
        invoke<void>(studio, 28, alpha);
    if (material)
        override_material(studio, material);
    const auto field = read<std::uint32_t>(args.info, 0x10);
    const std::uint32_t zero = 0;
    std::memcpy(static_cast<std::byte*>(args.info) + 0x10, &zero, 4);
    auto draw_args = args;
    draw_args.bones = matrices.data();
    draw_args.flags |= 0x824;
    original_draw(studio, draw_args);
    restore_state(studio, saved);
    std::memcpy(static_cast<std::byte*>(args.info) + 0x10, &field, 4);
}
bool ModelRenderRuntime::draw_shadow(void* studio, const ModelDrawArguments& args,
                                     const ModelShadowPose& pose, ModelShadowBones& bones) {
    lagcomp::AnimationState saved;
    lagcomp::capture_animation(saved, bones.layers, bones.layer_count, bones.pose_parameters);
    lagcomp::restore_animation(pose.animation_record->animation, bones.layers, bones.layer_count,
                               bones.pose_parameters);
    std::array<Matrix3x4, animation::maximum_bones> matrices;
    const animation::BoneRequest request{pose.angles, pose.origin, pose.time, 0xFFF00, -1, 0, true};
    const auto built = animation::build_bone_matrices(bones.state, bones.model, request, matrices,
                                                      bones.engine, bones.trigonometry);
    lagcomp::restore_animation(saved, bones.layers, bones.layer_count, bones.pose_parameters);
    if (!built)
        return false;
    draw_shadow_matrices(studio, args, pose, matrices);
    return true;
}
bool ModelRenderRuntime::draw_shadow(void* studio, const ModelDrawArguments& args,
                                     const ModelShadowPose& pose, void* player,
                                     const animation::BoneRuntime& runtime) {
    const auto layer_offset = runtime.fields.animation_layers_b538bba5;
    auto* layers = read<void*>(player, layer_offset);
    const auto count = read<std::int32_t>(player, layer_offset + 0xC);
    auto poses = runtime.pose_parameters(player);
    lagcomp::AnimationState saved;
    lagcomp::capture_animation(saved, layers, count, poses.data());
    lagcomp::restore_animation(pose.animation_record->animation, layers, count, poses.data());
    std::array<Matrix3x4, animation::maximum_bones> matrices;
    const animation::BoneRequest request{pose.angles, pose.origin, pose.time, 0xFFF00, -1, 0, true};
    const auto built = runtime.build_bone_matrices(player, request, matrices);
    auto* restored_layers = read<void*>(player, layer_offset);
    const auto restored_count = read<std::int32_t>(player, layer_offset + 0xC);
    lagcomp::restore_animation(saved, restored_layers, restored_count,
                               runtime.pose_parameters(player).data());
    if (!built)
        return false;
    draw_shadow_matrices(studio, args, pose, matrices);
    return true;
}
void ModelRenderRuntime::draw_shadow_matrices(void* studio, const ModelDrawArguments& args,
                                              const ModelShadowPose& pose,
                                              std::span<Matrix3x4> matrices) {
    const auto alpha = read<float>(studio, 0x24C);
    modulate(studio, settings.shadow_color.value);
    limit_alpha(studio, settings.shadow_color.value);
    if (solid_material) {
        invoke<void>(solid_material, 29, std::int32_t(0x8000), settings.player.behind_wall.value);
        override_material(studio, solid_material);
    }
    auto draw_args = args;
    draw_args.bones = matrices.data();
    draw_args.origin = const_cast<float*>(pose.origin.data());
    draw_args.flags |= 0x824;
    original_draw(studio, draw_args);
    override_material(studio, nullptr);
    invoke<void>(studio, 28, alpha);
}
void ModelRenderRuntime::apply_player_passes(void* studio, const ModelDrawState& saved,
                                             const ModelDrawArguments& args,
                                             const ModelPassSettings& pass, std::int32_t opacity) {
    const auto original_alpha = read<float>(studio, 0x24C);
    if (!(pass.flags & 1)) {
        if (pass.flags & 8) {
            modulate(studio, pass.occluded_color);
            if (auto* material = prepare_material(pass.material)) {
                invoke<void>(material, 29, std::int32_t(0x8000), true);
                invoke<void>(material, 29, std::int32_t(0x4000), true);
                override_material(studio, material);
            }
            const auto alpha = opacity == 100 ? float(pass.occluded_color >> 24) * byte_scale
                                              : 1.0f - float(opacity) * 0.01f;
            if (alpha < original_alpha ||
                original_alpha == float(pass.visible_color >> 24) * byte_scale)
                invoke<void>(studio, 28, alpha);
            original_draw(studio, args);
            override_material(studio, nullptr);
        } else if (opacity != 100) {
            const auto alpha = 1.0f - float(opacity) * 0.01f;
            if (alpha < original_alpha)
                invoke<void>(studio, 28, alpha);
            // studiohdr/material list: info+4 -> +8, skin index info+0x1C.
            auto* header = read<void*>(args.info, 4);
            if (header) {
                auto* skin_materials = read<void*>(header, 8);
                if (skin_materials) {
                    auto* skin = static_cast<std::byte*>(skin_materials) +
                                 read<std::uint32_t>(args.info, 0x1C) * 0x20;
                    const auto count = read<std::int32_t>(skin, 8);
                    auto** list = read<void**>(skin, 12);
                    for (std::int32_t n = 0; n < count; ++n) {
                        invoke<void>(list[n], 29, std::int32_t(0x8000), true);
                        invoke<void>(list[n], 29, std::int32_t(0x4000), true);
                    }
                    original_draw(studio, args);
                    override_material(studio, nullptr);
                    for (std::int32_t n = 0; n < count; ++n) {
                        invoke<void>(list[n], 29, std::int32_t(0x8000), false);
                        invoke<void>(list[n], 29, std::int32_t(0x4000), false);
                    }
                }
            }
        }
    }
    restore_state(studio, saved);
    if (pass.flags & 4) {
        modulate(studio, pass.visible_color);
        if (auto* material = prepare_material(pass.material)) {
            invoke<void>(material, 29, std::int32_t(0x8000), false);
            invoke<void>(material, 29, std::int32_t(0x4000), false);
            override_material(studio, material);
        }
        const auto alpha = float(pass.visible_color >> 24) * byte_scale;
        if (alpha < original_alpha)
            invoke<void>(studio, 28, alpha);
    }
}
void ModelRenderRuntime::apply_gated_player(void* studio, const ModelDrawState& saved,
                                            const ModelDrawArguments& args, void* entity,
                                            const ModelPassSettings& pass,
                                            const ModelDrawFrame& frame) {
    if (!frame.player_effects)
        return;
    const auto index = invoke<std::uint32_t>(static_cast<std::byte*>(entity) + 8, 10);
    if (index - 1u < 64u && frame.player_adjustments.records[index].adjustments.disable_visuals)
        return;
    if (!(pass.flags & 2) && (pass.flags & 16) &&
        read<std::uint8_t>(entity, frame.entities.life_state) == 0) {
        if (!index)
            throw std::logic_error("Native shadow draw requires a nonzero entity index");
        ModelShadowPose pose;
        if (frame.shadows.select(index, pose, frame.rotation_math))
            draw_shadow(studio, args, pose, entity, frame.bone_runtime);
    }
    apply_player_passes(studio, saved, args, pass, frame.wall_transparency);
}
void ModelRenderRuntime::draw_model(void* studio, const ModelDrawArguments& args,
                                    const ModelDrawFrame& frame) {
    if (frame.suppress_effects || read<std::int32_t>(studio, 0x260) == 2 ||
        (args.flags & 0x204) ||
        !(settings.player.enabled.value || settings.player.behind_wall.value ||
          settings.teammate.enabled.value || settings.teammate.behind_wall.value ||
          settings.local_player.enabled.value || settings.local_fake.enabled.value ||
          settings.on_shot.enabled.value || settings.hands.enabled.value ||
          settings.weapon_viewmodel.enabled.value || settings.weapons.enabled.value ||
          settings.shadow.value)) {
        original_draw(studio, args);
        return;
    }
    const auto saved = save_state(studio);
    {
        const auto category = classify(args.info, frame.classification);
        if (draw_viewmodel(studio, args, category) ||
            draw_weapon(studio, args, category, frame.entities))
            return;
        auto* renderable = read<std::byte*>(args.info, 0x18);
        auto* entity = renderable ? renderable - 4 : nullptr;
        const auto packet = [&](void* player, bool ragdoll = false) {
            const auto local = read<std::uint8_t>(player, frame.local_player_flag) != 0;
            return player_pass(local, !local && teammate(player, frame.entities),
                               frame.history_available, ragdoll);
        };
        if (category == RenderedModel::player) {
            if (entity && invoke<bool>(entity, 158) &&
                read<std::uint8_t>(entity, frame.entities.life_state) == 0 &&
                invoke<std::int32_t>(entity, 122) > 0 && !invoke<bool>(entity + 8, 9) &&
                read<std::uint8_t>(entity, frame.immunity) == 0) {
                if (settings.local_fake.enabled.value && frame.fake_controller &&
                    read<std::uint8_t>(entity, frame.local_player_flag) != 0 && frame.fake_pose)
                    draw_local_fake(studio, saved, args, *frame.fake_pose, frame.trigonometry);
                const auto pass = packet(entity);
                if (pass.flags & 0x1c)
                    apply_gated_player(studio, saved, args, entity, pass, frame);
            }
        } else if (category == RenderedModel::ragdoll) {
            if (entity) {
                const auto handle = read<std::uint32_t>(entity, frame.ragdoll_owner);
                if (handle != UINT32_MAX) {
                    auto* owner = invoke<void*>(frame.entities.entity_list, 4, handle);
                    if (owner && settings.ragdolls.value) {
                        const auto pass = packet(owner, true);
                        if (pass.flags & 4)
                            apply_gated_player(studio, saved, args, owner, pass, frame);
                    }
                }
            }
        } else if (category == RenderedModel::player_attachment) {
            if (entity) {
                const auto handle = read<std::uint32_t>(entity, frame.attachment_owner);
                if (handle != UINT32_MAX) {
                    auto* owner = invoke<void*>(frame.entities.entity_list, 4, handle);
                    if (owner && invoke<bool>(owner, 158)) {
                        auto pass = packet(owner);
                        pass.flags = std::uint8_t((pass.flags & ~8u) | 2u);
                        if ((pass.flags & 4) && frame.player_effects) {
                            const auto index =
                                invoke<std::uint32_t>(static_cast<std::byte*>(owner) + 8, 10);
                            if (index - 1u < 64u && !frame.player_adjustments.records[index]
                                                         .adjustments.disable_visuals) {
                                modulate(studio, pass.visible_color);
                                if (auto* material = prepare_material(pass.material)) {
                                    invoke<void>(material, 29, std::int32_t(0x8000), false);
                                    invoke<void>(material, 29, std::int32_t(0x4000), false);
                                    override_material(studio, material);
                                }
                                limit_alpha(studio, pass.visible_color);
                            }
                        }
                    }
                }
            }
        } else if (settings.on_shot.enabled.value && frame.shot_entity_handle != UINT32_MAX) {
            auto* marker = invoke<void*>(frame.entities.entity_list, 4, frame.shot_entity_handle);
            if (marker && renderable == invoke<void*>(marker, 5)) {
                if (frame.shot)
                    draw_on_shot(studio, saved, args, *frame.shot);
                restore_state(studio, saved);
                return;
            }
        }
    }
    original_draw(studio, args);
    restore_state(studio, saved);
}
void ModelRenderRuntime::update_local_transparency(void* player, void* renderable,
                                                   const LocalTransparencyInterfaces& i,
                                                   const LocalTransparencyNetvars& n,
                                                   bool suppressed, bool player_effects,
                                                   float frame_time) {
    local_alpha_overridden = false;
    if (suppressed)
        return;
    if (!player)
        throw std::logic_error("Native local transparency requires a valid player");
    if (read<std::uint8_t>(player, n.field_af649dac) != 0 &&
        read<std::uint8_t>(player, n.field_cacaeecc - 12) != 0) {
        local_alpha = 1.0f;
        return;
    }
    std::uint32_t alpha = 255;
    if (settings.local_player.enabled.value && player_effects)
        alpha = settings.local_player.primary_color.value >> 24;
    const auto conditions = std::uint32_t(settings.local_transparency.value);
    bool translucent = false;
    if (conditions & 1) {
        const auto handle = read<std::uint32_t>(player, n.active_weapon);
        std::int32_t type = 11;
        if (handle != UINT32_MAX)
            if (auto* weapon = invoke<void*>(i.entity_list, 4, handle))
                type = invoke<std::int32_t>(weapon, 455);
        translucent = type == 9;
    }
    if (conditions & 2)
        translucent = translucent || read<std::uint8_t>(player, n.field_cacaeecc) != 0 ||
                      read<std::uint8_t>(player, n.field_c07d1150) != 0;
    if (translucent && alpha > 100)
        alpha = 100;
    auto target = float(alpha) / 255.0f;
    target = _mm_cvtss_f32(
        _mm_min_ss(_mm_max_ss(_mm_set_ss(target), _mm_setzero_ps()), _mm_set_ss(1.0f)));
    if (!invoke<void*>(i.input, 32, std::int32_t(-1))) {
        local_alpha = target;
        return;
    }
    const auto difference = target - local_alpha, step = frame_time * 5.0f;
    if (difference > step)
        local_alpha += step;
    else if (-step > difference)
        local_alpha -= step;
    else
        local_alpha = target;
    const auto rounded = local_alpha * 255.0f + 8388608.0f;
    const auto byte = std::uint8_t(std::bit_cast<std::uint32_t>(rounded));
    invoke<void>(player, 396, byte == 255 ? std::int32_t(0) : std::int32_t(4), true);
    invoke<void>(player, 395, std::uint32_t(byte));
    const auto handle = *invoke<const std::int16_t*>(renderable, 7);
    if (handle != -1)
        invoke<void>(i.model_cache, 3, std::uint32_t(std::uint16_t(handle)), byte != 255);
    local_alpha_overridden = byte != 255;
}
void ModelRenderRuntime::build_unoccluded_list(void* list, void* view,
                                               const ModelOcclusionInterfaces& i) {
    const auto vtable = *static_cast<void***>(list);
    auto* descriptor = read<void*>(reinterpret_cast<const std::byte*>(vtable) - 8, 0);
    auto** original = read<void**>(descriptor, 8);
    reinterpret_cast<engine::MemberFunction<void, void*>>(original[16])(list, view);
    if (!settings.disable_model_occlusion.value ||
        (!settings.player.behind_wall.value && !settings.teammate.behind_wall.value) ||
        read<std::int32_t>(view, 0x2c) != 0)
        return;
    auto* storage = read<ModelOcclusionStorage*>(view, 4);
    if (!storage)
        return;
    void* marker = nullptr;
    if (i.shot_entity_handle != UINT32_MAX)
        if (auto* entity = invoke<void*>(i.entity_list, 4, i.shot_entity_handle))
            marker = invoke<void*>(entity, 5);
    auto index = read<std::uint32_t>(list, 0x30);
    while (index != 0xffff) {
        auto* items = read<ModelOcclusionItem*>(list, 0x24);
        auto& item = items[index];
        if (item.renderable &&
            (*static_cast<void**>(item.renderable) == i.player_renderable_vtable ||
             (marker && item.renderable == marker))) {
            bool other = (item.flags & 0xc0) != 0x40;
            if (other && item.metadata && (read<std::uint16_t>(item.metadata, 10) & 0x1e0) &&
                read<std::uint8_t>(item.metadata, 14) != 255)
                other = false;
            const auto bucket = std::uint32_t(other) ^ 1;
            auto& count = storage->counts[bucket];
            auto& records = storage->buckets[bucket];
            bool duplicate = false;
            for (std::int32_t n = 0; n < count; ++n)
                if (records[std::size_t(n)].renderable == item.renderable) {
                    duplicate = true;
                    break;
                }
            if (!duplicate && count < 4096) {
                auto& record = records[std::size_t(count)];
                record.field_04 = 0;
                record.field_06 = 255;
                record.renderable = item.renderable;
                record.flags = std::uint8_t(((item.flags >> 8) & 0x3f) |
                                            ((item.flags & 0xc0) == 0x80 ? 0x40 : 0));
                record.model_type = 0;
                if (auto* model = invoke<void*>(item.renderable, 8)) {
                    const auto handle = invoke<std::uint16_t>(i.model_info, 44, model);
                    if (handle != 0xffff)
                        record.model_type =
                            invoke<std::uint8_t>(i.model_type_registry, 70, std::uint32_t(handle));
                }
                ++count;
            }
        }
        index = read<ModelOcclusionItem*>(list, 0x24)[index].next;
    }
}
} // namespace skeet::effects
