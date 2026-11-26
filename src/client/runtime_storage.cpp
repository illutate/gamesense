#include "skeet/client/runtime_storage.hpp"
#include "skeet/engine/source_data_initialization.hpp"
#include "skeet/engine/owner_allocation.hpp"
#include <stdexcept>
#include <utility>

namespace skeet::client {
namespace {
std::shared_ptr<engine::NativeDataImage>
require_data(std::shared_ptr<engine::NativeDataImage> data) {
    if (!data)
        throw std::invalid_argument("Client runtime storage requires one canonical data image");
    return data;
}
std::shared_ptr<engine::NativeDataImage> create_runtime_data() {
    auto data = engine::NativeDataImage::create_source();
    engine::initialize_source_native_data(*data);
    return data;
}
}

ClientRuntimeStorage::ClientRuntimeStorage() : ClientRuntimeStorage(create_runtime_data()) {}

ClientRuntimeStorage::ClientRuntimeStorage(std::shared_ptr<engine::NativeDataImage> data)
    : data_(require_data(std::move(data))), memory_(engine::native_job_memory()),
      configuration_(std::make_shared<PreservedConfigStorage>(data_)), settings_menu_(data_),
      menu_data_(data_), input_(data_->native_cell<std::int32_t>(0x4347C9FCu)),
      owner_registries_(data_),
      commands_(data_, memory_, {data_->native_cell<std::uint32_t>(0x434759C0u), input_.bitmap()}),
      empty_ticks_(data_, memory_, commands_.services().input), entities_(data_, memory_),
      predictions_(data_, memory_), data_updates_(data_, memory_), recharge_(data_, memory_),
      offsets_(data_->native_cell<std::uint32_t*>(0x434797B0u)), event_names_(data_),
      events_(event_names_, data_->native_cell<void*>(0x4346A514u),
              data_->native_cell<void*>(0x43468364u)),
      exits_{data_->native_cell<std::array<std::uintptr_t, 512>>(0x43479CF0u),
             data_->native_cell<std::uintptr_t>(0x4347CA08u),
             data_->native_cell<std::uint32_t>(0x4347CA0Cu)} {}

ClientRuntimeStorage::~ClientRuntimeStorage() {
    if (engine_bindings_started_ && offsets_.words) {
        engine::release_owner_storage(memory_, offsets_.words);
        offsets_.words = nullptr;
    }
}

RegisteredHotkey& ClientRuntimeStorage::menu_hotkey() const {
    return data_->native_cell<RegisteredHotkey>(0x43478DB8u);
}
engine::CoreEngineInterfaces
ClientRuntimeStorage::bind_core_interfaces(engine::FactoryProvider& provider) {
    const auto interfaces = engine::discover_core_interfaces(
        provider, {data_->native_cell<void*>(0x4346A514u), data_->native_cell<void*>(0x4346A890u),
                   data_->native_cell<void*>(0x434683E0u), data_->native_cell<void*>(0x43469264u),
                   data_->native_cell<void*>(0x434689C0u), data_->native_cell<void*>(0x4346A87Cu),
                   data_->native_cell<void*>(0x43468304u)});
    input_.interfaces.engine_client = interfaces.engine_client;
    return interfaces;
}
engine::CoreEngineInterfaces ClientRuntimeStorage::bind_core_interfaces() {
    engine::LoadedModuleFactories provider;
    return bind_core_interfaces(provider);
}
void ClientRuntimeStorage::bind_input_interfaces() {
    const input::EngineInterfaces bindings{
        data_->native_cell<void*>(0x4346A514u), data_->native_cell<void*>(0x43468994u),
        data_->native_cell<engine::MemberFunction<void*, const char*>>(0x4346A374u),
        data_->native_cell<void*>(0x43468D24u), data_->native_cell<void*>(0x4346A89Cu)};
    if (!bindings.engine_client || !bindings.hud || !bindings.find_hud_element ||
        !bindings.cursor_contexts || !bindings.input_system)
        throw std::logic_error(
            "Menu input requires the discovered engine, HUD, input stack and input system");
    // The original cursor helpers read these canonical cells on every call.
    // Publish the same objects to the source runtime before the window hook.
    input_.interfaces = bindings;
}
} //namespace skeet::client
