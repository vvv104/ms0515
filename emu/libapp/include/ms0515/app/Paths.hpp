/*
 * Paths.hpp — host filesystem helpers, no SDL.
 *
 * Both ms0515.exe and ms0515-cli.exe locate their YAML config, ROM,
 * disks, and screenshots the same way: relative to the running .exe.
 * Platform-specific exeDir() implementations live in Paths.cpp behind
 * #ifdefs (GetModuleFileName / readlink / _NSGetExecutablePath).
 */

#ifndef MS0515_APP_PATHS_HPP
#define MS0515_APP_PATHS_HPP

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::app {

class Paths {
public:
    /* Directory containing the running executable, ending with the
     * platform's path separator.  Falls back to "./" if the OS query
     * fails. */
    static std::string exeDir();

    /* Locations searched for bundled resources (ROM, disks…).  Order
     * matters — earlier entries win.  Currently exeDir then cwd. */
    static std::vector<std::filesystem::path> searchRoots();

    /* "<exeDir>/<prefix>_<YYYY-MM-DD_HHMMSS><ext>" — for screenshot /
     * state-save default file names. */
    static std::string timestamped(std::string_view prefix,
                                   std::string_view ext);

    /* Decimal, "0x..." hex, or "0o..." octal integer parser; returns 0
     * on failure. */
    static int parseNumber(const std::string &s);

    /* Search assets/rom/<filename> through searchRoots() and return the
     * first existing match (normalised, absolute).  Empty string if
     * nothing found.  Used by both binaries to pick a default ROM when
     * neither the CLI args nor the YAML config specified one. */
    static std::string findAssetRom(const std::string &filename);
};

} /* namespace ms0515::app */

#endif /* MS0515_APP_PATHS_HPP */
