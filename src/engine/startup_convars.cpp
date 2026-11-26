#include "skeet/engine/startup_convars.hpp"
#include "skeet/client/startup.hpp"
#include "skeet/engine/interface_call.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace skeet::engine {
namespace {
// The working dump's decoded objects name these exact ConVars at+C. Each
// key is independently present in the original consumers of its native cell.
constexpr std::array<StartupConVarSlot, 9> slots{
    {{0x43467FBCu, 0x96BEB183u, "rate"},
     {0x43467130u, 0x99854835u, "net_maxroutable"},
     {0x4346A5B4u, 0x7CBE81A1u, "cl_ragdoll_workaround_threshold"},
     {0x43468BA8u, 0x30911EABu, "cl_smooth"},
     {0x434671ACu, 0xD8BFAEFFu, "cl_cmdrate"},
     {0x4346747Cu, 0x03B4819Cu, "cl_updaterate"},
     {0x4346A748u, 0x7D9DC86Fu, "cl_interp"},
     {0x4346A330u, 0xBE64D223u, "cl_interp_ratio"},
     {0x43468EA4u, 0xFCD2DA7Au, "weapon_accuracy_nospread"}}};
class EngineConVarProvider final : public StartupConVarProvider {
public:
    void* find_variable(void* cvar, const char* name) override {
        return invoke<void*>(cvar, 15, name);
    }
};
NativeConVarReference reference(NativeDataImage& data, std::size_t index) {
    const auto& slot = slots[index];
    return {data.native_cell<std::uint32_t>(slot.address), slot.xor_key};
}
}
std::span<const StartupConVarSlot, 9> startup_convar_slots() noexcept {
    return slots;
}
client::StartupConVars StartupConVarBindings::startup_variables() const {
    return {reference(data_, 0), reference(data_, 1), reference(data_, 2), reference(data_, 3),
            reference(data_, 4), reference(data_, 5), reference(data_, 6), reference(data_, 7)};
}
NativeConVarReference StartupConVarBindings::accuracy_variable() const {
    return reference(data_, 8);
}

StartupConVarBindings discover_startup_convars(NativeDataImage& data, void* cvar,
                                               StartupConVarProvider& provider) {
    if (!cvar)
        throw std::invalid_argument("Startup ConVar discovery requires the real ICvar interface");
    std::array<std::uint32_t*, slots.size()> destinations;
    for (std::size_t index = 0; index < slots.size(); ++index)
        destinations[index] = &data.native_cell<std::uint32_t>(slots[index].address);
    std::array<void*, slots.size()> found;
    for (std::size_t index = 0; index < slots.size(); ++index) {
        found[index] = provider.find_variable(cvar, slots[index].name);
        if (!found[index])
            throw std::runtime_error(std::string("Required startup ConVar is unavailable: ") +
                                     slots[index].name);
    }
    // Every destination and lookup is validated before these nonthrowing
    // stores. Other native consumers continue to read the encoded words.
    for (std::size_t index = 0; index < slots.size(); ++index)
        *destinations[index] =
            std::uint32_t(reinterpret_cast<std::uintptr_t>(found[index])) ^ slots[index].xor_key;
    return StartupConVarBindings(data);
}
StartupConVarBindings discover_startup_convars(NativeDataImage& data, void* cvar) {
    EngineConVarProvider provider;
    return discover_startup_convars(data, cvar, provider);
}
} //namespace skeet::engine
