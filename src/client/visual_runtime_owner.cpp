#include "skeet/client/visual_runtime_owner.hpp"
#include "skeet/client/runtime_composition.hpp"
#include "skeet/client/runtime_storage.hpp"
#include "skeet/client/world_runtime_factories.hpp"
#include "skeet/animation/native_trigonometry.hpp"
#include "skeet/engine/netvars.hpp"
#include "skeet/engine/pe_exports.hpp"
#include "skeet/render/renderer_assets.hpp"
#include "skeet/render/startup_renderer.hpp"
#include <array>
#include <cstring>
#include <stdexcept>

namespace skeet::client {
namespace {
template <class T> T read(const std::byte* bytes) {
    T value;
    std::memcpy(&value, bytes, sizeof value);
    return value;
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::invalid_argument(message);
}
bool contains(const std::byte* module, const void* pointer, std::size_t size, std::uint32_t rva,
              std::uint32_t length) {
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto first = reinterpret_cast<std::uintptr_t>(module) + rva;
    return address >= first && size <= length && address - first <= length - size;
}
void require_interface(const ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                       std::uint32_t cell) {
    const auto manifest = engine::build_interface_manifest();
    for (std::size_t index = 0; index < manifest.size(); ++index) {
        if (manifest[index].destination_cell != cell)
            continue;
        auto* object = storage.data().native_cell<void*>(cell);
        require(object && object == engine.interfaces.objects[index],
                "Visual runtime requires interfaces from the canonical engine bundle");
        return;
    }
    throw std::logic_error("Visual interface is absent from the build acquisition manifest");
}
void* acquire_key_values(const EngineRuntimeBindings& engine) {
    auto* module = static_cast<std::byte*>(engine.modules.at(u"vstdlib.dll"));
    require(module && read<std::uint16_t>(module) == 0x5A4D &&
                read<std::uint32_t>(module + 0x3C) == 0x108,
            "Projectile materials require the loaded vstdlib build");
    const auto* nt = module + 0x108;
    require(read<std::uint32_t>(nt) == 0x4550 && read<std::uint16_t>(nt + 4) == 0x14C &&
                read<std::uint16_t>(nt + 6) == 5 && read<std::uint16_t>(nt + 0x18) == 0x10B &&
                read<std::uint32_t>(nt + 8) == 0x63DC10BF &&
                read<std::uint32_t>(nt + 80) == 0x44000 && read<std::uint32_t>(nt + 88) == 0x4B1B5,
            "Unsupported projectile material vstdlib build");
    const auto* sections = nt + 24 + read<std::uint16_t>(nt + 20);
    constexpr std::array<std::array<std::uint32_t, 3>, 3> regions{{{0x1000, 0x277BA, 0x60000020},
                                                                   {0x29000, 0xF6F6, 0x40000040},
                                                                   {0x39000, 0x6B34, 0xC0000040}}};
    for (std::size_t index = 0; index < regions.size(); ++index) {
        const auto* section = sections + index * 40;
        require(read<std::uint32_t>(section + 12) == regions[index][0] &&
                    read<std::uint32_t>(section + 8) == regions[index][1] &&
                    read<std::uint32_t>(section + 36) == regions[index][2],
                "KeyValuesSystem module sections differ from the supported build");
    }
    // The PE export, not an old loader RVA, identifies this genuine getter.
    // Its complete build body is MOV EAX, singleton; RET. Reading the operand
    // has the same result and no skipped initializer or external side effect.
    const auto* getter =
        engine::resolve_pe_export(module, engine::netvar_name_hash("KeyValuesSystem"), 0, true);
    require(getter && contains(module, getter, 6, 0x1000, 0x277BA) &&
                getter[0] == std::byte{0xB8} && getter[5] == std::byte{0xC3},
            "KeyValuesSystem does not contain the proven singleton getter");
    auto* singleton = read<void*>(getter + 1);
    require(contains(module, singleton, 4, 0x39000, 0x6B34),
            "KeyValuesSystem singleton is outside the real module data");
    auto* table = read<const std::byte*>(static_cast<const std::byte*>(singleton));
    require(contains(module, table, 11 * 4, 0x29000, 0xF6F6),
            "KeyValuesSystem vtable is outside the real module read-only data");
    for (std::size_t slot = 0; slot < 11; ++slot)
        require(contains(module, read<const void*>(table + slot * 4), 1, 0x1000, 0x277BA),
                "KeyValuesSystem method is outside the real module executable section");
    return singleton;
}
void* checked_key_values(ClientRuntimeStorage& storage, const EngineRuntimeBindings& engine,
                         ConfigurationRuntimeOwner& configuration, ui::StartupRenderer& renderer,
                         ui::RendererAssets& assets) {
    require(
        &engine.variables.data() == &storage.data() && &configuration.storage() == &storage &&
            &assets.storage() == &storage && &assets.startup_renderer() == &renderer &&
            &renderer.dpi() == &storage.menu_startup_data().dpi &&
            storage.data().native_cell<ui::RendererContainer*>(0x4347690C) == &renderer.renderer(),
        "Visual runtime must borrow one canonical storage, configuration, renderer and asset owner");
    for (const auto cell : {0x4346A514u, 0x43467F68u, 0x4346A890u, 0x4346A4E0u})
        require_interface(storage, engine, cell);
    require(storage.data().native_cell<void*>(0x4346A624) != nullptr,
            "Visual runtime requires the acquired genuine CCSViewRender inline object");
    require(storage.offsets().words && !storage.offsets().globals.pointers.empty(),
            "Projectile material bindings require the initialized canonical entity offsets");
    require(storage.data().native_cell<void*>(0x43468A7C) == engine.modules.at(u"vstdlib.dll"),
            "Projectile material singleton must belong to the canonical loaded vstdlib module");
    return acquire_key_values(engine);
}
esp::Projection initial_projection(engine::NativeDataImage& data) {
    // The native scales surround the matrix; Projection's semantic C++ layout
    // differs. Never reinterpret that whole BSS region as this source object.
    return {data.native_cell<std::array<float, 16>>(0x434798E0),
            data.native_cell<float>(0x43479920), data.native_cell<float>(0x434798DC)};
}
}
VisualRuntimeOwner::VisualRuntimeOwner(ClientRuntimeStorage& storage,
                                       const EngineRuntimeBindings& engine,
                                       ConfigurationRuntimeOwner& configuration,
                                       ui::StartupRenderer& renderer, ui::RendererAssets& assets)
    : storage_(storage), configuration_(configuration), renderer_(renderer), assets_(assets),
      key_values_(checked_key_values(storage, engine, configuration, renderer, assets)),
      ribbons_{storage.data().native_cell<void*>(0x4346A890),
               storage.data().native_cell<void*>(0x4346A4E0), key_values_,
               effects::ProjectileRibbonOffsets{storage.offsets().globals}},
      projection_(initial_projection(storage.data())) {
    refresh();
}
VisualRuntimeOwner::~VisualRuntimeOwner() = default;
ui::RendererBackend& VisualRuntimeOwner::renderer() const {
    auto* backend = renderer_.renderer().backend_if_alive();
    if (!backend)
        throw std::logic_error("The canonical visual renderer has released its context");
    return *backend;
}
engine::GameSystemCameraState VisualRuntimeOwner::camera_state() noexcept {
    auto& data = storage_.data();
    return {data.native_cell<std::array<float, 3>>(0x43479CCC),
            data.native_cell<std::array<float, 3>>(0x43479CD8), data.native_cell<float>(0x43468E80),
            data.native_cell<float>(0x4346D548), projection_};
}
void VisualRuntimeOwner::refresh() {
    auto& backend = renderer();
    screen_size_ = {backend.screen_width, backend.screen_height};
    indicators_.scale = renderer_.dpi().active_scale;
    indicators_.screen_height = backend.screen_height;
    indicators_.texture_source = &backend;
    const auto& icons = assets_.startup_icons();
    explosive_.texture = static_cast<std::uint32_t>(icons.grenade);
    backend.texture_size(explosive_.texture, &explosive_.width, &explosive_.height);
    fire_.texture = static_cast<std::uint32_t>(icons.inferno);
    backend.texture_size(fire_.texture, &fire_.width, &fire_.height);
}
void VisualRuntimeOwner::initialize_game_system(ShutdownRuntime& shutdown) {
    if (game_system_)
        throw std::logic_error("The canonical game-system runtime has already been composed");
    require(
        &shutdown.input == &storage_.input_runtime() &&
            &shutdown.exit_callbacks == &storage_.exit_callbacks() &&
            &shutdown.owners == &storage_.owner_registries().at(0x43475B78),
        "Game-system runtime requires shutdown over the canonical input, exits and owner registry");
    auto& data = storage_.data();
    game_system_ = std::make_unique<engine::GameSystemRuntime>(
        shutdown, storage_.owner_registries().at(0x43475988),
        storage_.owner_registries().at(0x43475AD8), data.native_cell<std::int32_t>(0x4346DB2C),
        data.native_cell<std::int32_t>(0x43476904),
        engine::GameSystemBindings{
            data.native_cell<void*>(0x4346A514), data.native_cell<void*>(0x4346A624),
            data.native_cell<void*>(0x43467F68), configuration_.miscellaneous().disable_pure,
            animation::native_sse_tangent, animation::native_sse_arctangent},
        camera_state());
}
engine::GameSystemRuntime& VisualRuntimeOwner::game_system() {
    if (!game_system_)
        throw std::logic_error(
            "Actual shutdown must exist before composing the game-system runtime");
    return *game_system_;
}
GameSystemFactoryContext VisualRuntimeOwner::game_system_factory() {
    return {storage_.data().native_cell<engine::GameSystemManager*>(0x43467FB0),
            &storage_.data().native_cell<void*>(0x43467204), game_system()};
}
} // namespace skeet::client
