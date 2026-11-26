#include "skeet/esp/owner_runtime.hpp"
#include "skeet/engine/interface_call.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <cstring>
#include <new>

namespace skeet::esp {
EspOwnerRuntime::EspOwnerRuntime(EspOwnerBindings bindings, EspOwnerRuntime*& active)
    : bindings_(bindings), active_(&active), players_(bindings.player_engine),
      world_(bindings.world_engine), frame_(bindings.frame, players_, world_),
      sound_({bindings_.player, bindings_.sound.bindings, bindings_.frame.player_adjustments,
              bindings_.state.frame},
             players_, bindings_.state.players, bindings_.state.pickups),
      line_of_sight_(bindings_.line_of_sight, players_),
      events_(bindings.state, bindings.player, players_.runtime_engine(), bindings.event_frame),
      death_{bindings.death, bindings.state.players} {
    auto& r = bindings_.registries;
    auto& owner = bindings_.state;
    r.owners.append(this);
    hud_.emplace(r.hud, owner, frame_);
    histories_.emplace(r.histories, owner.pickups, bindings_.engine_globals,
                       bindings_.tick_interval);
    updates_.emplace(r.updates, owner.tablet, bindings_.tablet, bindings_.glow);
    packets_.emplace(r.packets, tracers_, owner.sounds, sound_, bindings_.effects,
                     bindings_.packet);
    sight_.emplace(r.frame_update, owner.sounds, events_, r.events, bindings_.world,
                   line_of_sight_);
    parser_.emplace(r.map_parser, owner, radius_prefix_);
    vtable = entries_; //43482698
    r.histories.publish(*histories_); //434826A8, after all seven registrations
    initialize_configuration();
    initialize_state();
    const auto templates = danger_zone_item_templates();
    owner.entity_models.bind_config(bindings_.world);
    for (std::size_t index = 0; index < item_labels_.size(); ++index) {
        item_labels_[index] = templates[index].label;
        auto& descriptor = owner.entity_models.descriptors[index];
        descriptor.label = item_labels_[index];
        descriptor.distance = 0;
        descriptor.closest_origin = {};
    }
    labels_ = {u"FRAG", u"FLASH", u"SMOKE", u"DECOY", u"MOLLY", u"NADE", u"BOMB"};
    owner.grenade_labels = {labels_[0], labels_[1], labels_[2], labels_[3], labels_[4], labels_[5]};
    owner.dropped_bomb_label = labels_[6];
    radius_prefix_ = "\"bombradius\" \"";
    events_.bind_before_death(r.events);
    death_.bind(r.events);
    events_.bind_after_death(r.events);
    initialize_classes();
}
void EspOwnerRuntime::initialize_configuration() {
    auto& registries = bindings_.registries.configuration;
    auto& r = registries.descriptors;
    auto& p = bindings_.player;
    auto& w = bindings_.world;
    auto boolean = [&](BooleanConfig& value, std::uint32_t hash) {
        initialize_boolean_config(r, value, hash);
    };
    auto color = [&](ColorConfig& value, std::uint32_t hash, std::uint32_t initial) {
        initialize_color_config(r, value, hash, initial);
    };
    auto integer = [&](IntegerConfig& value, std::uint32_t hash, std::int32_t initial) {
        initialize_integer_config(r, value, hash, initial);
    };
    boolean(bindings_.effects.bullet_tracers, 0x3A5285A6u);
    color(bindings_.effects.tracer_color, 0x635F47DEu, 0xFFFF8296u);
    bindings_.state.sounds.ring_model = -1;
    tracers_.beams = bindings_.beams; //434826E5..434826EA, before activation registration
    tracers_.first = 0;
    tracers_.count = 0;
    initialize_hotkey_config(registries, p.activation, 0x4700A298u, 0x10, HotkeyMode::always);
    boolean(p.bounding_box, 0xCFF9EBADu);
    color(p.box_color, 0x4D243E0Du, 0x82FFFFFFu);
    boolean(p.health_bar, 0x49733D3Cu);
    color(p.health_color, 0xD2E84B64u, 0xFF50E178u);
    boolean(p.name, 0xA1C152A1u);
    color(p.name_color, 0x25FEB6C1u, 0xC8FFFFFFu);
    boolean(p.weapon_text, 0xBB3FDF6Au);
    boolean(p.weapon_icon, 0x6A874B2Cu);
    color(p.weapon_icon_color, 0x00F64778u, 0xC8FFFFFFu);
    boolean(p.distance, 0x0F6D81D5u);
    boolean(p.glow, 0xB1D008B7u);
    color(p.glow_color, 0xECE872CBu, 0x99783CB4u);
    integer(field_8ce3b4b2_, 0x8CE3B4B2u, 0);
    boolean(p.visualize_sounds, 0xE8D2A207u);
    color(p.sound_color, 0x7E3B4C1Bu, 0xFF3F3FFAu);
    boolean(p.flags, 0x084A8B33u);
    boolean(p.ammo, 0x43F02F68u);
    color(p.ammo_color, 0x8BEC37F4u, 0xE6C88C50u);
    boolean(p.line_of_sight, 0x1DE914E6u);
    color(p.sight_color, 0x745EFA1Eu, 0xFF0000FFu);
    boolean(p.skeleton, 0x523E4B35u);
    color(p.skeleton_color, 0x0E995F95u, 0xFFAAFFFFu);
    boolean(p.teammates, 0x45D9D2ACu);
    boolean(w.radar, 0xD7CD1B0Bu);
    boolean(w.spectators, 0x38623779u);
    integer(w.dropped_weapons, 0x6D3D6833u, 0);
    color(w.dropped_weapon_color, 0xA10DC407u, 0xC8FFFFFFu);
    boolean(p.out_of_fov, 0x95A660ABu);
    integer(p.arrow_size, 0x4564BFC7u, 12);
    integer(p.arrow_distance, 0x09CBEDAEu, 100);
    color(p.arrow_color, 0x7D18A71Fu, 0xFFFFFFFFu);
    boolean(w.hostages.enabled, 0x559F78DCu);
    color(w.hostages.color, 0xA6B878A8u, 0xC8FC9F38u);
    boolean(w.bomb.enabled, 0xF2937E07u);
    color(w.bomb.color, 0x2ADA481Bu, 0xFF3CC896u);
    boolean(p.dormant, 0xC410C5D3u);
    boolean(w.upgrade_tablet, 0x553D3C3Fu);
    boolean(w.danger_zone_items, 0x89653491u);
    struct Item {
        ColoredWorldItem WorldEspConfig::* member;
        std::uint32_t enabled, color, value;
    };
    constexpr Item items[]{
        {&WorldEspConfig::drone_gun, 0x75A591C2u, 0xDB9A269Au, 0xFF3CC896u},
        {&WorldEspConfig::blackhawk, 0x325AF11Eu, 0x174ABFD6u, 0xFF00E0FFu},
        {&WorldEspConfig::drone, 0x6E6CDAD7u, 0xF407872Bu, 0xFF3CC896u},
        {&WorldEspConfig::random_case, 0x5DA3152Cu, 0x082CE178u, 0xFFF0138Fu},
        {&WorldEspConfig::tool_case, 0xF2A389D2u, 0x928C112Au, 0xFFFFDC00u},
        {&WorldEspConfig::pistol_case, 0x3B6A149Eu, 0xCEE5C756u, 0xFF505050u},
        {&WorldEspConfig::explosive_case, 0xA3B423BEu, 0x55436936u, 0xFF0082FFu},
        {&WorldEspConfig::heavy_weapon_case, 0x52DAAA5Du, 0x72894F7Du, 0xFF0000FFu},
        {&WorldEspConfig::light_weapon_case, 0x081D65D4u, 0xCF605580u, 0xFF3CC896u},
        {&WorldEspConfig::dufflebag, 0xD13FFDF6u, 0x8B5C32AEu, 0xFF3B7FC8u},
        {&WorldEspConfig::jammer, 0x6A586832u, 0x76DA764Au, 0xFF00006Eu},
        {&WorldEspConfig::ammobox, 0xF8774353u, 0x406A4767u, 0xFF3B7FC8u},
        {&WorldEspConfig::armor, 0xFFD5C89Du, 0xC11B6FBDu, 0xFFC8843Bu},
        {&WorldEspConfig::parachute_pack, 0x2802A1F6u, 0xB424B6AEu, 0xFFFFDC00u},
        {&WorldEspConfig::briefcase, 0x2B4CDE08u, 0xC8555DD4u, 0xFF3B7FC8u},
        {&WorldEspConfig::tablet_upgrade_zone, 0x4FD3F678u, 0x58386184u, 0xFFFFF0AAu},
        {&WorldEspConfig::tablet_upgrade_drone, 0x42694ACEu, 0x06D6A3C6u, 0xFFFFF0AAu},
        {&WorldEspConfig::cash_stack, 0x2D915386u, 0xE5F28DFEu, 0xFF00FF00u}};
    for (const auto& item : items) {
        if (item.member == &WorldEspConfig::parachute_pack) {
            boolean(field_5aadd6ed_.enabled, 0x5AADD6EDu);
            color(field_5aadd6ed_.color, 0xEC79024Du, 0xFF3CC896u);
            boolean(field_b1054809_.enabled, 0xB1054809u);
            color(field_b1054809_.color, 0xA7B7F909u, 0xFF3CC896u);
        }
        auto& setting = w.*item.member;
        boolean(setting.enabled, item.enabled);
        color(setting.color, item.color, item.value);
    }
}
void EspOwnerRuntime::initialize_state() {
    auto& owner = bindings_.state;
    owner.sounds.listener_enabled = {};
    tracers_.recent = {};
    owner.defuse_kit_model = -1;
    owner.frame.transient = 0;
    owner.frame.timer = 0;
    owner.bomb_peak_damage = 500.0f;
    owner.state_6b4 = 0;
    owner.grenade_classes = {};
    owner.notice_entity = 0; //calloc value; reset later changes this to-1
    bindings_.glow.weapons_active = false;
    bindings_.glow.bomb_handle = -1;
    owner.tablet = TabletUpgradeState{};
    owner.tablet.frame = 0; //constructor calloc, distinct from reset
    owner.pickups = PickupObservations{};
}
void EspOwnerRuntime::initialize_classes() {
    auto& owner = bindings_.state;
    auto* current = engine::invoke<EspClientClass*>(bindings_.client_classes, 10);
    for (std::uint32_t index = 0; current; current = current->next, ++index) {
        const auto hash =
            entity_class_hash(current->name ? std::string_view(current->name) : std::string_view{});
        const auto handler = entity_overlay_handler(hash);
        if (handler == EntityOverlayHandler::none)
            continue;
        if (class_handlers_.size() <= index)
            class_handlers_.resize(std::size_t(index) + 1);
        class_handlers_[index] = handler;
        switch (hash) {
        case 0xC09C6702u:
            owner.grenade_classes.base_projectile = std::int32_t(index);
            break;
        case 0x69629121u:
            owner.grenade_classes.decoy_projectile = std::int32_t(index);
            break;
        case 0xA239EA8Fu:
            owner.grenade_classes.molotov_projectile = std::int32_t(index);
            break;
        case 0xE31DC1B8u:
            owner.grenade_classes.smoke_projectile = std::int32_t(index);
            break;
        case 0xABE104C9u:
            owner.grenade_classes.inferno = std::int32_t(index);
            break;
        default:
            break;
        }
    }
    owner.class_handlers = class_handlers_;
}
EspOwnerRuntime::~EspOwnerRuntime() { //4338580A exact owned-field teardown order
    std::string{}.swap(radius_prefix_);
    for (auto index = labels_.size(); index; --index)
        std::u16string{}.swap(labels_[index - 1]);
    const auto listeners = events_.dormant_listeners();
    auto& owner = bindings_.state;
    for (auto index = listeners.size(); index; --index) {
        auto& enabled = owner.sounds.listener_enabled[index - 1];
        if (enabled) {
            enabled = 0;
            bindings_.registries.events.remove(listeners[index - 1]);
        }
    }
    for (auto index = item_labels_.size(); index; --index)
        std::u16string{}.swap(item_labels_[index - 1]);
    owner.entity_models.models.release_storage();
    std::vector<EntityOverlayHandler>{}.swap(class_handlers_);
}
void EspOwnerRuntime::bind_configuration() { //43485C12
    auto* value = find_config(bindings_.registries.configuration.descriptors, 0xFC225A4Fu);
    override_fov_ = &static_cast<IntegerConfig*>(value)->value;
}
void EspOwnerRuntime::reset() {
    reset_esp_frame_owner(bindings_.state, bindings_.glow);
}
void EspOwnerRuntime::initialize_models() {
    initialize_esp_entity_models(bindings_.state, bindings_.glow, bindings_.world,
                                 bindings_.network_string_tables);
}
#if defined(_M_IX86) || defined(__i386__)
#define SKEET_ESP_PRIMARY __thiscall
#else
#define SKEET_ESP_PRIMARY
#endif
void SKEET_ESP_PRIMARY EspOwnerRuntime::destroy(void* object) { //43331A5C
    auto* self = static_cast<EspOwnerRuntime*>(object);
    destroy_esp_owner(*self->active_, self);
}
void SKEET_ESP_PRIMARY EspOwnerRuntime::configuration(void* object) {
    static_cast<EspOwnerRuntime*>(object)->bind_configuration();
}
void SKEET_ESP_PRIMARY EspOwnerRuntime::reset_entry(void* object) {
    static_cast<EspOwnerRuntime*>(object)->reset();
}
void SKEET_ESP_PRIMARY EspOwnerRuntime::models(void* object) {
    static_cast<EspOwnerRuntime*>(object)->initialize_models();
}
void SKEET_ESP_PRIMARY EspOwnerRuntime::unchanged(void*) {} //433CCE8E
#undef SKEET_ESP_PRIMARY
void initialize_esp_owner(EspOwnerRuntime*& active, EspOwnerBindings bindings) { //43481EAB
    constexpr auto bytes = sizeof(EspOwnerRuntime) > 0xF90 ? sizeof(EspOwnerRuntime) : 0xF90;
    auto* storage = engine::allocate_owner_storage(bindings.registries.owners.memory, bytes);
    if (!storage) {
        active = nullptr;
        return;
    }
    active = ::new (storage) EspOwnerRuntime(bindings, active);
}
void destroy_esp_owner(EspOwnerRuntime*& active, EspOwnerRuntime* object) { //43331A5C
    active = nullptr;
    if (!object)
        return;
    const auto memory = object->bindings_.registries.owners.memory;
    object->~EspOwnerRuntime();
    engine::release_owner_storage(memory, object);
}
} //namespace skeet::esp
