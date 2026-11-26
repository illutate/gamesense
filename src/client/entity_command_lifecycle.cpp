#include "skeet/client/entity_command_lifecycle.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/animation/entity_update.hpp"
#include "skeet/esp/map_entity_parser.hpp"
#include "skeet/misc/native_text.hpp"
#include "skeet/engine/interface_call.hpp"
#include "entity_command_map.hpp"
#include "entity_command_memory.hpp"
#include "entity_command_scratch.hpp"
#include <array>
#include <cstring>
#include <emmintrin.h>
#include <new>
#include <stdexcept>

namespace skeet::client {
namespace {
#if defined(_M_IX86) || defined(__i386__)
#define ENTITY_MEMBER __thiscall
#define ENTITY_FAST __fastcall
#define ENTITY_SCALAR __vectorcall
#else
#define ENTITY_MEMBER
#define ENTITY_FAST
#define ENTITY_SCALAR
#endif
using OwnerEntry = void(ENTITY_MEMBER*)(void*);
struct EntityCommandInstance;
struct MapEndpoint : esp::MapEntityParserEndpoint {
    EntityCommandInstance* owner;
};
void ENTITY_MEMBER destruction_entry(void*);
void ENTITY_MEMBER release_entry(void*);
void ENTITY_MEMBER reset_entry(void*);
void ENTITY_MEMBER refresh_entry(void*);
void ENTITY_MEMBER unchanged(void*) {} //actual433CCE8E
void ENTITY_SCALAR map_scalar(void*, float) {} //actual433CCE8E
void ENTITY_FAST map_text(void*, const char*);
const std::array<OwnerEntry, 9> primary_table{destruction_entry, unchanged,     unchanged,
                                              unchanged,         release_entry, reset_entry,
                                              unchanged,         refresh_entry, unchanged};
const esp::MapEntityParserTable map_table{map_scalar, map_text};

struct EntityCommandInstance {
    const OwnerEntry* vtable = nullptr;
    NativeCommandPhaseReceiver command_receiver;
    NativeEntityObserverReceiver entity_receiver;
    MapEndpoint map{{nullptr}, this};
    EntityCommandFactoryBindings bindings;
    std::shared_ptr<engine::NativeDataImage> image;
    entity_command_detail::ModelCategories categories;
    std::array<misc::NativeText, 8> names;

