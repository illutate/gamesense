#include "skeet/client/hook_descriptor_cleanup.hpp"
#include "skeet/client/native_startup.hpp"
#include "skeet/client/shutdown.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace skeet::client {
namespace {
struct DescriptorCleanup {
    std::uint32_t initializer, finalizer, descriptor;
};
// Each initializer is MOV ECX,finalizer; JMP433837F5. Each finalizer is
// MOV ECX,descriptor; JMP433D2DC1. These are ordinary native bodies, not
// captured operand returns or a claimed game-owner factory order.
constexpr std::array<DescriptorCleanup, 26> recipes{{
    {0x4341f48c, 0x43406146, 0x43475954}, //0C4
    {0x43400941, 0x43341b52, 0x43475b48}, //0C8
    {0x4335939c, 0x434178d5, 0x43475878}, //0CC
    {0x433e87aa, 0x4341a06a, 0x43475998}, //0D0
    {0x433acf51, 0x43381d7a, 0x43475830}, //0D4
    {0x4340c133, 0x43402a1a, 0x4347589c}, //0D8
    {0x433f97b6, 0x433cdc42, 0x43475a28}, //0DC
    {0x433cd6c5, 0x433b196d, 0x43475b60}, //0E0
    {0x4341f401, 0x43328ad1, 0x43475864}, //0E4
    {0x4336a58b, 0x433b32e6, 0x43475aec}, //0E8
    {0x43388e1c, 0x433ebc6a, 0x43475b18}, //0EC
    {0x433b6e1b, 0x433f8fed, 0x43475a08}, //0F0
    {0x4336a322, 0x4338231e, 0x43475b34}, //0F4
    {0x4340c482, 0x43366333, 0x43475ac4}, //0F8
    {0x43336f65, 0x433662cd, 0x43475908}, //0FC
    {0x4339d11f, 0x4332b66e, 0x434758f4}, //100
    {0x4341cbcf, 0x434178eb, 0x434757f4}, //104
    {0x4337cab9, 0x433f52c9, 0x434757c8}, //108
    {0x433a17ae, 0x43348276, 0x43475a3c}, //10C
    {0x4334c748, 0x4331ff41, 0x43475b04}, //110
    {0x4333bf21, 0x433c9fa8, 0x43475a68}, //114
    {0x433e90fd, 0x433c1247, 0x434757b0}, //118
    {0x4335ba76, 0x433c8f1b, 0x434757e0}, //11C
    {0x4341875b, 0x433e3cab, 0x43475814}, //120
    {0x4338a373, 0x43368994, 0x434758b8}, //124
    {0x433d0e9d, 0x4339d52d, 0x4347591c}, //128
}};
HookDescriptorCleanupContext* active_cleanup = nullptr;

template <std::size_t Index> void finalize_descriptor() {
    auto& context = *active_cleanup;
    context.descriptors.clear(context.storage.at(recipes[Index].descriptor));
}
template <std::size_t... Index> constexpr auto finalizer_entries(std::index_sequence<Index...>) {
    return std::array<void (*)(), sizeof...(Index)>{finalize_descriptor<Index>...};
}
constexpr auto finalizers = finalizer_entries(std::make_index_sequence<recipes.size()>{});

template <std::size_t Index> void register_descriptor_finalizer(void* owner) {
    auto& context = *static_cast<HookDescriptorCleanupContext*>(owner);
    (void)context.callbacks.append(context.source_finalizer(recipes[Index].finalizer));
}
template <std::size_t... Index>
void bind_initializers(NativeStartupSources& sources, HookDescriptorCleanupContext& context,
                       std::index_sequence<Index...>) {
    (sources.bind_initializer(
         {recipes[Index].initializer, &context, register_descriptor_finalizer<Index>}),
     ...);
}
}

void (*HookDescriptorCleanupContext::source_finalizer(std::uint32_t address))() {
    for (std::size_t index = 0; index < recipes.size(); ++index) {
        if (recipes[index].finalizer == address) {
            if (active_cleanup && active_cleanup != this)
                throw std::logic_error(
                    "Descriptor exit callbacks require one stable source binding");
            // Source binding publication for the same native global storage.
            // This does not add a native initialization or cleanup operation.
            active_cleanup = this;
            return finalizers[index];
        }
    }
    throw std::out_of_range("Finalizer is not a recovered hook descriptor cleanup entry");
}

void bind_hook_descriptor_initializers(NativeStartupSources& sources,
                                       HookDescriptorCleanupContext& context) {
    bind_initializers(sources, context, std::make_index_sequence<recipes.size()>{});
}
}
