#include "skeet/skins/native_entity_cosmetics.hpp"
#include <array>
#include <bit>
#include <cstring>

namespace skeet::skins {
namespace {
constexpr std::uint32_t invalid = 0xFFFFFFFF;
constexpr float glove_wear = std::bit_cast<float>(std::uint32_t(0x3727C5AC));
template <class T> T read(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof(value));
    return value;
}
template <class T> void write(void* object, std::ptrdiff_t offset, T value) {
    std::memcpy(static_cast<std::byte*>(object) + offset, &value, sizeof(value));
}
void* subobject(void* object, std::ptrdiff_t offset) {
    return static_cast<std::byte*>(object) + offset;
}
std::uint32_t entity_index(std::uint32_t handle) {
    return handle == invalid ? 0x1FFF : handle & 0xFFFF;
}
void* complete_entity_list(void* object) {
    auto* table = read<std::byte*>(object, 0);
    auto* locator = read<void*>(table, -std::ptrdiff_t(sizeof(void*)));
    return subobject(object, -read<std::int32_t>(locator, 4));
}
}
NativeCosmeticOffsets native_cosmetic_offsets(const engine::NativeOffsetGlobals& globals) {
    auto offset = [&](std::uint32_t pointer, std::uint32_t mask,
                      std::uint32_t key) -> std::ptrdiff_t {
        return std::ptrdiff_t(*globals.pointers.at(pointer) ^ globals.keys.at(mask) ^ key);
    };
    return {offset(0x434688B0, 0x4346A518, 0x8B5586F6),
            offset(0x43468360, 0x4346891C, 0x40C98EF6),
            offset(0x43468E78, 0x434684E4, 0x9D920E35),
            offset(0x43468968, 0x43467EA0, 0xE96443C6),
            offset(0x4346A8A8, 0x43468568, 0xC6D22EE2),
            offset(0x4346908C, 0x43468BA0, 0x5C9F0FBB),
            offset(0x43468978, 0x43468F7C, 0x47877DEB),
            offset(0x4346A5F8, 0x43467B88, 0x950F7B20),
            offset(0x43468FEC, 0x4346A3C4, 0xA2A8247F) - 4,
            offset(0x43468B50, 0x43467B48, 0x38CA5228),
            offset(0x434683C8, 0x4346758C, 0xE4E2D1A7),
            offset(0x43467FCC, 0x43467938, 0x8D1BD8D0),
            offset(0x43468C9C, 0x43469280, 0x4E36DFD7),
            offset(0x43467A74, 0x434678C0, 0xCD812654),
            offset(0x43468D48, 0x43467E94, 0xC6C78C82),
            offset(0x43469230, 0x434679A8, 0xA1AF8FFC),
            offset(0x43468C1C, 0x43468F28, 0x3C5CCD57),
            offset(0x43467F9C, 0x43467188, 0xB4056E61),
            offset(0x4346A358, 0x43468B78, 0x1136BA97),
            offset(0x43468950, 0x43469200, 0x67FC28B7),
            offset(0x43469080, 0x434682B4, 0xD172D9BA),
            offset(0x4346A354, 0x43467500, 0x3353C75F),
            offset(0x43468960, 0x43467AB4, 0x71DCEE42),
            offset(0x4346A6CC, 0x43468EF8, 0x2F4E97C4),
            offset(0x43468F84, 0x43467E64, 0x3B704B41),
            offset(0x4346A5DC, 0x43468430, 0xCDE290D2),
            offset(0x43467E8C, 0x43467900, 0x32E4DAF1),
            offset(0x43469344, 0x43467018, 0x49D93E62) - 0x18,
            offset(0x43468D30, 0x43468ED4, 0x2A671B42),
            offset(0x43468368, 0x43467FAC, 0x3B1F6ECC),
            offset(0x43468BD4, 0x4346A678, 0x7501CB91),
            offset(0x43467044, 0x4346A3BC, 0xB7E9C94E),
            offset(0x43467964, 0x4346A744, 0x902A2BFD),
            offset(0x43469224, 0x4346718C, 0x83A6CF2B)};
}
NativeEntityCosmetics::NativeEntityCosmetics(const Settings& settings, NativeCosmeticOffsets fields,
                                             NativeCosmeticGlobals globals,
                                             NativeCosmeticBindings bindings)
    : settings_(settings), fields_(fields), globals_(globals), bindings_(bindings) {}
