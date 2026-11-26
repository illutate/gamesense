#pragma once
#include <cstdarg>
#include <string_view>

namespace skeet::debug {

void initialize(void* module_handle) noexcept;
void shutdown() noexcept;
bool active() noexcept;

void write(const char* category, const char* format, ...) noexcept;
void vwrite(const char* category, const char* format, std::va_list args) noexcept;
void write_u16(const char* category, const char* action, std::u16string_view text) noexcept;

} // namespace skeet::debug

#define DEBUG_LOG(category, format, ...) ::skeet::debug::write((category), (format), ##__VA_ARGS__)
#define DEBUG_LOG_U16(category, action, text) ::skeet::debug::write_u16((category), (action), (text))
