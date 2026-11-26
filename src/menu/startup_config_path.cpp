#include "skeet/menu/startup_config_path.hpp"
#include "skeet/menu/startup_window_name.hpp"
#include "skeet/config_storage/preserved.hpp"
#include "skeet/script/database.hpp"
#include <array>
#include <stdexcept>
#include <string>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winternl.h>

extern "C" ULONG NTAPI RtlGetCurrentDirectory_U(ULONG, PWSTR);
extern "C" BOOLEAN NTAPI RtlDoesFileExists_U(PCWSTR);

namespace skeet::ui {
MainMenuConfigPathInitialization initialize_main_menu_config_path(PreservedConfigStorage& storage) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    static_assert(sizeof(NativeConfigPath) == sizeof(UNICODE_STRING));
    auto& output = storage.path();
    if (output.length || output.maximum_length || output.buffer)
        throw std::logic_error("Main menu configuration path is already initialized");
    const auto cpu = read_main_menu_cpu_leaf1();
    const auto key = script::database_path_key(cpu.eax);
    // The actual startup4349CE62 call requests104h bytes into a cleared208h
    // buffer. Its return value is ignored before constructing the filename.
    std::array<wchar_t, 260> directory{};
    (void)RtlGetCurrentDirectory_U(0x104, directory.data());
    std::wstring path(directory.data());
    path += L"\\csgo\\cfg\\";
    path += std::to_wstring(key);
    path += L"9.cfg";
    const bool existed = RtlDoesFileExists_U(path.c_str()) != 0;
    UNICODE_STRING name{};
    const bool converted = RtlDosPathNameToNtPathName_U(path.c_str(), &name, nullptr, nullptr) != 0;
    // Transfer only the real API result, including its exact failure output.
    // The source's existing canonical-path finalizer retains sole ownership.
    output = {name.Length, name.MaximumLength, reinterpret_cast<char16_t*>(name.Buffer)};
    return {cpu.eax, key, existed, converted};
}
}