void NativeEntityCosmetics::initialize_glove_owner() noexcept {
    glove_handle_ = invalid;
    glove_factory_ = globals_.glove_class;
}
void NativeEntityCosmetics::initialize_mask_owner(NativeMaskResources& resources) noexcept {
    mask_handle_ = invalid;
    mask_factory_ = nullptr;
    mask_resources_ = &resources;
}
void NativeEntityCosmetics::resolve_mask_owner_class() {
    mask_factory_ = engine::find_client_class(bindings_.base_client, 0xF89B2C53);
}
void NativeEntityCosmetics::release_glove_owner(void* player) {
    teardown_glove(player);
}
void NativeEntityCosmetics::release_mask_owner() {
    teardown_mask();
}
void* NativeEntityCosmetics::resolve(std::uint32_t value) {
    return value == invalid ? nullptr : engine::invoke<void*>(bindings_.entity_list, 4, value);
}
std::uint32_t NativeEntityCosmetics::handle(void* entity) {
    return *engine::invoke<const std::uint32_t*>(entity, 2);
}
void* NativeEntityCosmetics::create(engine::ClientClass* factory, const char* model) {
    const auto old_highest = read<std::int32_t>(bindings_.entity_list, 0x24);
    using Create = void*(__cdecl*)(std::int32_t, std::int32_t);
    auto* created = reinterpret_cast<Create>(factory->create)(0x7FE, 0x7FF);
    if (!created)
        return nullptr;
    if (bindings_.register_entity && bindings_.unregister_entity) {
        auto* entity = engine::invoke<void*>(engine::invoke<void*>(created, 0), 7);
        bindings_.register_entity(complete_entity_list(bindings_.entity_list),
                                  entity_index(handle(entity)));
        write(bindings_.entity_list, 0x24, old_highest);
        if (engine::invoke<std::int32_t>(bindings_.model_info, 2, model) != -1 &&
            engine::invoke<bool>(entity, 0x184 / 4, model, false))
            write(entity, 0x64, entity_index(handle(entity)));
        else {
            auto value = invalid;
            bindings_.unregister_entity(complete_entity_list(bindings_.entity_list), &value,
                                        entity);
        }
    }
    engine::invoke<void>(created, 3, 0);
    return engine::invoke<void*>(engine::invoke<void*>(created, 0), 7);
}
void NativeEntityCosmetics::destroy(void* entity, bool recreating) {
    if (!entity)
        return;
    auto* networkable = engine::invoke<void*>(entity, 4);
    if (!networkable)
        return;
    if (recreating)
        engine::invoke<void>(networkable, 0x34 / 4);
    engine::invoke<void>(networkable, 3, 1);
    engine::invoke<void>(networkable, 1);
}
void* NativeEntityCosmetics::item_definition(std::uint32_t definition) {
    if (!globals_.item_system && bindings_.acquire_item_system)
        globals_.item_system = bindings_.acquire_item_system();
    if (!globals_.item_system)
        return nullptr;
    auto* schema = engine::invoke<void*>(globals_.item_system, 0);
    return schema ? engine::invoke<void*>(schema, 4, definition) : nullptr;
}
std::int32_t NativeEntityCosmetics::model_index(const char* path) {
    if (!path || !*path)
        return -1;
    auto index = engine::invoke<std::int32_t>(bindings_.model_info, 2, path);
    if (index != -1)
        return index;
    void* precache = nullptr;
    const auto count = engine::invoke<std::int32_t>(bindings_.string_tables, 5);
    for (std::int32_t i = 0; i < count; ++i) {
        auto* table = engine::invoke<void*>(bindings_.string_tables, 4, i);
        if (!table)
            continue;
        const auto* name = engine::invoke<const char*>(table, 1);
        if (name && engine::netvar_name_hash(name) == 0x9FE6B136) {
            precache = table;
            break;
        }
    }
    if (precache && engine::invoke<void*>(bindings_.model_info, 0xAC / 4, path))
        index = engine::invoke<std::int32_t>(precache, 8, 0, path, -1,
                                             static_cast<const void*>(nullptr));
    return index;
}
void NativeEntityCosmetics::set_glove(void* entity, std::int32_t paint, std::uint32_t definition,
                                      std::int32_t model) {
    write(entity, fields_.item_id_low, std::int32_t(-1));
    write(entity, fields_.item_id_high, std::int32_t(-1));
    write(entity, fields_.paintkit, paint);
    write(entity, fields_.wear, glove_wear);
    write(entity, fields_.definition, std::uint16_t(definition));
    std::array<std::byte, 0x158> info;
    write(info.data(), 8, std::uint32_t(0));
    write(info.data(), 12, std::uint32_t(0));
    const auto index = engine::invoke<std::int32_t>(bindings_.engine_client, 12);
    engine::invoke<bool>(bindings_.engine_client, 8, index, info.data());
    write(entity, fields_.account_id, read<std::uint32_t>(info.data(), 8));
    engine::invoke<void>(bindings_.model_cache, 0x84 / 4);
    engine::invoke<void>(entity, 0x12C / 4, model);
    engine::invoke<void>(bindings_.model_cache, 0x88 / 4);
}
void NativeEntityCosmetics::attribute(void* entity, std::int32_t definition, float value) {
    const auto count = read<std::int32_t>(entity, fields_.attributes + 0x10);
    auto* cursor = read<std::byte*>(entity, fields_.attributes + 4);
    for (std::int32_t i = 0; i < count; ++i, cursor += 0x18) {
        if (read<std::int32_t>(cursor, 4) != definition)
            continue;
        if (read<float>(cursor, 8) == value && read<float>(cursor, 12) == value)
            return;
        write(cursor, 8, value);
        write(cursor, 12, value);
        if (auto* cache = read<void*>(entity, fields_.attribute_cache)) {
            engine::invoke<void>(cache, 0, 1);
            write(entity, fields_.attribute_cache, static_cast<void*>(nullptr));
        }
        return;
    }
}
bool NativeEntityCosmetics::glove_matches(void* entity, std::int32_t paint,
                                          std::uint32_t definition, std::int32_t model) {
    return read<std::int32_t>(entity, fields_.paintkit) == paint &&
           std::uint32_t(read<std::uint16_t>(entity, fields_.definition)) == definition &&
           read<std::int16_t>(entity, fields_.model_index) == model;
}
void NativeEntityCosmetics::detach(void* entity, void* player) {
    engine::invoke<void>(entity, 0x454 / 4, player, false);
    engine::invoke<void>(entity, 0x478 / 4, player);
}
void* NativeEntityCosmetics::view_model(void* player, std::int32_t index) {
    auto* result =
        resolve(read<std::uint32_t>(player, fields_.view_models + std::ptrdiff_t(index) * 4));
    if (read<std::int32_t>(player, fields_.observer_mode) != 4)
        return result;
    auto* target = resolve(read<std::uint32_t>(player, fields_.observer_target));
    if (target && target != player && !read<std::uint8_t>(target, fields_.life_state) &&
        engine::invoke<std::int32_t>(target, 0x1E8 / 4) > 0 &&
        !engine::invoke<bool>(subobject(target, 8), 0x24 / 4) &&
        !read<std::uint8_t>(target, fields_.observer_excluded))
        result = view_model(target, index);
    return result;
}
void NativeEntityCosmetics::teardown_glove(void* player) {
    if (glove_handle_ == invalid)
        return;
    if (player && read<std::uint32_t>(player, fields_.wearable) != invalid &&
        read<std::uint32_t>(player, fields_.wearable) == glove_handle_) {
        if (auto* entity = resolve(read<std::uint32_t>(player, fields_.wearable))) {
            detach(entity, player);
            write(player, fields_.wearable_counter, std::uint32_t(0));
        }
        write(player, fields_.wearable, invalid);
    }
    destroy(resolve(glove_handle_), false);
    glove_handle_ = invalid;
}
void NativeEntityCosmetics::update_gloves(void* player) {
    if (!player || engine::invoke<bool>(bindings_.engine_client, 82) ||
        engine::invoke<bool>(bindings_.engine_client, 93)) {
        teardown_glove(player);
        return;
    }
    const bool ct = read<std::int32_t>(player, fields_.team) == 3;
    if (!(ct ? settings_.glove_ct.value : settings_.glove_t.value) ||
        read<std::uint8_t>(player, fields_.dormant)) {
        teardown_glove(player);
        return;
    }
    if (read<std::uint8_t>(player, fields_.life_state))
        return;
    const auto definition =
        std::uint32_t(ct ? settings_.glove_model_ct.value : settings_.glove_model_t.value);
    auto* item = item_definition(definition);
    if (!item)
        return;
    const auto* world_path = engine::invoke<const char*>(item, 7);
    const auto* view_path = engine::invoke<const char*>(item, 6);
    if (!world_path || !view_path)
        return;
    const auto model = engine::invoke<std::int32_t>(bindings_.model_info, 2, world_path);
    const auto view = engine::invoke<std::int32_t>(bindings_.model_info, 2, view_path);
    if (model == -1 || view == -1 || !glove_factory_)
        return;
    const auto paint = ct ? settings_.glove_paint_ct.value : settings_.glove_paint_t.value;
    if (resolve(read<std::uint32_t>(player, fields_.wearable)) && !resolve(glove_handle_)) {
        if (auto* worn = resolve(read<std::uint32_t>(player, fields_.wearable))) {
            if (!glove_matches(worn, paint, definition, model))
                set_glove(worn, paint, definition, model);
            attribute(worn, 6, float(paint));
            attribute(worn, 8, glove_wear);
            return;
        }
    }
    if (auto* owned = resolve(glove_handle_)) {
        if (engine::invoke<engine::ClientClass*>(subobject(owned, 8), 2) != glove_factory_)
            return;
        if (glove_matches(owned, paint, definition, model) &&
            read<std::uint32_t>(player, fields_.wearable) == glove_handle_) {
            if (read<std::int32_t>(player, fields_.transmission_state) != 1)
                engine::invoke<void>(owned, 0x454 / 4, player, true);
            return;
        }
        if (read<std::uint32_t>(player, fields_.wearable) != invalid) {
            if (auto* worn = resolve(read<std::uint32_t>(player, fields_.wearable))) {
                detach(worn, player);
                write(player, fields_.wearable_counter, std::uint32_t(0));
            }
            write(player, fields_.wearable, invalid);
        }
        destroy(resolve(glove_handle_), true);
    }
    glove_handle_ = invalid;
    auto* created = create(glove_factory_, world_path);
    if (!created)
        return;
    glove_handle_ = handle(created);
    const bool replace_local = globals_.local_override == nullptr;
    auto* saved_local = globals_.local_override;
    if (replace_local)
        globals_.local_override = player;
    write(created, fields_.entity_flags, read<std::uint32_t>(created, fields_.entity_flags) | 0x80);
    write(created, fields_.quality, std::int32_t(11));
    set_glove(created, paint, definition, model);
    const auto saved_handle = read<std::uint32_t>(player, fields_.model_update_handle);
    write(player, fields_.model_update_handle, invalid);
    const auto instance = engine::invoke<std::uint32_t>(subobject(created, 4), 0x98 / 4);
    engine::invoke<void>(created, 0x488 / 4, instance);
    write(player, fields_.model_update_handle, saved_handle);
    if (auto* worn = resolve(read<std::uint32_t>(player, fields_.wearable)))
        detach(worn, player);
    write(player, fields_.wearable, invalid);
    engine::invoke<void>(created, 0x468 / 4, player);
    engine::invoke<void>(created, 0x454 / 4, player, true);
    write(created, fields_.owner_handle, handle(player));
    engine::invoke<void>(created, 0x80 / 4);
    if (auto* weapon = resolve(read<std::uint32_t>(player, fields_.active_weapon))) {
        if (auto* model_entity =
                view_model(player, read<std::int32_t>(weapon, fields_.view_model_index))) {
            if (bindings_.invalidate_bones)
                bindings_.invalidate_bones(model_entity);
            engine::invoke<void>(model_entity, 0x3F8 / 4, 0x20);
        }
    }
    write(created, fields_.initialized, std::uint8_t(1));
    engine::invoke<void>(subobject(created, 8), 0x14 / 4, 0);
    write(player, fields_.wearable_counter, std::uint32_t(0));
    if (replace_local)
        globals_.local_override = saved_local;
}
void NativeEntityCosmetics::zero_transform(void* entity, std::ptrdiff_t offset,
                                           std::int32_t flags) {
    if (read<float>(entity, offset) != 0.0f || read<float>(entity, offset + 4) != 0.0f ||
        read<float>(entity, offset + 8) != 0.0f) {
        if (bindings_.invalidate_transform)
            bindings_.invalidate_transform(entity, flags);
        write(entity, offset, std::array<float, 3>{0.0f, 0.0f, 0.0f});
    }
}
void NativeEntityCosmetics::teardown_mask() {
    destroy(resolve(mask_handle_), false);
    mask_handle_ = invalid;
}
void NativeEntityCosmetics::update_mask(void* player) {
    if (!bindings_.set_parent)
        return;
    if (!player || engine::invoke<bool>(bindings_.engine_client, 82) ||
        engine::invoke<bool>(bindings_.engine_client, 93)) {
        teardown_mask();
        return;
    }
    const bool ct = read<std::int32_t>(player, fields_.team) == 3;
    if (!(ct ? settings_.mask_ct.value : settings_.mask_t.value)) {
        teardown_mask();
        return;
    }
    if (read<std::uint8_t>(player, fields_.dormant) ||
        !engine::invoke<void*>(subobject(player, 4), 0x20 / 4))
        return;
    auto* header = read<void*>(player, fields_.model_header);
    if (!header || !read<std::uint32_t>(header, 0) ||
        read<std::uint8_t>(player, fields_.life_state))
        return;
    const auto attachment = engine::invoke<std::int32_t>(subobject(player, 4), 0x84 / 4,
                                                         mask_resources_->attachment.begin);
    if (attachment == -1)
        return;
    const auto selected = ct ? settings_.mask_model_ct.value : settings_.mask_model_t.value;
    if (std::uint32_t(selected) >= mask_resources_->filenames.size())
        return;
    // The constructor's fixed directory/filenames fit the native256-byte path.
    std::array<char, 0x100> path;
    const auto directory_length = std::strlen(mask_resources_->directory.begin);
    const auto* filename = mask_resources_->filenames[std::size_t(selected)].begin;
    std::memcpy(path.data(), mask_resources_->directory.begin, directory_length);
    std::memcpy(path.data() + directory_length, filename, std::strlen(filename) + 1);
    const auto model = model_index(path.data());
    if (model == -1)
        return;
    auto* entity = resolve(mask_handle_);
    if (entity) {
        if (engine::invoke<engine::ClientClass*>(subobject(entity, 8), 2) != mask_factory_)
            return;
        if (read<std::int16_t>(entity, fields_.model_index) == model &&
            resolve(read<std::uint32_t>(entity, fields_.parent_handle)) == player &&
            read<std::uint32_t>(entity, fields_.model_update_handle) == handle(player) &&
            read<std::uint32_t>(entity, fields_.owner_handle) == handle(player))
            return;
    } else {
        mask_handle_ = invalid;
        entity = create(mask_factory_, path.data());
        if (!entity)
            return;
        mask_handle_ = handle(entity);
    }
    engine::invoke<void>(bindings_.model_cache, 0x84 / 4);
    engine::invoke<void>(entity, 0x12C / 4, model);
    engine::invoke<void>(bindings_.model_cache, 0x88 / 4);
    zero_transform(entity, fields_.local_origin, 1);
    zero_transform(entity, fields_.local_angles, 2);
    bindings_.set_parent(entity, player, attachment);
    zero_transform(entity, fields_.local_origin, 1);
    zero_transform(entity, fields_.local_angles, 2);
    write(entity, fields_.quality, std::int32_t(0));
    write(entity, fields_.mask_byte_43468bd4, std::uint8_t(0));
    engine::invoke<void>(entity, 0x3B8 / 4, 0);
    write(entity, fields_.mask_bits_43467044,
          std::uint16_t(read<std::uint16_t>(entity, fields_.mask_bits_43467044) | 4));
    write(entity, fields_.mask_byte_43467964, std::uint8_t(0));
    write(entity, fields_.mask_flags_43469224,
          read<std::uint32_t>(entity, fields_.mask_flags_43469224) & 0xFFFBFFFF);
    write(entity, fields_.model_update_handle, handle(player));
    write(entity, fields_.owner_handle, handle(player));
}
} //namespace skeet::skins
