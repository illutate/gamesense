#include "skeet/menu/startup_window_name.hpp"
#include <algorithm>
#include <stdexcept>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace skeet::ui {
std::u16string_view MainMenuRegistryName::view() const {
    const auto end = std::find(text.begin(), text.end(), char16_t{});
    if (end == text.end())
        throw std::logic_error("Main menu registry name has no native terminator");
    return {text.data(), std::size_t(end - text.begin())};
}
MainMenuCpuLeaf1 read_main_menu_cpu_leaf1() {
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
    int words[4];
    __cpuidex(words, 1, 0);
    return {static_cast<std::uint32_t>(words[0]), static_cast<std::uint32_t>(words[1]),
            static_cast<std::uint32_t>(words[2]), static_cast<std::uint32_t>(words[3])};
#elif defined(__i386__) || defined(__x86_64__)
    MainMenuCpuLeaf1 result;
    __cpuid_count(1, 0, result.eax, result.ebx, result.ecx, result.edx);
    return result;
#else
    throw std::logic_error("Main menu registry naming requires the original x86 CPUID input");
#endif
}
MainMenuRegistryName make_main_menu_registry_name(MainMenuCpuLeaf1 cpu) noexcept {
    // The final startup CPUID at434AF5E2 feeds the unsigned value formatted
    // by434C7805 into native menu+E0. The addition wraps before the shift.
    auto value = main_menu_registry_value(cpu);
    std::array<char16_t, 10> reversed;
    std::size_t count = 0;
    do {
        reversed[count++] = char16_t(u'0' + value % 10);
        value /= 10;
    } while (value);
    MainMenuRegistryName result;
    for (std::size_t index = 0; index < count; ++index)
        result.text[index] = reversed[count - index - 1];
    return result;
}
MainMenuRegistryName make_main_menu_registry_name() {
    return make_main_menu_registry_name(read_main_menu_cpu_leaf1());
}
} //namespace skeet::ui
