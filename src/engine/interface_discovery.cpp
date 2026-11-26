#include "skeet/engine/interface_discovery.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace skeet::engine {
namespace {
// Fixed-build interface data; these integers are logical data-cell identifiers.
constexpr std::array<BuildInterfaceRecipe, 41> build_requests{{
    {0x4C8u,
     0x4346A650u,
     0xA90F554Cu,
     0x43468A4Cu,
     0u,
     {"studio_renderer", "studiorender.dll", "VStudioRender026"}},
    {0x4D4u,
     0x4346A55Cu,
     0x66DCB316u,
     0x434683E0u,
     0u,
     {"base_client", "client.dll", "VClient018"}},
    {0x4E0u,
     0x4346A55Cu,
     0x530147E3u,
     0x4346A890u,
     0u,
     {"entity_list", "client.dll", "VClientEntityList003"}},
    {0x4ECu,
     0x4346A55Cu,
     0x2CEEF853u,
     0x43469264u,
     0u,
     {"prediction", "client.dll", "VClientPrediction001"}},
    {0x4F8u,
     0x4346A55Cu,
     0xE620BB3Cu,
     0x4346A7F8u,
     0u,
     {"game_movement", "client.dll", "GameMovement001"}},
    {0x504u,
     0x4346A55Cu,
     0x370B6789u,
     0x43467D84u,
     0u,
     {"game_console", "client.dll", "GameConsole004"}},
    {0x510u,
     0x4346A55Cu,
     0xB49164FAu,
     0x4346A3A0u,
     0u,
     {"client_leaf_system", "client.dll", "ClientLeafSystem002"}},
    {0x51Cu,
     0x434670ACu,
     0x5C0F884Bu,
     0x43468C0Cu,
     0u,
     {"match_framework", "matchmaking.dll", "MATCHFRAMEWORK_001"}},
    {0x528u,
     0x43467DACu,
     0x6A62EA4Cu,
     0x4346A89Cu,
     0u,
     {"input_system", "inputsystem.dll", "InputSystemVersion001"}},
    {0x534u,
     0x43467DACu,
     0xCB2A089Au,
     0x43468D24u,
     0u,
     {"input_stack_system", "inputsystem.dll", "InputStackSystemVersion001"}},
    {0x540u,
     0x43468D20u,
     0xFC552D7Cu,
     0x4346A514u,
     0u,
     {"engine_client", "engine.dll", "VEngineClient014"}},
    {0x54Cu,
     0x43468D20u,
     0x6A2E45E8u,
     0x43468304u,
     0u,
     {"engine_trace", "engine.dll", "EngineTraceClient004"}},
    {0x558u,
     0x43468D20u,
     0xE9DDF952u,
     0x4346A614u,
     0u,
     {"model_info", "engine.dll", "VModelInfoClient004"}},
    {0x564u,
     0x43468D20u,
     0x22DC4D99u,
     0x43468260u,
     0u,
     {"debug_overlay", "engine.dll", "VDebugOverlay004"}},
    {0x570u,
     0x43468D20u,
     0xCFE960F2u,
     0x43468974u,
     0u,
     {"model_render", "engine.dll", "VEngineModel016"}},
    {0x57Cu,
     0x43468D20u,
     0x1D91BB6Eu,
     0x4346790Cu,
     0u,
     {"render_view", "engine.dll", "VEngineRenderView014"}},
    {0x588u,
     0x43468D20u,
     0xE8D5CFDEu,
     0x434693F8u,
     0u,
     {"engine_sound", "engine.dll", "IEngineSoundClient003"}},
    {0x594u,
     0x43468D20u,
     0xF8DA3AF4u,
     0x4346712Cu,
     0u,
     {"engine_vgui", "engine.dll", "VEngineVGui001"}},
    {0x5A0u,
     0x43468D20u,
     0x59E431D7u,
     0x43467198u,
     0u,
     {"string_tables", "engine.dll", "VEngineClientStringTable001"}},
    {0x5ACu,
     0x43468D20u,
     0x8D6D3467u,
     0x43467014u,
     0u,
     {"static_props", "engine.dll", "StaticPropMgrClient005"}},
    {0x5B8u,
     0x43468D20u,
     0x22078B6Eu,
     0x43467FB0u,
     0u,
     {"tool_framework", "engine.dll", "VTOOLFRAMEWORKVERSION002"}},
    {0x5C4u,
     0x43468D20u,
     0x70F6F65Bu,
     0x43467F10u,
     0u,
     {"spatial_partition", "engine.dll", "SpatialPartition001"}},
    {0x5D0u,
     0x43468D20u,
     0x5A17F0FFu,
     0x43469214u,
     0u,
     {"engine_tool", "engine.dll", "VENGINETOOL003"}},
    {0x5DCu,
     0x43468D20u,
     0xDEF79758u,
     0x43468364u,
     0u,
     {"game_event_manager", "engine.dll", "GAMEEVENTSMANAGER002"}},
    {0x5E8u,
     0x43467AFCu,
     0xFF7E6279u,
     0x434689C0u,
     0u,
     {"surface", "vguimatsurface.dll", "VGUI_Surface031"}},
    {0x5F4u,
     0x4346889Cu,
     0xF7986847u,
     0x43468898u,
     0u,
     {"vgui_input", "vgui2.dll", "VGUI_Input005"}},
    {0x600u,
     0x4346889Cu,
     0xE4271189u,
     0x4346A44Cu,
     0u,
     {"vgui_panel", "vgui2.dll", "VGUI_Panel009"}},
    {0x60Cu,
     0x4346889Cu,
     0x0F80C440u,
     0x43467118u,
     0u,
     {"vgui_system", "vgui2.dll", "VGUI_System010"}},
    {0x618u, 0x43468A7Cu, 0x9B263577u, 0x4346A87Cu, 0u, {"cvar", "vstdlib.dll", "VEngineCvar007"}},
    {0x624u,
     0x43468B74u,
     0xF0196491u,
     0x43469268u,
     0u,
     {"physics_surface_props", "vphysics.dll", "VPhysicsSurfaceProps001"}},
    {0x630u,
     0x4346A600u,
     0x945D3F26u,
     0x4346A4E0u,
     0u,
     {"material_system", "materialsystem.dll", "VMaterialSystem080"}},
    {0x63Cu,
     0x43468D98u,
     0x2CF5E2E3u,
     0x43469330u,
     0u,
     {"model_cache", "datacache.dll", "MDLCache004"}},
    {0x648u,
     0x434670E4u,
     0x64884D32u,
     0x43467470u,
     0u,
     {"server_game_dll", "server.dll", "ServerGameDLL005"}},
    {0x654u,
     0x43468D40u,
     0x7F31CFBDu,
     0x43467010u,
     0u,
     {"sound_system", "soundsystem.dll", "SoundSystem001"}},
    {0x660u,
     0x43469234u,
     0x877DC74Cu,
     0x43468E98u,
     0u,
     {"panorama_engine", "panorama.dll", "PanoramaUIEngine001"}},
    {0x66Cu,
     0x4346762Cu,
     0x15869CDFu,
     0x43467F14u,
     0u,
     {"panorama_ui_client", "panoramauiclient.dll", "PanoramaUIClient001"}},
    {0x678u,
     0x4346A604u,
     0xD5500FAEu,
     0x4346A470u,
     0u,
     {"shader_device_manager", "shaderapidx9.dll", "ShaderDeviceMgr001"}},
    {0x684u,
     0x4346A604u,
     0xA9097514u,
     0x4346A8A4u,
     0u,
     {"shader_api", "shaderapidx9.dll", "ShaderApi029"}},
    {0x690u,
     0x4346A604u,
     0x54E48848u,
     0x4346A33Cu,
     0u,
     {"shader_device", "shaderapidx9.dll", "ShaderDevice001"}},
    {0x69Cu,
     0x43467B74u,
     0x1E300843u,
     0x43467F68u,
     0u,
     {"filesystem", "filesystem_stdio.dll", "VFileSystem017"}},
    {0x6A8u,
     0x43468B74u,
     0x786E227Au,
     0x43467208u,
     0u,
     {"physics_collision", "vphysics.dll", "VPhysicsCollision007"}},
}};
constexpr std::array<InterfaceRequest, 7> requests{{
    {"engine_client", "engine.dll", "VEngineClient014"},
    {"entity_list", "client.dll", "VClientEntityList003"},
    {"base_client", "client.dll", "VClient018"},
    {"prediction", "client.dll", "VClientPrediction001"},
    {"surface", "vguimatsurface.dll", "VGUI_Surface031"},
    {"cvar", "vstdlib.dll", "VEngineCvar007"},
    {"engine_trace", "engine.dll", "EngineTraceClient004"},
}};
void validate_build_destinations(NativeDataImage& data) {
    for (const auto& recipe : build_requests) {
        (void)data.native_cell<void*>(recipe.module_cell);
        (void)data.native_cell<void*>(recipe.destination_cell);
        if (netvar_name_hash(recipe.request.version.data()) != recipe.interface_hash)
            throw std::logic_error(
                "Build interface name does not match its recovered registration hash");
    }
}
void publish_build_interface(NativeDataImage& data, const BuildInterfaceRecipe& recipe,
                             void* object) {
    // All 41 observed destination masks are zero. Keep the recovered storage
    // representation explicit, without replacing live results by dump values.
    const auto encoded = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object)) ^
                         recipe.destination_xor;
    std::memcpy(&data.native_cell<void*>(recipe.destination_cell), &encoded, sizeof encoded);
}
[[noreturn]] void missing_build_interface(const BuildInterfaceRecipe& recipe,
                                          BuildInterfaceFailureStage stage) {
    throw BuildInterfaceDiscoveryFailure(recipe, stage);
}
}
std::span<const InterfaceRequest, 7> core_interface_requests() noexcept {
    return requests;
}
CoreEngineInterfaces discover_core_interfaces(FactoryProvider& provider) {
    InterfaceBindings interfaces;
    interfaces.bind(requests, provider);
    return {interfaces.at("engine_client"), interfaces.at("entity_list"),
            interfaces.at("base_client"),   interfaces.at("prediction"),
            interfaces.at("surface"),       interfaces.at("cvar"),
            interfaces.at("engine_trace")};
}
CoreEngineInterfaces discover_core_interfaces(FactoryProvider& provider, CoreInterfaceCells cells) {
    const auto interfaces = discover_core_interfaces(provider);
    cells.engine_client = interfaces.engine_client;
    cells.entity_list = interfaces.entity_list;
    cells.base_client = interfaces.base_client;
    cells.prediction = interfaces.prediction;
    cells.surface = interfaces.surface;
    cells.cvar = interfaces.cvar;
    cells.engine_trace = interfaces.engine_trace;
    return interfaces;
}
CoreEngineInterfaces discover_core_interfaces(CoreInterfaceCells cells) {
    LoadedModuleFactories provider;
    return discover_core_interfaces(provider, cells);
}
std::span<const BuildInterfaceRecipe, 41> build_interface_manifest() noexcept {
    return build_requests;
}
BuildInterfaceDiscoveryFailure::BuildInterfaceDiscoveryFailure(
    const BuildInterfaceRecipe& recipe, BuildInterfaceFailureStage failure_stage)
    : std::runtime_error("Missing build interface: " + std::string(recipe.request.module) + "!" +
                         std::string(recipe.request.version)),
      operand(recipe.operand), destination_cell(recipe.destination_cell), stage(failure_stage) {}