    explicit EntityCommandInstance(const EntityCommandFactoryBindings& source)
        : bindings(source), image(source.data),
          categories(source.registries.lifecycle.memory, *image, source.ceiling) {
        auto& registries = bindings.registries;
        auto& command = bindings.command;
        registries.lifecycle.append(this); //4347F93C
        registries.commands.append(command_receiver, &command); //4347F944
        registries.prediction.append(command); //4347F94C
        registries.data_updates.append(command); //4347F954: register, not publish
        registries.entities.append_entity_commands(entity_receiver, command); //4347F95C
        esp::register_map_entity_parser(registries.map_entities, &map); //4347F964
        registries.recharge.append(command); //4347F96C

        registries.entities.publish_entity_commands(entity_receiver); //4347F973, table4346FBF0
        map.vtable = &map_table; //4347F97C, table4347294C
        registries.recharge.publish(command); //4347F983, table43470268
        vtable = primary_table.data(); //4347F991, table434726E0
        registries.commands.publish(command_receiver, CommandOwnerId::owner_43470200); //4347F997
        registries.prediction.publish(command); //4347F99E, table43470924
        registries.data_updates.publish(command); //4347F9A5, table4346F814
        auto& context = bindings.command_bindings;
        context.engine_client = bindings.engine_client; //4347F9AC
        context.shot_recording_enabled = &bindings.shot_recording_enabled; //4347F9B4
        context.prediction = bindings.prediction_interface; //4347F9BB
        auto& state = command.state;
        state.revolver_delay = 0.23125f;
        auto* revolver = static_cast<RevolverCommandPrediction*>(
            entity_command_detail::allocate(registries.lifecycle.memory, 0x708));
        if (revolver)
            initialize_entity_revolver_storage(revolver);
        state.revolver = revolver;
        auto* history = static_cast<prediction::CommandHistoryEntry*>(
            entity_command_detail::allocate(registries.lifecycle.memory, 0x5460));
        if (history)
            initialize_entity_prediction_storage(history);
        state.history = history;
        categories.initialize();
        state.model_categories = &categories.view();

        constexpr std::array<const char*, 8> literals{
            "m_flVelocityModifier", "m_flThirdpersonRecoil", "m_bWaitForNoAttack",
            "m_bStrafing",          "m_fThrowTime",          "m_bIsHeldByPlayer",
            "m_iMoveState",         "m_flLowerBodyYawTarget"};
        for (std::size_t index = 0; index < names.size(); ++index)
            misc::construct_native_text(names[index], literals[index], registries.lifecycle.memory);
        command.prediction_names = names;
    }
    EntityCommandOwner*& active() const {
        return image->native_cell<EntityCommandOwner*>(0x43475898u);
    }
    void release_owned() {
        bindings.command.prediction_names = {};
        for (std::size_t index = names.size(); index; --index)
            misc::destroy_native_text(names[index - 1], bindings.registries.lifecycle.memory);
        categories.release();
        auto& state = bindings.command.state;
        if (state.history) {
            entity_command_detail::free(bindings.registries.lifecycle.memory, state.history);
            state.history = nullptr;
        }
        if (state.revolver) {
            entity_command_detail::free(bindings.registries.lifecycle.memory, state.revolver);
            state.revolver = nullptr;
        }
    }
};
void ENTITY_MEMBER destruction_entry(void* object) {
    auto& owner = *static_cast<EntityCommandInstance*>(object);
    destroy_entity_command_owner(object, *owner.image);
}
void ENTITY_MEMBER release_entry(void* object) {
    auto& owner = *static_cast<EntityCommandInstance*>(object);
    entity_command_detail::release_prediction_scratch(owner.active(), *owner.image);
}
void ENTITY_MEMBER reset_entry(void* object) {
    reset_entity_command_owner(object);
}
void ENTITY_MEMBER refresh_entry(void* object) {
    refresh_entity_command_owner(object);
}
void ENTITY_FAST map_text(void* endpoint, const char* text) {
    static_cast<MapEndpoint*>(static_cast<esp::MapEntityParserEndpoint*>(endpoint))
        ->owner->categories.parse(text);
}
void factory_entry(void* source) {
    create_entity_command_owner(*static_cast<EntityCommandFactoryBindings*>(source));
}
void require_binding_identity(const EntityCommandFactoryBindings& bindings) {
    if (&bindings.command.bindings != &bindings.command_bindings)
        throw std::invalid_argument(
            "Entity command lifecycle requires its canonical command bindings");
    const auto& image = engine::require_native_data_image(bindings.data);
    if (&bindings.registries.prediction.image() != &image ||
        &bindings.registries.data_updates.image() != &image ||
        &bindings.registries.recharge.image() != &image)
        throw std::invalid_argument("Entity command registries require the same retained image");
}
#undef ENTITY_MEMBER
#undef ENTITY_FAST
#undef ENTITY_SCALAR
}

std::size_t entity_command_owner_size() noexcept {
    return sizeof(EntityCommandInstance);
}
void construct_entity_command_owner(void* storage, const EntityCommandFactoryBindings& bindings) {
    require_binding_identity(bindings);
    ::new (storage) EntityCommandInstance(bindings);
}
void* create_entity_command_owner(const EntityCommandFactoryBindings& bindings) {
    require_binding_identity(bindings);
    auto* storage = entity_command_detail::allocate(bindings.registries.lifecycle.memory,
                                                    entity_command_owner_size());
    EntityCommandOwner* owner = nullptr;
    if (storage) {
        construct_entity_command_owner(storage, bindings);
        owner = &bindings.command;
    }
    engine::require_native_data_image(bindings.data).native_cell<EntityCommandOwner*>(0x43475898u) =
        owner;
    return storage;
}
void destroy_entity_command_owner(void* storage, const engine::NativeDataImage& image) {
    image.native_cell<EntityCommandOwner*>(0x43475898u) = nullptr;
    if (!storage)
        return;
    auto* owner = static_cast<EntityCommandInstance*>(storage);
    const auto memory = owner->bindings.registries.lifecycle.memory;
    owner->release_owned(); //43401B38
    owner->~EntityCommandInstance();
    entity_command_detail::free(memory, storage);
}
void reset_entity_command_owner(void* storage) {
    auto& owner = *static_cast<EntityCommandInstance*>(storage);
    auto& command = owner.bindings.command;
    command.state.prediction_stamp = 0;
    command.bindings.angle_history.set_active(false);
    command.bindings.angle_confirmation->count = 0;
    command.bindings.angle_confirmation->index = 0;
    reset_entity_prediction_history(command);
}
void refresh_entity_command_owner(void* storage) {
    auto& owner = *static_cast<EntityCommandInstance*>(storage);
    owner.vtable[5](storage);
    if (!engine::invoke<bool>(owner.bindings.command.bindings.engine_client, 27))
        return;
    const auto* interval = owner.image->native_cell<const float*>(0x4346A338u);
    const float ticks = 1.0f / *interval + 0.5f;
    if (_mm_cvttss_si32(_mm_set_ss(ticks)) != 64)
        return;
    auto* convar = reinterpret_cast<const std::byte*>(
        std::uintptr_t(owner.image->native_cell<std::uint32_t>(0x43468EA4u) ^ 0xFCD2DA7Au));
    for (;;) {
        const std::byte* parent;
        std::memcpy(&parent, convar + 0x1C, sizeof parent);
        if (parent == convar)
            break;
        convar = parent;
    }
    std::uint32_t marker;
    std::memcpy(&marker, convar + 0x30, sizeof marker);
    if (marker == std::uint32_t(reinterpret_cast<std::uintptr_t>(convar)))
        owner.bindings.command.bindings.angle_history.set_active(true);
}
void bind_native_entity_command_factory(NativeStartupSources& sources,
                                        EntityCommandFactoryBindings& bindings) {
    sources.bind_initializer({0x43486705u, &bindings, factory_entry});
}
} // namespace skeet::client
