#include "skeet/skins/sequence_receive.hpp"
#include "skeet/engine/interface_call.hpp"
#include <bit>
#include <cstring>

namespace skeet::skins {
namespace {
SequenceReceivers* active_receivers = nullptr;
template <class T> T field(const void* object, std::ptrdiff_t offset) {
    T value;
    std::memcpy(&value, static_cast<const std::byte*>(object) + offset, sizeof value);
    return value;
}
std::uint32_t handle(void* entity) {
    return *engine::invoke<const std::uint32_t*>(entity, 2);
}
std::ptrdiff_t offset(const SequenceReceiveBindings& b, std::uint32_t pointer, std::uint32_t key,
                      std::uint32_t mask) {
    return std::bit_cast<std::int32_t>(*b.offsets.globals.pointers.at(pointer) ^
                                       b.offsets.globals.keys.at(key) ^ mask);
}
bool active(const SequenceReceiveBindings& bindings) {
    auto* const player = bindings.local_player;
    if (!player || engine::invoke<bool>(bindings.engine, 82) ||
        engine::invoke<bool>(bindings.engine, 93))
        return false;
    return (field<std::int32_t>(player, offset(bindings, 0x434688B0, 0x4346A518, 0x8B5586F6)) == 3
                ? bindings.settings.knife_ct.value
                : bindings.settings.knife_t.value) != 0;
}
void sequence_proxy(const engine::RecvProxyData* data, void* entity, void* output) {
    active_receivers->receive_sequence(data, entity, output);
}
void model_proxy(const engine::RecvProxyData* data, void* entity, void* output) {
    active_receivers->receive_model(data, entity, output);
}
void parity_proxy(const engine::RecvProxyData* data, void* entity, void* output) {
    active_receivers->receive_parity(data, entity, output);
}
void weapon_proxy(const engine::RecvProxyData* data, void* entity, void* output) {
    active_receivers->receive_weapon_handle(data, entity, output);
}
constexpr std::array<SequenceReceiveDescriptor, 4> descriptors{{
    {0xA48, 0xDD75A186, 0xB05C42F2, 0xA5BF8ECD, 0x43365510, 0x434769BC, sequence_proxy},
    {0xA5C, 0xDD75A186, 0xB05C42F2, 0x0BA094FB, 0x433B1669, 0x434769AC, model_proxy},
    {0xA70, 0xDD75A186, 0xB05C42F2, 0x91E87C84, 0x433FD58A, 0x43476B0C, parity_proxy},
    {0xA84, 0xAEEB25F3, 0x6B34DA8F, 0x623C927E, 0x4334B4A0, 0x43476A6C, weapon_proxy},
}};
}
void SequenceReceivers::receive_sequence(const engine::RecvProxyData* data, void* entity,
                                         void* output) {
    if (!active(bindings)) {
        bindings.sequence(data, entity, output);
        return;
    }
    previous_handle = handle(entity);
    previous_sequence = field<std::int32_t>(data, 8);
    // Native queries the engine handle again after publishing both words.
    state.viewmodels.push_back({handle(entity), -1, field<std::int16_t>(data, 8)});
}
void SequenceReceivers::receive_model(const engine::RecvProxyData* data, void* entity,
                                      void* output) {
    if (!active(bindings)) {
        bindings.model(data, entity, output);
        return;
    }
    state.viewmodels.push_back({handle(entity), field<std::int16_t>(data, 8), -1});
}
void SequenceReceivers::receive_parity(const engine::RecvProxyData* data, void* entity,
                                       void* output) {
    if (active(bindings)) {
        const auto value = handle(entity);
        const auto end = state.viewmodels.end();
        auto found = state.viewmodels.begin();
        while (found != end && found->handle != value)
            ++found;
        if ((found == end || found->sequence == -1) && previous_handle == value &&
            previous_sequence !=
                field<std::int32_t>(entity, offset(bindings, 0x43467ABC, 0x4346A56C, 0xA25D8271)))
            state.viewmodels.push_back({value, -1, std::int16_t(previous_sequence)});
    }
    bindings.parity(data, entity, output);
}
void SequenceReceivers::receive_weapon_handle(const engine::RecvProxyData* data, void* entity,
                                              void* output) {
    if (!active(bindings)) {
        bindings.weapon_handle(data, entity, output);
        return;
    }
    bindings.weapon_handle(data, nullptr, output);
    const auto value = handle(entity);
    state.weapon_handles.push_back({value, field<std::uint32_t>(data, 8)});
}
void SequenceReceivers::reset() noexcept {
    state.models.fill({-1, -1, -1});
    state.default_models.fill({-1, -1, -1});
    previous_handle = 0xFFFFFFFF;
    previous_sequence = -1;
    state.viewmodels.clear();
}
std::span<const SequenceReceiveDescriptor, 4> sequence_receive_descriptors() noexcept {
    return descriptors;
}
void bind_sequence_receive_runtime(SequenceReceivers& runtime) noexcept {
    active_receivers = &runtime;
}
void bind_sequence_receive_runtime(
    SequenceReceivers& runtime, std::span<client::EntityUpdatePatch* const, 4> patches) noexcept {
    bind_sequence_receive_runtime(runtime);
    const auto original = [](const client::EntityUpdatePatch* patch) {
        return reinterpret_cast<engine::RecvVarProxy>(std::uintptr_t(std::uint32_t(patch->after)));
    };
    runtime.bindings.sequence = original(patches[0]);
    runtime.bindings.model = original(patches[1]);
    runtime.bindings.parity = original(patches[2]);
    runtime.bindings.weapon_handle = original(patches[3]);
    for (std::size_t index = 0; index < descriptors.size(); ++index)
        patches[index]->before = std::bit_cast<std::int32_t>(
            std::uint32_t(reinterpret_cast<std::uintptr_t>(descriptors[index].replacement)));
}
void unbind_sequence_receive_runtime(SequenceReceivers&) noexcept {
    active_receivers = nullptr;
}
} //namespace skeet::skins
