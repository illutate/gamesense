#include "skeet/skins/agent_receive.hpp"
#include "skeet/client/entity_update_dispatch.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::skins {
namespace {
AgentModelReceiver* active_receiver = nullptr;
void agent_model_proxy(const engine::RecvProxyData* data, void* entity, void* output) {
    active_receiver->receive(data, entity, output);
}
// Exact initialization metadata in ctx.h: class/table/property followed by
// replacement4335CCD8 and the43476A8C node. Original callback is node+8.
constexpr AgentModelReceiveDescriptor descriptor{0x0F0EDE49, 0x8C24DA3D, 0x0BA094FB,
                                                 0x4335CCD8, 0x43476A8C, agent_model_proxy};
}
void AgentModelReceiver::receive(const engine::RecvProxyData* data, void* entity, void* output) {
    const auto* handle = engine::invoke<const std::uint32_t*>(entity, 2);
    const auto index =
        *handle == 0xFFFFFFFFu ? 0x1FFFu : std::uint32_t(static_cast<std::uint16_t>(*handle));
    if (index == std::uint32_t(engine::invoke<std::int32_t>(bindings_.engine_client, 12))) {
        globals_.original_handle = *handle; //Native rereads the word after GetLocalPlayer.
        std::memcpy(&globals_.original_model, reinterpret_cast<const std::byte*>(data) + 8,
                    sizeof(globals_.original_model));
    }
    bindings_.original(data, entity, output);
}
const AgentModelReceiveDescriptor& agent_model_receive_descriptor() noexcept {
    return descriptor;
}
void bind_agent_model_receiver(AgentModelReceiver& receiver,
                               client::EntityUpdatePatch& patch) noexcept {
    active_receiver = &receiver;
    receiver.bindings_.original =
        reinterpret_cast<engine::RecvVarProxy>(std::uintptr_t(std::uint32_t(patch.after)));
    patch.before = std::bit_cast<std::int32_t>(
        std::uint32_t(reinterpret_cast<std::uintptr_t>(descriptor.replacement)));
}
} //namespace skeet::skins
