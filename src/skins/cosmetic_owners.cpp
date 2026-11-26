#include "skeet/skins/cosmetic_owners.hpp"
#include "skeet/skins/masks.hpp"
#include <array>
#include <cstring>
#include <new>

namespace skeet::skins {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define COSMETIC_MEMBER __thiscall
#else
#define COSMETIC_MEMBER
#endif
using OwnerEntry = void(COSMETIC_MEMBER*)(void*);
void COSMETIC_MEMBER unchanged(void*) {} //433CCE8E
void COSMETIC_MEMBER destroy_gloves(void*);
void COSMETIC_MEMBER disable_gloves(void*);
void COSMETIC_MEMBER release_gloves(void*);
void COSMETIC_MEMBER destroy_mask(void*);
void COSMETIC_MEMBER release_mask(void*);
const std::array<OwnerEntry, 9> glove_table{//434707AC
                                            destroy_gloves, unchanged,      unchanged,
                                            unchanged,      disable_gloves, unchanged,
                                            unchanged,      unchanged,      release_gloves};
const std::array<OwnerEntry, 9> mask_table{//43472574
                                           destroy_mask, unchanged,    unchanged,
                                           unchanged,    release_mask, unchanged,
                                           release_mask, unchanged,    release_mask};
void register_integer(ConfigLink& registry, IntegerConfig& value, std::uint32_t hash) {
    value.hash = hash;
    value.serialized_size = 4;
    value.type = ConfigType::integer;
    append_config_link(registry, value);
}
void* allocate_owner(engine::JobMemory memory, std::size_t size) { //43387448
    auto* raw = memory.allocate(memory.process_heap, 8, size + 0x20);
    if (!raw)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    const auto header = (address + 15) & ~std::uintptr_t(15);
    const auto displacement = std::uint32_t(header - address);
    const auto length = std::uint32_t(size + 0x20);
    std::memcpy(reinterpret_cast<void*>(header + 8), &displacement, 4);
    std::memcpy(reinterpret_cast<void*>(header + 12), &length, 4);
    return reinterpret_cast<void*>(header + 16);
}
void free_owner(engine::JobMemory memory, void* object) { //433837D0
    auto* bytes = static_cast<std::byte*>(object);
    std::uint32_t displacement;
    std::memcpy(&displacement, bytes - 8, 4);
    memory.free(memory.process_heap, 0, bytes - 16 - displacement);
}
}
class GloveOwner {
public:
    const OwnerEntry* vtable = nullptr;
    CosmeticOwnerBindings bindings;
    engine::JobMemory memory;
    client::NativeEntityObserverReceiver entity_receiver{};
    GloveOwner(const CosmeticOwnerRegistries& registries, ConfigRegistries& config,
               CosmeticOwnerBindings input)
        : bindings(input), memory(registries.lifecycle.memory) {
        registries.lifecycle.append(this); //433676A0
        registries.entities.append_gloves(entity_receiver, bindings.cosmetics); //4349074A /4333BF32
        vtable = glove_table.data(); //43490752
        registries.entities.publish_gloves(entity_receiver); //4349075A
        auto& settings = bindings.settings;
        initialize_boolean_config(config.descriptors, settings.glove_t, 0x3A8EF907);
        initialize_boolean_config(config.descriptors, settings.glove_ct, 0xE5249CA0);
        initialize_integer_config(config.descriptors, settings.glove_model_t, 0x7439E8B5, 0x13A3);
        initialize_integer_config(config.descriptors, settings.glove_model_ct, 0x5101537E, 0x13A3);
        register_integer(config.descriptors, settings.glove_paint_t, 0xE13F61E0);
        register_integer(config.descriptors, settings.glove_paint_ct, 0x7CFD0011);
        bindings.cosmetics.initialize_glove_owner(); //handle=-1, captured4346841C
    }
};
class MaskOwner {
public:
    const OwnerEntry* vtable = nullptr;
    CosmeticOwnerBindings bindings;
    engine::JobMemory memory;
    client::NativeEntityObserverReceiver entity_receiver{};
    NativeMaskResources resources;
    MaskOwner(const CosmeticOwnerRegistries& registries, ConfigRegistries& config,
              CosmeticOwnerBindings input)
        : bindings(input), memory(registries.lifecycle.memory) {
        registries.lifecycle.append(this); //433676A0
        registries.entities.append_mask(entity_receiver, bindings.cosmetics); //4347EBBE /4333BF32
        vtable = mask_table.data(); //4347EBC6
        registries.entities.publish_mask(entity_receiver); //4347EBCE
        auto& settings = bindings.settings;
        initialize_boolean_config(config.descriptors, settings.mask_t, 0xDE88B6DC);
        register_integer(config.descriptors, settings.mask_model_t, 0x588FC522);
        initialize_boolean_config(config.descriptors, settings.mask_ct, 0xF6D4D395);
        register_integer(config.descriptors, settings.mask_model_ct, 0x82097AD3);
        bindings.cosmetics.initialize_mask_owner(resources); //handle=-1, class=0
        misc::construct_native_text(resources.attachment, "facemask", memory);
        constexpr char directory[] = "models/player/holiday/facemasks/";
        misc::construct_native_text(resources.directory, directory, memory);
        for (auto& text : resources.filenames)
            misc::construct_native_text(text, "", memory);
        //Native4347ED32 resolves its class after the24 text objects exist and
        //before assigning the22 separator-delimited filename ranges.
        bindings.cosmetics.resolve_mask_owner_class();
        const auto paths = mask_model_paths();
        for (std::size_t index = 0; index < paths.size(); ++index) {
            const auto filename = paths[index].substr(sizeof(directory) - 1);
            //All22 native names exceed the empty-vector growth minimum8;
            //43320011/434015F3/433F4815 therefore allocate exactly length+1.
            misc::construct_native_text_range(resources.filenames[index], filename.data(),
                                              filename.data() + filename.size(), memory);
        }
    }
};
namespace {
void COSMETIC_MEMBER release_gloves(void* object) { //4331EB65
    auto& owner = *static_cast<GloveOwner*>(object);
    if (owner.bindings.cosmetics.glove_handle() == 0xFFFFFFFF)
        return;
    auto* player = engine::invoke<bool>(owner.bindings.engine_client, 26)
                       ? owner.bindings.local_player
                       : nullptr;
    owner.bindings.cosmetics.release_glove_owner(player);
}
void COSMETIC_MEMBER disable_gloves(void* object) { //4332CAC3
    auto& owner = *static_cast<GloveOwner*>(object);
    owner.bindings.settings.glove_ct.value = 0;
    owner.bindings.settings.glove_t.value = 0;
    release_gloves(object);
}
void COSMETIC_MEMBER destroy_gloves(void* object) { //433B1A7A ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<GloveOwner*>(object);
    const auto memory = owner->memory;
    //The native destructor only frees storage. Entity teardown is its real
    //primary slot4/8 operation, rather than an added destructor side effect.
    owner->~GloveOwner();
    free_owner(memory, object);
}
void COSMETIC_MEMBER release_mask(void* object) { //43386F30, all three actual slots
    static_cast<MaskOwner*>(object)->bindings.cosmetics.release_mask_owner();
}
void COSMETIC_MEMBER destroy_mask(void* object) { //4336E6A4 ->4335BD60 ->433837D0
    if (!object)
        return;
    auto* owner = static_cast<MaskOwner*>(object);
    const auto memory = owner->memory;
    for (std::size_t index = owner->resources.filenames.size(); index != 0; --index)
        misc::destroy_native_text(owner->resources.filenames[index - 1], memory);
    misc::destroy_native_text(owner->resources.directory, memory);
    misc::destroy_native_text(owner->resources.attachment, memory);
    owner->~MaskOwner();
    free_owner(memory, object);
}
#undef COSMETIC_MEMBER
}
std::size_t glove_owner_size() noexcept {
    return sizeof(GloveOwner);
}
std::size_t mask_owner_size() noexcept {
    return sizeof(MaskOwner);
}
GloveOwner& construct_glove_owner(void* storage, const CosmeticOwnerRegistries& registries,
                                  ConfigRegistries& config, CosmeticOwnerBindings bindings) {
    return *::new (storage) GloveOwner(registries, config, bindings);
}
MaskOwner& construct_mask_owner(void* storage, const CosmeticOwnerRegistries& registries,
                                ConfigRegistries& config, CosmeticOwnerBindings bindings) {
    return *::new (storage) MaskOwner(registries, config, bindings);
}
void* allocate_glove_owner_storage(engine::JobMemory memory) {
    return allocate_owner(memory, sizeof(GloveOwner));
}
void* allocate_mask_owner_storage(engine::JobMemory memory) {
    return allocate_owner(memory, sizeof(MaskOwner));
}
NativeEntityCosmetics& glove_owner_cosmetics(GloveOwner& owner) noexcept {
    return owner.bindings.cosmetics;
}
NativeEntityCosmetics& mask_owner_cosmetics(MaskOwner& owner) noexcept {
    return owner.bindings.cosmetics;
}
} //namespace skeet::skins
