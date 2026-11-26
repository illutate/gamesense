#include "skeet/engine/focus_overlay_hooks.hpp"
#include "skeet/engine/hook_descriptor_storage.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/startup_bindings.hpp"
#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/hud/composite_runtime.hpp"
#include "skeet/menu/root_menu.hpp"
#include "skeet/menu/startup_data.hpp"
#include "skeet/render/startup_renderer.hpp"
#include "skeet/script/modules.hpp"
#include <atomic>
#include <cstring>
#include <stdexcept>

#if defined(_M_IX86) || defined(__i386__)
#define SKEET_FOCUS_ABI __fastcall
#else
#define SKEET_FOCUS_ABI
#endif
namespace skeet::engine {
namespace {
using SourceEntry = void(SKEET_FOCUS_ABI*)(void*, void*);
FocusOverlayHooks* active;
void SKEET_FOCUS_ABI first_entry(void* receiver, void*) {
    active->first_invocation(receiver);
}
void SKEET_FOCUS_ABI initialize_entry(void*, void*) {
    active->initialize();
}
void SKEET_FOCUS_ABI draw_entry(void* receiver, void*) {
    active->draw(receiver);
}
template <class T> T read(const std::byte* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof value);
    return value;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool in_region(const std::byte* module, const void* pointer, std::size_t size, std::uint32_t rva,
               std::uint32_t extent) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto begin = reinterpret_cast<std::uintptr_t>(module) + rva;
    return address >= begin && size <= extent && address - begin <= extent - size;
}
const std::uint32_t& accent(client::ClientRuntimeStorage& storage) {
    // Private persistence descriptors are unlinked before focus publication.
    // Rendering borrows the same live color cell as the actual menu widget.
    return storage.menu_startup_data().settings_values().menu_color;
}
VtableHookMetadata& installed_descriptor(void* receiver) {
    return *static_cast<VtableHookMetadata*>((*static_cast<void***>(receiver))[-2]);
}
}

void* find_build_focus_overlay_receiver(const NativeDataImage& data,
                                        const VtableHookMetadata& descriptor) {
    auto* module = data.native_cell<std::byte*>(0x43468D20);
    require(module && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x150,
            "Focus overlay requires the loaded engine build");
    const auto* nt = module + 0x150;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint32_t>(nt + 8) == 0x64EE7E86 &&
                read<std::uint32_t>(nt + 80) == 0x39C0000 &&
                read<std::uint32_t>(nt + 88) == 0x64174E,
            "Unsupported focus overlay engine build");
    auto* engine_vgui = data.native_cell<void*>(0x4346712C);
    require(in_region(module, engine_vgui, 0x48, 0x528000, 0x3427638),
            "VEngineVGui001 is outside its actual module data");
    auto** table = *static_cast<void***>(engine_vgui);
    require(in_region(module, table, 40 * sizeof(void*), 0x453000, 0xD4766),
            "VEngineVGui001 vtable is outside engine RTTI data");
    const auto* method = static_cast<const std::byte*>(table[39]);
    require(in_region(module, method, 0x13E, 0x1000, 0x451C32),
            "VEngineVGui001 focus field use is outside executable engine text");
    const auto* code = method + 0x134;
    static constexpr unsigned char field_use[]{0x8B, 0x4B, 0x44, 0x8B, 0x01, 0xFF,
                                               0x90, 0xE8, 0,    0,    0};
    require(!std::memcmp(code, field_use, sizeof field_use),
            "VEngineVGui001 does not contain the proven focus panel field use");
    auto* receiver = read<void*>(static_cast<const std::byte*>(engine_vgui) +
                                 static_cast<unsigned char>(code[2]));
    require(receiver != nullptr, "The engine focus overlay panel has not been created");
    auto** current = *static_cast<void***>(receiver);
    require(current && descriptor.original_table &&
                (current == descriptor.original_table || current == descriptor.replacement_table),
            "Engine focus receiver does not match the canonical CFocusOverlayPanel descriptor");
    return receiver;
}
void bind_focus_overlay_first_entry(VtableHookMetadata& descriptor) noexcept {
    descriptor.replacement_table[131] = reinterpret_cast<void*>(&first_entry);
}

