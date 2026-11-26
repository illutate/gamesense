#include "skeet/engine/bindings.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace skeet::engine {
namespace {
void check_name(std::string_view value, const char* description) {
    if (value.empty() || value.find('\0') != std::string_view::npos)
        throw std::invalid_argument(description);
}

constexpr std::array consumers{
    InterfaceRequest{"entity_list", "client.dll", "VClientEntityList003"}, // blob +0x3B1F2
    InterfaceRequest{"surface", "vguimatsurface.dll", "VGUI_Surface031"}, // +0x4A69B
    InterfaceRequest{"base_client", "client.dll",
                     "VClient018"}, //working dump; blob+0x4B02F names client_panorama.dll
    InterfaceRequest{"vgui_system", "vgui2.dll", "VGUI_System010"}, // +0x4BA28
    InterfaceRequest{"trace", "engine.dll", "EngineTraceClient004"}, // +0x5D3DA
    InterfaceRequest{"effects", "engine.dll", "VEngineEffects001"}, // +0x5D6D6
    InterfaceRequest{"string_tables", "engine.dll", "VEngineClientStringTable001"}, // +0x5E2AA
    InterfaceRequest{"engine", "engine.dll", "VEngineClient014"} // +0x6AB87
};
}

InterfaceFactory LoadedModuleFactories::factory(std::string_view module) {
    check_name(module, "Invalid engine module name");
#if defined(_WIN32)
    const std::string name(module);
    const auto loaded = GetModuleHandleA(name.c_str());
    if (!loaded)
        throw std::runtime_error("Engine module is not loaded: " + name);
    const auto address = GetProcAddress(loaded, "CreateInterface");
    if (!address)
        throw std::runtime_error("Engine module has no CreateInterface export: " + name);
    return reinterpret_cast<InterfaceFactory>(address);
#else
    throw std::runtime_error("Loaded engine interface factories require Windows");
#endif
}

void InterfaceBindings::bind(std::span<const InterfaceRequest> requests,
                             FactoryProvider& provider) {
    std::vector<BoundInterface> discovered;
    discovered.reserve(requests.size());
    // Validate the complete manifest before any factory can have side effects.
    for (std::size_t i = 0; i != requests.size(); ++i) {
        const auto& request = requests[i];
        check_name(request.name, "Invalid interface binding name");
        check_name(request.module, "Invalid interface module name");
        check_name(request.version, "Invalid interface version");
        for (std::size_t j = 0; j != i; ++j)
            if (requests[j].name == request.name)
                throw std::invalid_argument("Duplicate interface binding name");
    }
    for (const auto& request : requests) {
        const auto factory = provider.factory(request.module);
        if (!factory)
            throw std::runtime_error("Missing engine interface factory: " +
                                     std::string(request.module));
        const std::string version(request.version);
        int status = -1;
        void* object = factory(version.c_str(), &status);
        if (!object)
            throw std::runtime_error("Missing engine interface: " + std::string(request.module) +
                                     "!" + version);
        discovered.push_back(
            {std::string(request.name), std::string(request.module), version, object});
    }
    bindings_.swap(discovered);
}

void InterfaceBindings::clear() noexcept {
    bindings_.clear();
}

void* InterfaceBindings::at(std::string_view name) const {
    const auto found = std::find_if(bindings_.begin(), bindings_.end(),
                                    [name](const auto& binding) { return binding.name == name; });
    if (found == bindings_.end())
        throw std::out_of_range("Engine interface is not bound: " + std::string(name));
    return found->object;
}

std::span<const InterfaceRequest> embedded_interface_consumers() noexcept {
    return consumers;
}
} // namespace skeet::engine
