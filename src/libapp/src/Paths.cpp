/*
 * Paths.cpp — host filesystem helpers (Windows + POSIX).
 */

#include "ms0515/app/Paths.hpp"

#include <cstdio>
#include <ctime>
#include <system_error>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

namespace ms0515::app {

namespace {

std::string queryExePath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    int u8len = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                                    nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return {};
    std::string out(static_cast<size_t>(u8len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(n),
                        out.data(), u8len, nullptr, nullptr);
    return out;
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return {};
    return std::string(buf);
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    return std::string(buf, static_cast<size_t>(n));
#endif
}

}  // namespace

std::string Paths::exeDir()
{
    std::string path = queryExePath();
    if (path.empty()) return "./";
    /* Trim back to last path separator (Windows accepts both / and \). */
    auto slash = path.find_last_of("\\/");
    if (slash == std::string::npos) return "./";
    return path.substr(0, slash + 1);
}

std::vector<std::filesystem::path> Paths::searchRoots()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> roots;
    roots.emplace_back(exeDir());
    roots.emplace_back(fs::current_path(ec));
    return roots;
}

std::string Paths::timestamped(std::string_view prefix, std::string_view ext)
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm);
    return exeDir() + std::string{prefix} + "_" + buf + std::string{ext};
}

std::string Paths::findAssetRom(const std::string &filename)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto &root : searchRoots()) {
        fs::path candidate = root / "assets" / "rom" / filename;
        if (fs::exists(candidate, ec))
            return candidate.lexically_normal().string();
    }
    return {};
}

int Paths::parseNumber(const std::string &s)
{
    if (s.empty()) return 0;
    try {
        if (s.rfind("0o", 0) == 0 || s.rfind("0O", 0) == 0)
            return std::stoi(s.substr(2), nullptr, 8);
        if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0)
            return std::stoi(s.substr(2), nullptr, 16);
        return std::stoi(s);
    } catch (...) { return 0; }
}

} /* namespace ms0515::app */
