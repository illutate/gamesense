#pragma once
#include <cstdint>

namespace skeet {
class ConfigRegistries;
}
namespace skeet::engine {
struct NativeOffsetStorage;
}
namespace skeet::events {
struct EventRegistry;
struct GameEventReader;
struct EventPlayerIndices;
}
namespace skeet::esp {
struct EffectsSettings;
struct OtherEspSettings;
}
namespace skeet::misc {
struct AutoBuyConfig;
}
namespace skeet::rage {
struct HitlogsConfig;
}
namespace skeet::script {
struct GlobalsVariables;
}
namespace skeet::ui {
struct RenderQueue;
}

namespace skeet::client {

struct EnhancedFeatureBindings {
    ConfigRegistries& configuration;
    events::EventRegistry& events;
    const engine::NativeOffsetStorage& offsets;
    void* engine = nullptr;
    void* entity_list = nullptr;
    void* cvar = nullptr;
    const script::GlobalsVariables* globals = nullptr;
    void* const* player_resource = nullptr;
    void* const& local_player;
    const std::uint32_t& menu_color;
    const std::int32_t& dpi_scale_index;
    const esp::EffectsSettings& effects;
    esp::OtherEspSettings& other_esp;
    const rage::HitlogsConfig& hitlogs;
    misc::AutoBuyConfig& auto_buy;
};

class EnhancedFeaturesRuntime final {
public:
    explicit EnhancedFeaturesRuntime(EnhancedFeatureBindings bindings);
    ~EnhancedFeaturesRuntime();
    EnhancedFeaturesRuntime(const EnhancedFeaturesRuntime&) = delete;
    EnhancedFeaturesRuntime& operator=(const EnhancedFeaturesRuntime&) = delete;

    void on_player_spawn(events::GameEventReader& event, events::EventPlayerIndices& players);
    void on_player_hurt(events::GameEventReader& event, events::EventPlayerIndices& players);
    void on_item_purchase(events::GameEventReader& event, events::EventPlayerIndices& players);
    void on_round_prestart();
    void render_hud(ui::RenderQueue& queue);

private:
    friend void record_native_aim_fire(std::int32_t id, std::int32_t target,
                                       std::int32_t hitgroup, std::int32_t damage,
                                       float hit_chance, std::int32_t shot_tick) noexcept;
    friend void record_native_aim_hit(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                                      std::int32_t damage) noexcept;
    friend void record_native_aim_miss(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                                       std::uint32_t reason) noexcept;

    struct Impl;
    Impl* impl_ = nullptr;
};

void record_native_aim_fire(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                            std::int32_t damage, float hit_chance,
                            std::int32_t shot_tick) noexcept;
void record_native_aim_hit(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                           std::int32_t damage) noexcept;
void record_native_aim_miss(std::int32_t id, std::int32_t target, std::int32_t hitgroup,
                            std::uint32_t reason) noexcept;
void set_unsafe_dt_recharge_warning(bool active) noexcept;
void render_native_hud_overlays(ui::RenderQueue& queue) noexcept;

} // namespace skeet::client