void* BuildEngineInterfaces::at(std::string_view role) const {
    for (std::size_t index = 0; index < build_requests.size(); ++index)
        if (build_requests[index].request.name == role)
            return objects[index];
    throw std::out_of_range("Name is not a recovered build interface role");
}
CoreEngineInterfaces BuildEngineInterfaces::core() const noexcept {
    return {objects[10], objects[2], objects[1], objects[3], objects[24], objects[28], objects[11]};
}
BuildEngineInterfaces discover_build_interfaces(NativeDataImage& data,
                                                NativeImageEnvironment environment) {
    validate_build_destinations(data);
    BuildEngineInterfaces interfaces;
    for (std::size_t index = 0; index < build_requests.size(); ++index) {
        const auto& recipe = build_requests[index];
        auto* const module = static_cast<std::byte*>(data.native_cell<void*>(recipe.module_cell));
        auto* const registry = find_interface_registry(module, environment);
        if (!registry)
            missing_build_interface(recipe, BuildInterfaceFailureStage::module_registry);
        auto* const registration = find_interface_registration(registry, recipe.interface_hash);
        if (!registration || !registration->create)
            missing_build_interface(recipe, BuildInterfaceFailureStage::registration);
        auto* const object = registration->create();
        if (!object)
            missing_build_interface(recipe, BuildInterfaceFailureStage::creator);
        interfaces.objects[index] = object;
        publish_build_interface(data, recipe, object);
    }
    return interfaces;
}
BuildEngineInterfaces discover_build_interfaces(NativeDataImage& data) {
    return discover_build_interfaces(data, native_image_environment());
}
BuildEngineInterfaces discover_build_interfaces(FactoryProvider& provider, NativeDataImage& data) {
    validate_build_destinations(data);
    BuildEngineInterfaces interfaces;
    for (std::size_t index = 0; index < build_requests.size(); ++index) {
        const auto& recipe = build_requests[index];
        InterfaceFactory factory;
        try {
            factory = provider.factory(recipe.request.module);
        } catch (const std::runtime_error&) {
            missing_build_interface(recipe, BuildInterfaceFailureStage::factory);
        }
        if (!factory)
            missing_build_interface(recipe, BuildInterfaceFailureStage::factory);
        auto* const object = factory(recipe.request.version.data(), nullptr);
        if (!object)
            missing_build_interface(recipe, BuildInterfaceFailureStage::creator);
        interfaces.objects[index] = object;
        publish_build_interface(data, recipe, object);
    }
    return interfaces;
}
} //namespace skeet::engine