FocusOverlayHooks::FocusOverlayHooks(client::CrtRuntimeOwner& crt,
                                     client::PostInitializationOwner& post,
                                     ui::StartupRenderer& renderer,
                                     client::VisualRuntimeOwner& visual)
    : crt_(crt), storage_(crt.storage()), post_(post), renderer_(renderer), visual_(visual),
      scripts_(post.menu().script_modules()), metadata_(crt.descriptor_storage().at(0x434757C8)),
      unload_(scripts_.unload_state()), accent_(accent(storage_)) {
    auto& data = storage_.data();
    if (&post.data() != &data || &visual.storage() != &storage_ ||
        &visual.startup_renderer() != &renderer ||
        &renderer.dpi() != &storage_.menu_startup_data().dpi ||
        data.native_cell<ui::RendererContainer*>(0x4347690C) != &renderer.renderer() ||
        data.native_cell<ui::RootMenu*>(0x43476924) != &post.menu())
        throw std::invalid_argument(
            "Focus hooks require the canonical source menu, renderer and startup data");
    if (active && active != this)
        throw std::logic_error("A canonical focus hook owner already exists");
    active = this;
    // Static native producer:434675D0=4341F57F;43467F78=43466D60.
    // Loader extra() and the mapped working dump prove that43466D60's time
    // guard branches unconditionally to ordinary4338A69A. Bind those actual
    // source operations, without copying either native executable address.
    data.native_cell<SourceEntry>(0x434675D0) = &initialize_entry;
    data.native_cell<SourceEntry>(0x43467F78) = &draw_entry;
}
FocusOverlayHooks::~FocusOverlayHooks() {
    restore();
    if (active == this) {
        auto& before = storage_.data().native_cell<SourceEntry>(0x434675D0);
        auto& after = storage_.data().native_cell<SourceEntry>(0x43467F78);
        if (before == &initialize_entry)
            before = nullptr;
        if (after == &draw_entry)
            after = nullptr;
        active = nullptr;
    }
}
void FocusOverlayHooks::prepare() {
    if (!metadata_.replacement_table) {
        auto* module = storage_.data().native_cell<std::byte*>(0x43468D20);
        crt_.descriptors().prepare(
            metadata_, resolve_rtti_vtable(module, 0xBA6192DD, 0, crt_.descriptors().images()));
    }
    require(metadata_.replacement_table && metadata_.original_table,
            "Focus descriptor preparation failed");
    bind_focus_overlay_first_entry(metadata_);
}
void FocusOverlayHooks::install() {
    if (installed_)
        throw std::logic_error("The focus overlay hook is already installed");
    prepare();
    auto* receiver = find_build_focus_overlay_receiver(storage_.data(), metadata_);
    metadata_.target = receiver;
    unload_.first_patch = {static_cast<void**>(receiver), metadata_.original_table,
                           reinterpret_cast<std::uintptr_t>(metadata_.replacement_table)};
    std::atomic_ref<void**>(*static_cast<void***>(receiver)).exchange(metadata_.replacement_table);
    installed_ = true;
}
void FocusOverlayHooks::restore() {
    if (!installed_)
        return;
    installed_ = false;
    auto& patch = unload_.first_patch;
    if (!patch.enabled)
        return;
    // Normal unload restores the canonical patch before CRT releases its
    // cloned table. The engine receiver can retire before this outer owner.
    auto** replacement = metadata_.replacement_table;
    if (!replacement)
        return;
    if (patch.target != metadata_.target || patch.saved != metadata_.original_table ||
        patch.enabled != reinterpret_cast<std::uintptr_t>(replacement))
        return;
    auto* expected = static_cast<void*>(replacement);
    std::atomic_ref<void*>(*patch.target).compare_exchange_strong(expected, patch.saved);
    patch.enabled = 0;
}
void dispatch_focus_first_invocation(NativeDataImage& data, void* receiver) {
    auto& first = data.native_cell<SourceEntry>(0x434675D0);
    const auto before = first;
    first = nullptr;
    auto& second = data.native_cell<SourceEntry>(0x43467F78);
    const auto after = second;
    second = nullptr;
    before(receiver, nullptr);
    //43416A0F rereads the receiver after post-initialization may run callbacks.
    auto& current = installed_descriptor(receiver);
    std::atomic_ref<void*>(current.replacement_table[131]).exchange(reinterpret_cast<void*>(after));
    after(receiver, nullptr);
}
void FocusOverlayHooks::first_invocation(void* receiver) {
    dispatch_focus_first_invocation(storage_.data(), receiver);
}
void FocusOverlayHooks::initialize() {
    post_.runtime().initialize();
}
void FocusOverlayHooks::draw(void* receiver) {
    auto& data = storage_.data();
    // Source HUD bindings borrow semantic screen/icon views. Do not touch a
    // released renderer during the native blocked/null-context rejection.
    if (!data.native_cell<std::int32_t>(0x4347C9FC) &&
        data.native_cell<ui::RendererContainer*>(0x4347690C) &&
        renderer_.renderer().backend_if_alive())
        visual_.refresh();
    const hud::CompositeFrameContext context{data.native_cell<ui::RendererContainer*>(0x4347690C),
                                             data.native_cell<std::int32_t>(0x4347C9FC),
                                             renderer_.dpi().active_scale,
                                             data.native_cell<void*>(0x4346A514),
                                             data.native_cell<void*>(0x4346A890),
                                             storage_.owner_registries().at(0x43475A9C),
                                             scripts_.bridge(),
                                             data.native_cell<ui::MenuWindow*>(0x4347691C),
                                             accent_};
    (void)hud::draw_composite_frame(context);
    reinterpret_cast<MemberFunction<void>>(installed_descriptor(receiver).original_table[131])(
        receiver);
}
}
#undef SKEET_FOCUS_ABI
