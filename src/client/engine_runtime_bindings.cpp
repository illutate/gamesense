#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/source_initialization_imports.hpp"
#include "skeet/engine/build_window_bindings.hpp"
#include "skeet/engine/build_game_state_bindings.hpp"
#include "skeet/engine/build_client_state_aliases.hpp"
#include "skeet/engine/build_script_bindings.hpp"
#include "skeet/engine/build_misc_engine_bindings.hpp"
#include "skeet/engine/build_command_bindings.hpp"
#include "skeet/engine/build_legit_bindings.hpp"
#include "skeet/engine/build_entity_animation_bindings.hpp"
#include "skeet/engine/build_eye_adjustment_bindings.hpp"
#include "skeet/engine/build_visual_interface_bindings.hpp"
#include "skeet/engine/build_esp_bindings.hpp"
#include "skeet/engine/build_material_bindings.hpp"
#include "skeet/engine/build_world_bindings.hpp"
#include "skeet/engine/build_packet_bindings.hpp"
#include "skeet/engine/build_shared_esp_bindings.hpp"
#include "skeet/engine/build_scene_bindings.hpp"
#include "skeet/engine/build_loader_inspection_bindings.hpp"
#include "skeet/engine/build_runtime_type_bindings.hpp"
#include "skeet/engine/tier0_startup_exports.hpp"
#include "skeet/engine/animation_startup_patterns.hpp"
#include <stdexcept>

namespace skeet::client {
EngineRuntimeBindings
ClientRuntimeStorage::initialize_engine_bindings(const engine::SourceInitializationImports& imports,
                                                 std::uint32_t timestamp_low) {
    if (engine_bindings_started_)
        throw std::logic_error("Engine bindings already started for this client storage");
    if (offsets_.words)
        throw std::logic_error("Source engine initialization cannot adopt existing offset storage");
    if (imports.memory.allocate != memory_.allocate || imports.memory.free != memory_.free ||
        imports.memory.process_heap != memory_.process_heap)
        throw std::invalid_argument("Engine bindings must use the canonical client process heap");
    engine_bindings_started_ = true;

    auto modules = engine::publish_build_module_bases(*data_, imports);
    engine::initialize_build_window_bindings(*data_, imports.modules);
    auto interfaces = engine::discover_build_interfaces(*data_, imports.images);
    engine::initialize_build_game_state_bindings(*data_);
    engine::initialize_build_client_state_aliases(*data_);
    engine::initialize_build_script_bindings(*data_);
    engine::initialize_build_loader_inspection_inputs(*data_);
    engine::initialize_build_client_factories(*data_);
    engine::initialize_build_visual_interface_bindings(*data_, imports.images);
    engine::initialize_build_esp_bindings(*data_, imports.images);
    engine::initialize_build_material_bindings(*data_, imports.modules, imports.images);
    engine::initialize_build_misc_engine_bindings(*data_);
    const auto command = engine::initialize_build_command_bindings(*data_);
    engine::initialize_build_legit_bindings(*data_);
    engine::initialize_build_world_bindings(*data_);
    engine::initialize_build_map_game_system_bindings(*data_);
    engine::initialize_build_packet_bindings(*data_);
    engine::initialize_build_shared_esp_bindings(*data_, imports.images);
    const auto core = interfaces.core();
    input_.interfaces.engine_client = core.engine_client;
    engine::initialize_build_offset_storage(offsets_, memory_, core.base_client,
                                            static_cast<std::byte*>(modules.at(u"client.dll")),
                                            timestamp_low);
    auto variables = engine::discover_build_startup_convars(*data_, core.cvar);
    const auto attachments = engine::initialize_build_animation_bindings(*data_, imports.modules);
    engine::initialize_build_animation_update_code(*data_, imports.modules);
    engine::initialize_build_animation_velocity_code(*data_, imports.modules);
    engine::initialize_animation_update_flag(*data_);
    engine::initialize_tier0_startup_exports(*data_, imports.modules, imports.images);
    engine::initialize_build_runtime_type_bindings(*data_, imports.images);
    engine::initialize_build_entity_animation_bindings(*data_, imports.modules, offsets_);
    engine::initialize_build_eye_adjustment_bindings(*data_);
    engine::initialize_build_scene_bindings(*data_);
    return {modules, interfaces, variables, attachments, command};
}
} // namespace skeet::client
