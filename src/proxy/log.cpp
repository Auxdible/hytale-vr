#include "log.h"

#include <windows.h>

#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hvr {
namespace {

std::wstring g_client_dir, g_data_dir, g_log_dir;
std::mutex g_log_mutex;
FILE* g_log = nullptr;

}  // namespace

void paths_init(void* module) {
    wchar_t path[MAX_PATH * 2] = {};
    GetModuleFileNameW(static_cast<HMODULE>(module), path, MAX_PATH * 2);
    std::wstring dir = path;
    const auto slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir.resize(slash);
    g_client_dir = dir;
    g_data_dir = dir + L"\\HytaleVR";
    g_log_dir = g_data_dir + L"\\logs";
}

const std::wstring& client_dir() { return g_client_dir; }
const std::wstring& data_dir() { return g_data_dir; }
const std::wstring& log_dir() { return g_log_dir; }

bool ensure_dir(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

void log_init() {
    std::lock_guard lock(g_log_mutex);
    if (g_log) return;
    ensure_dir(g_data_dir);
    ensure_dir(g_log_dir);
    g_log = _wfsopen((g_log_dir + L"\\hytalevr.log").c_str(), L"w", _SH_DENYWR);
}

void logf(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    SYSTEMTIME now;
    GetLocalTime(&now);
    std::lock_guard lock(g_log_mutex);
    if (!g_log) return;
    std::fprintf(g_log, "%02d:%02d:%02d.%03d  %s\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, line);
    std::fflush(g_log);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

}  // namespace hvr
