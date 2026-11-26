#include "skeet/animation/runtime_bindings.hpp"
#include "skeet/engine/decoded_offset.hpp"
#include <stdexcept>

namespace skeet::animation {
namespace {
std::ptrdiff_t offset(const engine::NativeOffsetGlobals& globals, std::uint32_t pointer,
                      std::uint32_t key, std::uint32_t salt) {
    return engine::DecodedOffset(globals, pointer, key, salt, 0);
}
}

BoneRuntimeFields native_bone_runtime_fields(const engine::NativeOffsetGlobals& globals) {
    //43388720/433FD30D,433899B3,43341E47 and433FF63C. Pose parameters are
    //the24-float range decoded at4331ED7F and4341D407, not the layer vector.
    return {offset(globals, 0x43467ABCu, 0x4346A56Cu, 0xA25D8271u),
            offset(globals, 0x43467E8Cu, 0x43467900u, 0x32E4DAF1u),
            offset(globals, 0x434683B4u, 0x43467E50u, 0x57B2D535u),
            offset(globals, 0x4346703Cu, 0x43468FD4u, 0xAE5B982Eu),
            offset(globals, 0x43468B50u, 0x43467B48u, 0x38CA5228u),
            offset(globals, 0x43467634u, 0x43468F2Cu, 0xBB78DC43u),
            offset(globals, 0x43467A74u, 0x434678C0u, 0xCD812654u),
            offset(globals, 0x43467180u, 0x43467FA4u, 0xCA2B5FFBu),
            offset(globals, 0x43467F4Cu, 0x4346A8B0u, 0x3D52A1A7u),
            offset(globals, 0x43467CD8u, 0x43468DD8u, 0x01D0BACCu),
            offset(globals, 0x4346A660u, 0x434679E0u, 0xB538BBA5u),
            offset(globals, 0x43468368u, 0x43467FACu, 0x3B1F6ECCu),
            offset(globals, 0x434688D4u, 0x43469030u, 0xF8205EBAu),
            offset(globals, 0x43468D30u, 0x43468ED4u, 0x2A671B42u),
            offset(globals, 0x434670E0u, 0x43467D28u, 0x4E66BB34u),
            offset(globals, 0x43469224u, 0x4346718Cu, 0x83A6CF2Bu),
            offset(globals, 0x434670CCu, 0x43468BCCu, 0xA9EE6062u),
            offset(globals, 0x43468240u, 0x4346A84Cu, 0xC2B6FB10u),
            offset(globals, 0x43468E7Cu, 0x434674A8u, 0x54783141u),
            offset(globals, 0x43467D78u, 0x43467B80u, 0x1CAAF0A8u)};
}

RemoteSetupBonesFields
native_remote_setup_bones_fields(const engine::NativeOffsetGlobals& globals) {
    //433FF7F6 and the linked-entity traversal in4339C7A0. The -14/-10 child
    //handle displacements are applied by that consumer, not folded in here.
    return {offset(globals, 0x43469064u, 0x4346A484u, 0x17978090u),
            offset(globals, 0x4346A808u, 0x43468BE8u, 0x2B554AD0u),
            offset(globals, 0x43468EA0u, 0x4346A74Cu, 0xE23A9BE1u),
            offset(globals, 0x43469344u, 0x43467018u, 0x49D93E62u)};
}

BoneRuntime native_bone_runtime(const engine::NativeDataImage& data,
                                const engine::NativeOffsetGlobals& globals,
                                BoneAttachmentUpdate attachment_update,
                                const RageTrigonometry& trigonometry) {
    if (!attachment_update ||
        attachment_update != data.native_cell<BoneAttachmentUpdate>(0x43468F80u))
        throw std::invalid_argument(
            "Bone runtime requires the published build animation attachment entry");
    auto* const entity_list = data.native_cell<void*>(0x4346A890u);
    if (!entity_list || !trigonometry.sine || !trigonometry.cosine)
        throw std::invalid_argument(
            "Bone runtime requires its canonical entity list and CRT trigonometry");
    return {native_bone_runtime_fields(globals), entity_list, attachment_update, trigonometry};
}

RemoteSetupBonesBindings
native_remote_setup_bones_bindings(const engine::NativeDataImage& data,
                                   const engine::NativeOffsetGlobals& globals,
                                   const BoneRuntime& bones, client::PlayerStateCache& players,
                                   const RemoteAnimationPreparationRuntime& preparation) {
    if (bones.entity_list != data.native_cell<void*>(0x4346A890u) ||
        bones.update_attachments != data.native_cell<BoneAttachmentUpdate>(0x43468F80u))
        throw std::invalid_argument(
            "Remote bone setup must share canonical bone runtime dependencies");
    auto* const global_vars = data.native_cell<std::byte*>(0x43467B04u);
    if (!global_vars || reinterpret_cast<std::uintptr_t>(global_vars) % alignof(float))
        throw std::invalid_argument("Remote bone setup requires published aligned gpGlobals");
    return {bones,
            players,
            preparation,
            native_remote_setup_bones_fields(globals),
            data.native_cell<const std::int32_t*>(0x43469060u),
            data.native_cell<BoneEnginePrepare>(0x4346A38Cu),
            data.native_cell<BoneEngineThreadPredicate>(0x43476920u),
            data.native_cell<BoneEngineAttachments>(0x4346A6B0u),
            *reinterpret_cast<float*>(global_vars + 0x14),
            native_remote_bone_thread_guard};
}
} // namespace skeet::animation
