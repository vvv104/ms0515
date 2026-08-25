/*
 * Config.hpp — emulator config (YAML, persistent).
 *
 * Both ms0515.exe and ms0515-cli.exe load the same `ms0515.yaml` next
 * to the binary, so any disk / ROM choice the user made in the GUI is
 * automatically picked up by the CLI on next launch (and vice versa).
 *
 * CLI-only builds simply ignore the UI-specific knobs (showKeyboard,
 * fullscreen, screenshotPath…); they're kept here so the YAML schema
 * stays a single source of truth.
 */

#ifndef MS0515_APP_CONFIG_HPP
#define MS0515_APP_CONFIG_HPP

#include <string>

namespace ms0515::app {

/* (drive, side) → core FDC unit index.  Hardware mapping:
 *   FD0 = drive 0 side 0, FD1 = drive 1 side 0,
 *   FD2 = drive 0 side 1, FD3 = drive 1 side 1. */
constexpr int fdcUnitFor(int drive, int side) noexcept
{
    return drive + side * 2;
}

class Config {
public:
    std::string fdPath[4];          /* single-side image per FDC unit */
    std::string dsPath[2];          /* double-sided per drive */
    bool        hdEnabled = false;  /* HD: controller present (yaml: "hd_enabled") */
    std::string hdPath;             /* HD: image / media (yaml: "hd") */
    std::string romPath;            /* ROM image (yaml: "rom") */
    bool showKeyboard = false;
    bool showDebugger = false;
    bool hostMode     = false;
    int  historySize  = 0;
    int  historyWatchAddr     = 0;
    int  historyWatchLen      = 0;
    int  historyReadWatchAddr = 0;
    int  historyReadWatchLen  = 0;
    int  kbdTypingDelayMs     = -1;
    int  kbdTypingPeriodMs    = -1;
    int  kbdGameDelayMs       = -1;
    int  kbdGamePeriodMs      = -1;
    int  kbdAutoGameMode      = -1;
    bool fullscreen           = false;
    std::string joystick;           /* "" (off) | "keys" | "gamepad" (yaml: "joystick") */

    [[nodiscard]] bool isDefault() const;

    /* Tolerant YAML loader — missing file, malformed lines, unknown
     * keys all silently fall back to defaults.  Forward/backward
     * compatibility across versions. */
    static Config load();

    /* Persist the config.  Writes only non-default fields; deletes the
     * file outright if the config matches all defaults. */
    void save() const;

    /* "<exeDir>/ms0515.yaml" — full path to the on-disk config. */
    static std::string path();
};

/* The canonical "which ROM file should we open?" decision.  Order:
 *   1. `cliRomPath` (from the command line, --rom <path>)
 *   2. `cfgRomPath` (from ms0515.yaml's "rom:" entry)
 *   3. Paths::findAssetRom("ms0515-roma.rom") — the default that ships
 *      next to either binary.
 * Returns an empty string only when none of the three resolve to an
 * existing file. */
std::string resolveRom(const std::string &cliRomPath,
                       const std::string &cfgRomPath);

} /* namespace ms0515::app */

#endif /* MS0515_APP_CONFIG_HPP */
