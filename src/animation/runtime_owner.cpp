#include "skeet/animation/runtime_owner.hpp"
#include "skeet/client/runtime_storage.hpp"
#include <array>
#include <stdexcept>

namespace skeet::animation {
namespace {
client::ClientRuntimeStorage& require_storage(client::ClientRuntimeStorage& storage,
                                              const client::EngineRuntimeBindings& engine) {
    const auto& data = storage.data();
    const auto core = engine.interfaces.core();
    if (&engine.variables.data() != &data || !core.engine_client || !core.entity_list ||
        core.engine_client != data.native_cell<void*>(0x4346A514u) ||
        core.entity_list != data.native_cell<void*>(0x4346A890u) ||
        !data.native_cell<void*>(0x4346A894u))
        throw std::invalid_argument(
            "Animation runtime requires its initialized canonical engine bundle");
    if (!engine.modules.at(u"client.dll") || !engine.modules.at(u"engine.dll") ||
        engine.modules.at(u"client.dll") != data.native_cell<void*>(0x4346A55Cu) ||
        engine.modules.at(u"engine.dll") != data.native_cell<void*>(0x43468D20u))
        throw std::invalid_argument(
            "Animation runtime engine modules belong to another data image");
    auto* const globals = data.native_cell<std::byte*>(0x43467B04u);
    if (!globals || reinterpret_cast<std::uintptr_t>(globals) % alignof(float) ||
        data.native_cell<void*>(0x434678F8u) != globals + 4 ||
        data.native_cell<void*>(0x43467EB8u) != globals + 0x10 ||
        data.native_cell<void*>(0x4346A338u) != globals + 0x20)
        throw std::invalid_argument(
            "Animation runtime requires the canonical gpGlobals member aliases");
    const auto& offsets = storage.offsets();
    const auto begin = reinterpret_cast<std::uintptr_t>(offsets.words);
    const auto cursor = reinterpret_cast<std::uintptr_t>(offsets.cursor);
    if (!begin || begin % alignof(std::uint32_t) || cursor < begin + 4 || cursor > begin + 0x1000 ||
        (cursor - begin) % 4 || offsets.globals.pointers.empty())
        throw std::invalid_argument("Animation runtime requires initialized native offset storage");
    for (const auto& [identity, pointer] : offsets.globals.pointers) {
        (void)identity;
        const auto address = reinterpret_cast<std::uintptr_t>(pointer);
        // Three published zero-offset sentinels intentionally use word0.
        if (address < begin || address >= cursor || (address - begin) % 4)
            throw std::invalid_argument(
                "Animation offset points outside its canonical published word allocation");
    }
    return storage;
}
RemoteAnimationPreparationBindings
preparation_bindings(client::ClientRuntimeStorage& storage,
                     const RemoteAnimationPreparationFields& fields,
                     const RageTrigonometry& trigonometry) {
    // Force all required operand identities to exist before publishing a
    // callable runtime; subsequent preparation calls still read these live.
    (void)std::array<std::ptrdiff_t, 12>{
        fields.frame,           fields.ik_mask,         fields.flags,      fields.previous_flags,
        fields.active_weapon,   fields.animation_state, fields.view_gate,  fields.observer_mode,
        fields.observer_target, fields.world_model,     fields.eye_origin, fields.duck_amount};
    auto& data = storage.data();
    auto* const globals = data.native_cell<std::byte*>(0x43467B04u);
    return {fields,
            trigonometry,
            data.native_cell<void*>(0x4346A514u),
            data.native_cell<void*>(0x4346A890u),
            data.native_cell<void*>(0x4346A894u),
            data.native_cell<void*>(0x43475808u),
            *reinterpret_cast<const std::int32_t*>(globals + 4),
            data.native_cell<RageVector>(0x43479CCCu),
            data.native_cell<RageVector>(0x43479CD8u)};
}
}
RuntimeOwner::RuntimeOwner(client::ClientRuntimeStorage& storage,
                           const client::EngineRuntimeBindings& engine,
                           client::PlayerStateCache& players)
    : storage_(require_storage(storage, engine)), players_(players),
      trigonometry_(native_sse_trigonometry()), preparation_fields_(storage_.offsets().globals),
      bones_(native_bone_runtime(storage_.data(), storage_.offsets().globals,
                                 engine.update_bone_attachments, trigonometry_)),
      preparation_(preparation_bindings(storage_, preparation_fields_, trigonometry_)),
      remote_bindings_(native_remote_setup_bones_bindings(
          storage_.data(), storage_.offsets().globals, bones_, players_, preparation_)),
      remote_(remote_bindings_) {}
} //namespace skeet::animation
