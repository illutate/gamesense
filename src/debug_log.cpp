#include "skeet/debug_log.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace skeet::debug {
namespace {

SRWLOCK log_lock = SRWLOCK_INIT;
HANDLE log_file = INVALID_HANDLE_VALUE;
bool log_closed = false;
wchar_t log_path[MAX_PATH]{};

void build_log_path(void* module_handle) noexcept {
    log_path[0] = L'\0';
    HMODULE module = static_cast<HMODULE>(module_handle);
    if (!module) {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&build_log_path), &module);
    }
    wchar_t buffer[MAX_PATH]{};
    DWORD len = module ? GetModuleFileNameW(module, buffer, MAX_PATH) : 0;
    if (len > 0 && len < MAX_PATH) {
        wchar_t* slash = wcsrchr(buffer, L'\\');
        wchar_t* fwd = wcsrchr(buffer, L'/');
        wchar_t* sep = (slash && fwd) ? (slash > fwd ? slash : fwd) : (slash ? slash : fwd);
        if (sep) {
            *(sep + 1) = L'\0';
            wcsncpy_s(log_path, MAX_PATH, buffer, _TRUNCATE);
        }
    }
    if (!log_path[0]) {
        DWORD cwd_len = GetCurrentDirectoryW(MAX_PATH, buffer);
        if (cwd_len > 0 && cwd_len + 20 < MAX_PATH) {
            wcsncpy_s(log_path, MAX_PATH, buffer, _TRUNCATE);
            const size_t n = wcslen(log_path);
            if (n > 0 && log_path[n - 1] != L'\\' && log_path[n - 1] != L'/')
                wcsncat_s(log_path, MAX_PATH, L"\\", _TRUNCATE);
        }
    }
    wcsncat_s(log_path, MAX_PATH, L"russiansense.log", _TRUNCATE);
}

void write_raw(const char* bytes, std::size_t length) noexcept {
    if (log_file == INVALID_HANDLE_VALUE || !bytes || !length)
        return;
    DWORD written = 0;
    WriteFile(log_file, bytes, static_cast<DWORD>(length), &written, nullptr);
}

void ensure_open_locked() noexcept {
    if (log_closed || log_file != INVALID_HANDLE_VALUE)
        return;
    if (!log_path[0])
        build_log_path(nullptr);
    log_file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void format_timestamp(char* out, std::size_t capacity) noexcept {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::snprintf(out, capacity, "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

} // namespace

void initialize(void* module_handle) noexcept {
    AcquireSRWLockExclusive(&log_lock);
    log_closed = false;
    if (log_file != INVALID_HANDLE_VALUE) {
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
    build_log_path(module_handle);
    log_file = CreateFileW(log_path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file != INVALID_HANDLE_VALUE) {
        char ts[32]{};
        format_timestamp(ts, sizeof(ts));
        char path_utf8[MAX_PATH * 3]{};
        WideCharToMultiByte(CP_UTF8, 0, log_path, -1, path_utf8, sizeof(path_utf8), nullptr,
                            nullptr);
        char line[1200]{};
        const int n = std::snprintf(
            line, sizeof(line),
            "[%s] [tid=%lu] [DEBUG] [INIT] === russiansense session started (pid=%lu, log=%s) ===\r\n",
            ts, static_cast<unsigned long>(GetCurrentThreadId()),
            static_cast<unsigned long>(GetCurrentProcessId()), path_utf8);
        if (n > 0)
            write_raw(line, static_cast<std::size_t>(std::min<int>(n, sizeof(line) - 1)));
    }
    ReleaseSRWLockExclusive(&log_lock);
}

void shutdown() noexcept {
    AcquireSRWLockExclusive(&log_lock);
    if (log_file != INVALID_HANDLE_VALUE) {
        char ts[32]{};
        format_timestamp(ts, sizeof(ts));
        char line[256]{};
        const int n = std::snprintf(
            line, sizeof(line),
            "[%s] [tid=%lu] [DEBUG] [SHUTDOWN] === russiansense session ended ===\r\n", ts,
            static_cast<unsigned long>(GetCurrentThreadId()));
        if (n > 0)
            write_raw(line, static_cast<std::size_t>(std::min<int>(n, sizeof(line) - 1)));
        CloseHandle(log_file);
        log_file = INVALID_HANDLE_VALUE;
    }
    log_closed = true;
    ReleaseSRWLockExclusive(&log_lock);
}

bool active() noexcept {
    AcquireSRWLockShared(&log_lock);
    const bool open = (log_file != INVALID_HANDLE_VALUE);
    ReleaseSRWLockShared(&log_lock);
    return open;
}

void write(const char* category, const char* format, ...) noexcept {
    std::va_list args;
    va_start(args, format);
    vwrite(category, format, args);
    va_end(args);
}

void vwrite(const char* category, const char* format, std::va_list args) noexcept {
    if (!format)
        return;
    std::array<char, 2048> body{};
    const int body_len = std::vsnprintf(body.data(), body.size(), format, args);
    if (body_len < 0)
        return;
    const std::size_t clipped =
        static_cast<std::size_t>(std::min<int>(body_len, static_cast<int>(body.size() - 1)));
    body[clipped] = '\0';

    char ts[32]{};
    format_timestamp(ts, sizeof(ts));
    std::array<char, 2240> line{};
    const int total = std::snprintf(
        line.data(), line.size(), "[%s] [tid=%lu] [DEBUG] [%s] %s\r\n", ts,
        static_cast<unsigned long>(GetCurrentThreadId()), category ? category : "CORE",
        body.data());
    if (total <= 0)
        return;

    AcquireSRWLockExclusive(&log_lock);
    ensure_open_locked();
    write_raw(line.data(),
              static_cast<std::size_t>(std::min<int>(total, static_cast<int>(line.size() - 1))));
    ReleaseSRWLockExclusive(&log_lock);
}

void write_u16(const char* category, const char* action, std::u16string_view text) noexcept {
    std::array<char, 512> utf8{};
    if (!text.empty()) {
        const int converted = WideCharToMultiByte(
            CP_UTF8, 0, reinterpret_cast<const wchar_t*>(text.data()),
            static_cast<int>(text.size()), utf8.data(), static_cast<int>(utf8.size() - 1),
            nullptr, nullptr);
        if (converted > 0)
            utf8[static_cast<std::size_t>(converted)] = '\0';
        else
            utf8[0] = '\0';
    }
    write(category, "%s: \"%s\"", action ? action : "item", utf8.data());
}

} // namespace skeet::debug
