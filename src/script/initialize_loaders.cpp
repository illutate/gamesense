#include "skeet/script/initialize.hpp"
#include "initialize_detail.hpp"
#include "detail.hpp"
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace skeet::script {
namespace {
bool has_local_override(std::string_view name) {
    std::string path = "gamesense\\";
    path.append(name);
    for (char& ch : path)
        if (ch == '/')
            ch = '\\';
    path.append(".lua");
    const DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
} // namespace

void ScriptRuntime::install_loaders() { // 433B6C6E
    if (required_modules.empty())
        return;
    const auto& lua = *bridge.api;
    auto* state = bridge.state;
    lua.getfield(state, -10002, "table");
    lua.getfield(state, -1, "insert");
    lua.remove(state, -2);
    lua.getfield(state, -10002, "package");
    lua.getfield(state, -1, "loaders");
    lua.remove(state, -2);
    lua.pushinteger(state, 1);
    lua.pushcclosure(state, loader, 0);
    if (lua.pcall(state, 3, 0, 0))
        detail::report_lua_error(bridge);
}
int ScriptRuntime::loader(void* state) { // 43331646
    auto* runtime = initialize_detail::owner(state);
    if (!runtime)
        throw std::logic_error("Lua loader has no native script owner");
    auto& bridge = runtime->bridge;
    const auto& lua = *bridge.api;
    if (bridge.runtime_ref == -1 || runtime->required_modules.empty() || lua.type(state, 1) != 4)
        return 0;
    std::size_t length = 0;
    const auto* requested = lua.tolstring(state, 1, &length);
    if (!requested || length <= 11)
        return 0;
    auto hash = 0x811c9dc5u;
    for (std::size_t i = 0; i < 10; ++i) {
        auto byte = static_cast<unsigned char>(requested[i]);
        if (byte >= 'A' && byte <= 'Z')
            byte = static_cast<unsigned char>(byte + 32);
        else if (i == 9 && byte == '\\')
            byte = '/';
        hash = (hash ^ byte) * 0x01000193u;
    }
    if ((hash ^ 0x92ca5df1u) != 0xf817bfb5u)
        return 0;
    for (auto& module : runtime->required_modules) {
        // Native774FB200 is msvcrt!_stricmp; suffix case is ignored too.
        if (module.name.size() != length - 10 || _stricmp(module.name.c_str(), requested + 10))
            continue;
        if (!module.encrypted)
            throw std::logic_error("Required module source is not encrypted in native storage");
        lua.rawgeti(state, -10000, bridge.runtime_ref);
        lua.rawgeti(state, -1, 5);
        lua.remove(state, -2);
        lua.pushlstring(state, module.name.data(), module.name.size());
        runtime->cipher.transform(module.source);
        module.encrypted = false;
        lua.pushlstring(state, reinterpret_cast<const char*>(module.source.data()),
                        module.source.size());
        runtime->cipher.transform(module.source);
        module.encrypted = true;
        runtime->loading_builtin = true;
        const auto status = lua.pcall(state, 2, 1, 0);
        runtime->loading_builtin = false;
        if (status) {
            if (has_local_override(module.name))
                lua.settop(state, -2);
            else
                detail::report_lua_error(bridge, state);
            lua.pushnil(state); // 43433656, native returns one nil after logging.
        }
        return 1;
    }
    return 0;
}
} // namespace skeet::script
