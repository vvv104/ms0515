/*
 * Config.hpp — emulator frontend config + path helpers.
 *
 * Persistent settings (disk/ROM paths, UI toggles, keyboard typematic
 * presets, history-watch settings, fullscreen) live in `ms0515.yaml`
 * next to the executable.  This module owns the YAML schema and all
 * filesystem-anchor helpers — anything that needs to find or write a
 * file under the exe folder.
 */
#pragma once

#include "Platform.hpp"   /* FileDialogKind */

#include <string>
#include <string_view>

namespace ms0515_frontend {

/* Map a (drive, side) pair to the core FDC's logical-unit index.
 * Hardware mapping: FD0 = drive 0 side 0, FD1 = drive 1 side 0,
 * FD2 = drive 0 side 1, FD3 = drive 1 side 1.  See core/floppy.c. */
constexpr int fdcUnitFor(int drive, int side) noexcept
{
    return drive + side * 2;
}

class Config {
public:
    std::string fdPath[4];
    std::string dsPath[2];     /* double-sided per drive */
    std::string romPath;
    bool showKeyboard = false;
    bool showDebugger = false;
    bool hostMode     = false;
    /* Size of the binary event history ring (in 16-byte events).  0
     * disables recording entirely (zero runtime cost).  When > 0, the
     * last N I/O events are kept in RAM and serialised into the HIST
     * chunk on snapshot save — turn this on when diagnosing a hang. */
    int         historySize  = 0;
    /* Memory-write watchpoint — when watchLen > 0, each byte/word write
     * to [watchAddr, watchAddr+watchLen) is recorded as an MEMW event
     * in the history ring (requires historySize > 0 to be useful). */
    int         historyWatchAddr = 0;
    int         historyWatchLen  = 0;
    /* Same, but for reads (MEMR events). */
    int         historyReadWatchAddr = 0;
    int         historyReadWatchLen  = 0;
    /* When true, the frontend writes a timestamped snapshot every time
     * the CPU spontaneously re-enters POST (fetch at 0172000 after the
     * initial cold boot).  Useful for catching rare reboots while
     * playing — by the time the user notices, the snapshot is already
     * on disk with the event ring intact. */
    bool        autoSnapOnReset = false;
    /* Keyboard typematic settings.  -1 means "use core default", which
     * lets us add new presets without baking them into existing config
     * files.  Auto-game-mode is the heuristic toggle. */
    int         kbdTypingDelayMs  = -1;
    int         kbdTypingPeriodMs = -1;
    int         kbdGameDelayMs    = -1;
    int         kbdGamePeriodMs   = -1;
    int         kbdAutoGameMode   = -1;     /* -1 unset, 0 off, 1 on */
    bool        fullscreen        = false;

    bool isDefault() const;

    /* Forgiving YAML loader.  Missing file, malformed lines, unknown
     * keys and bad numeric values are all non-fatal — defaults
     * stand in. */
    static Config load();

    /* Persist the config.  Writes only non-default values, deletes the
     * file entirely if the config has reverted to all defaults. */
    void save() const;

    /* Path to the config file (`ms0515.yaml` next to the .exe). */
    static std::string path();
};

/* ── Filesystem-anchor helpers ──────────────────────────────────────── */

class Paths {
public:
    /* Directory containing the executable, with trailing separator. */
    static std::string exeDir();

    /* Starting folder for a file dialog of the given kind: the bundled
     * assets folder next to the executable.  Subsequent dialogs land
     * wherever the user was last (handled by the OS shell). */
    static std::string initialDirFor(FileDialogKind kind);

    /* Build a path next to the executable with a timestamped filename:
     *   <exeDir>/<prefix>_YYYY-MM-DD_HHMMSS<ext> */
    static std::string timestamped(std::string_view prefix,
                                    std::string_view ext);

    /* Parse a numeric string accepting decimal, 0x-hex, and 0o-octal
     * (Python-style).  Returns 0 on malformed input. */
    static int parseNumber(const std::string &s);
};

} /* namespace ms0515_frontend */
