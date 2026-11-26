#include "skeet/client/game_window_proxy_factory.hpp"
#include "skeet/client/native_startup.hpp"
#include <stdexcept>

namespace skeet::client {
namespace {
void create(void* opaque) {
    create_game_window_proxy_owner(*static_cast<GameWindowProxyFactoryBindings*>(opaque));
}
}
engine::GameWindowProxyOwner*
create_game_window_proxy_owner(GameWindowProxyFactoryBindings& bindings) {
    return engine::create_game_window_proxy_owner(
        {{bindings.registries.at(0x43475B78u), bindings.memory},
         {bindings.registries.at(0x43475770u), bindings.memory}},
        bindings.proxy);
}
void bind_game_window_proxy_factory(NativeStartupSources& sources,
                                    GameWindowProxyFactoryBindings& bindings) {
    auto& data = engine::require_native_data_image(bindings.data);
    if (&data != &sources.data() || &data != &bindings.registries.image())
        throw std::invalid_argument(
            "Game-window proxy requires the scheduler's canonical native data and registries");
    if (&bindings.proxy.guarded_return_site != &data.native_cell<const std::byte*>(0x43467288u) ||
        &bindings.proxy.game_interface_slot != &data.native_cell<void**>(0x43468B2Cu) ||
        &bindings.proxy.original_rtti != &data.native_cell<std::uintptr_t>(0x43469288u))
        throw std::invalid_argument(
            "Game-window proxy requires its canonical live engine binding cells");
    if (!bindings.memory.allocate || !bindings.memory.free)
        throw std::invalid_argument(
            "Game-window proxy requires its genuine process-heap allocation imports");
    if (!bindings.proxy.imports.crc32 || !bindings.proxy.imports.exchange)
        throw std::invalid_argument(
            "Game-window proxy requires genuine CRC and atomic exchange imports");
    sources.bind_initializer({game_window_proxy_factory, &bindings, create});
}
} // namespace skeet::client
