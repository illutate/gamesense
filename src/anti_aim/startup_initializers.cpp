#include "skeet/anti_aim/startup_initializers.hpp"
#include "skeet/client/native_startup.hpp"

namespace skeet::anti_aim {
namespace {
void initialize_enabled(void* context) { //434931DE
    auto& source = *static_cast<StartupInitializerBindings*>(context);
    append_config_link(source.configuration.descriptors, source.fake_lag.enabled);
}
void initialize_hotkey(void* context) { //434931F8
    auto& source = *static_cast<StartupInitializerBindings*>(context);
    append_config_link(source.configuration.descriptors, source.fake_lag.hotkey);
    skeet::initialize_hotkey(source.configuration.hotkeys, source.fake_lag.hotkey.value, 0x1BU,
                             HotkeyMode::always);
}
void initialize_slow_motion_key(void* context) { //43493456
    auto& source = *static_cast<StartupInitializerBindings*>(context);
    append_config_link(source.configuration.descriptors, source.other.slow_motion_key);
    skeet::initialize_hotkey(source.configuration.hotkeys, source.other.slow_motion_key.value,
                             0x14U, HotkeyMode::hold);
}
void initialize_leg_movement(void* context) { //43490A8D
    auto& source = *static_cast<StartupInitializerBindings*>(context);
    append_config_link(source.configuration.descriptors, source.other.leg_movement);
    source.other.leg_movement.value = 0;
}
}

void bind_native_startup_initializers(client::NativeStartupSources& sources,
                                      StartupInitializerBindings& bindings) {
    sources.bind_initializer({0x434931DEU, &bindings, initialize_enabled});
    sources.bind_initializer({0x434931F8U, &bindings, initialize_hotkey});
    sources.bind_initializer({0x43493456U, &bindings, initialize_slow_motion_key});
    sources.bind_initializer({0x43490A8DU, &bindings, initialize_leg_movement});
}
} // namespace skeet::anti_aim
