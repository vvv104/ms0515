#define _CRT_SECURE_NO_WARNINGS
/*
 * main.cpp — MS0515 emulator frontend (SDL2 + Dear ImGui).
 *
 * Runs the core emulator in a 50/60/72 Hz frame loop and presents the
 * decoded framebuffer through an SDL_Renderer texture.  A Dear ImGui
 * debugger overlay provides register/disassembly/breakpoint views and
 * run/step controls.
 *
 * CLI:
 *   ms0515 [--rom <path>]
 *          [--disk0 <path>] | [--disk0-side0 <path>] [--disk0-side1 <path>]
 *          [--disk1 <path>] | [--disk1-side0 <path>] [--disk1-side1 <path>]
 *          (short aliases: -d0/-d0s0/-d0s1, -d1/-d1s0/-d1s1)
 *          [--screen-dump stderr|stdout|<path>]
 *          [--history-size N]               (events; 0 disables)
 *          [--history-watch-addr A] [--history-watch-len L]  (MEMW evts)
 *          [--history-read-watch-addr A] [--history-read-watch-len L]
 *
 * Disk-mount options come in two flavours:
 *   - Single-side mounts: `--diskN-sideM` (-dNsM) — one 409600-byte
 *     image per physical side.  The core driver calls these logical
 *     units FD0..FD3, mapped via bits 1:0 of System Register A:
 *
 *         --disk0-side0  ↔  drive 0, lower head (= core FD0)
 *         --disk0-side1  ↔  drive 0, upper head (= core FD2)
 *         --disk1-side0  ↔  drive 1, lower head (= core FD1)
 *         --disk1-side1  ↔  drive 1, upper head (= core FD3)
 *
 *   - Double-sided mount: `--diskN` (-dN) — one 819200-byte image
 *     in track-interleaved layout (T0S0, T0S1, T1S0, T1S1, ...) —
 *     this is what raw MS0515 hardware dumps look like.
 *     fdc_attach() detects the image size and gives the upper-side
 *     unit an in-track offset so reads and writes for either side
 *     land in the right half of each track slot.  `--diskN` and
 *     `--diskN-sideM` for the same N are mutually exclusive.
 *
 * Defaults: looks for assets/rom/ms0515-roma.rom (the patched ROM-A,
 * relative to either the executable directory or the current working
 * directory) when --rom is not given.  Other ROMs need explicit --rom.
 */

#include <SDL.h>
#include <imgui.h>
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <ms0515/Emulator.hpp>
#include <ms0515/Debugger.hpp>
#include <ms0515/Disassembler.hpp>
#include <ms0515/board.h>

#include "Audio.hpp"
#include "OnScreenKeyboard.hpp"
#include "PhysicalKeyboard.hpp"
#include "Platform.hpp"
#include <ms0515/ScreenReader.hpp>
#include "Video.hpp"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

/* Map a (drive, side) pair to the core FDC's logical-unit index.
 * Hardware mapping: FD0 = drive 0 side 0, FD1 = drive 1 side 0,
 * FD2 = drive 0 side 1, FD3 = drive 1 side 1.  See core/floppy.c. */
constexpr int fdcUnitFor(int drive, int side) noexcept
{
    return drive + side * 2;
}

struct CliArgs {
    std::string romPath;
    /* fdPath[unit] — one single-side image per core FDC unit
     * (FD0..FD3 indexing).  Frontend names these by (drive, side) —
     * see fdcUnitFor(). */
    std::string fdPath[4];
    /* dsPath[drive] — one double-sided image covering both sides of a
     * drive.  Mutually exclusive with fdPath[fdcUnitFor(drive, 0|1)]. */
    std::string dsPath[2];
    std::string screenDumpPath; /* --screen-dump: VRAM text output */
    std::string screenshotPath;
    int         maxFrames = 0;      /* 0 = run forever */
    int         screenshotFrame = 0; /* frame number to take screenshot */
    int         historySize = -1;   /* -1 = take from config, 0 = disabled */
    int         historyWatchAddr = -1;
    int         historyWatchLen  = -1;
    int         historyReadWatchAddr = -1;
    int         historyReadWatchLen  = -1;
};

/* ── Config file (YAML) ─────────────────────────────────────────────── */

/* Returns the directory containing the executable (with trailing separator). */
std::string getExeDir()
{
    if (char *base = SDL_GetBasePath()) {
        std::string dir(base);
        SDL_free(base);
        return dir;
    }
    return {};
}

std::string configPath()
{
    return getExeDir() + "ms0515.yaml";
}

/* Minimal YAML parser — only handles top-level "key: value" lines. */
struct Config {
    std::string fdPath[4];
    std::string dsPath[2];     /* double-sided per drive */
    std::string romPath;
    bool showKeyboard = false;
    bool showDebugger = false;
    bool hostMode     = false;
    /* Last directory used for each kind of file dialog — remembered
     * across sessions so the user doesn't repeatedly navigate to the
     * same folder. */
    std::string lastDirDisk;
    std::string lastDirRom;
    std::string lastDirState;
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

    bool isDefault() const {
        for (int i = 0; i < 4; ++i)
            if (!fdPath[i].empty()) return false;
        for (int i = 0; i < 2; ++i)
            if (!dsPath[i].empty()) return false;
        if (!romPath.empty() || showKeyboard || showDebugger || hostMode)
            return false;
        if (!lastDirDisk.empty() || !lastDirRom.empty() ||
            !lastDirState.empty())
            return false;
        if (historySize != 0) return false;
        if (historyWatchAddr != 0 || historyWatchLen != 0) return false;
        if (historyReadWatchAddr != 0 || historyReadWatchLen != 0)
            return false;
        if (autoSnapOnReset) return false;
        return true;
    }
};

/* Parse a numeric config value accepting decimal, 0x-hex, and 0o-octal
 * (Python-style).  Returns 0 on malformed input. */
static int parseNumber(const std::string &s)
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

Config loadConfig()
{
    Config cfg;
    std::ifstream f(configPath());
    if (!f) return cfg;

    std::string line;
    while (std::getline(f, line)) {
        /* Skip comments and empty lines. */
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        /* Trim leading/trailing whitespace from value. */
        auto vb = val.find_first_not_of(" \t\"");
        auto ve = val.find_last_not_of(" \t\r\n\"");
        if (vb == std::string::npos) val.clear();
        else val = val.substr(vb, ve - vb + 1);

        if      (key == "disk0")       cfg.dsPath[0] = val;
        else if (key == "disk1")       cfg.dsPath[1] = val;
        else if (key == "disk0_side0") cfg.fdPath[fdcUnitFor(0, 0)] = val;
        else if (key == "disk0_side1") cfg.fdPath[fdcUnitFor(0, 1)] = val;
        else if (key == "disk1_side0") cfg.fdPath[fdcUnitFor(1, 0)] = val;
        else if (key == "disk1_side1") cfg.fdPath[fdcUnitFor(1, 1)] = val;
        /* Quiet migration of legacy fd0..fd3 keys.  Older configs
         * continue to load; the next saveConfig() rewrites them in
         * the new disk{N}_side{M} form. */
        else if (key == "fd0") cfg.fdPath[fdcUnitFor(0, 0)] = val;
        else if (key == "fd1") cfg.fdPath[fdcUnitFor(1, 0)] = val;
        else if (key == "fd2") cfg.fdPath[fdcUnitFor(0, 1)] = val;
        else if (key == "fd3") cfg.fdPath[fdcUnitFor(1, 1)] = val;
        else if (key == "rom") cfg.romPath = val;
        else if (key == "show_keyboard") cfg.showKeyboard = (val == "true");
        else if (key == "show_debugger") cfg.showDebugger = (val == "true");
        else if (key == "host_mode")     cfg.hostMode     = (val == "true");
        else if (key == "last_dir_disk")  cfg.lastDirDisk  = val;
        else if (key == "last_dir_rom")   cfg.lastDirRom   = val;
        else if (key == "last_dir_state") cfg.lastDirState = val;
        else if (key == "history_size") {
            try { cfg.historySize = std::stoi(val); }
            catch (...) { cfg.historySize = 0; }
            if (cfg.historySize < 0) cfg.historySize = 0;
        }
        else if (key == "history_watch_addr") {
            cfg.historyWatchAddr = parseNumber(val);
        }
        else if (key == "history_watch_len") {
            cfg.historyWatchLen = parseNumber(val);
        }
        else if (key == "history_read_watch_addr") {
            cfg.historyReadWatchAddr = parseNumber(val);
        }
        else if (key == "history_read_watch_len") {
            cfg.historyReadWatchLen = parseNumber(val);
        }
        else if (key == "auto_snap_on_reset") {
            cfg.autoSnapOnReset = (val == "true");
        }
    }
    return cfg;
}

void saveConfig(const Config &cfg)
{
    std::string path = configPath();
    if (cfg.isDefault()) {
        /* No non-default values — remove config file if it exists. */
        std::filesystem::remove(path);
        return;
    }
    std::ofstream f(path);
    if (!f) return;
    f << "# MS0515 emulator configuration\n";
    /* Double-sided drives take precedence — when set, the side-N
     * fields below are not also written for the same drive. */
    static constexpr const char *kDsKeys[] = {"disk0", "disk1"};
    for (int drive = 0; drive < 2; ++drive) {
        if (!cfg.dsPath[drive].empty())
            f << kDsKeys[drive] << ": \"" << cfg.dsPath[drive] << "\"\n";
    }
    static constexpr struct { int drive, side; const char *name; } kSlots[] = {
        {0, 0, "disk0_side0"}, {0, 1, "disk0_side1"},
        {1, 0, "disk1_side0"}, {1, 1, "disk1_side1"},
    };
    for (const auto &s : kSlots) {
        if (!cfg.dsPath[s.drive].empty()) continue;  /* DS owns this drive */
        const auto &p = cfg.fdPath[fdcUnitFor(s.drive, s.side)];
        if (!p.empty())
            f << s.name << ": \"" << p << "\"\n";
    }
    if (!cfg.romPath.empty())
        f << "rom: \"" << cfg.romPath << "\"\n";
    if (cfg.showKeyboard) f << "show_keyboard: true\n";
    if (cfg.showDebugger) f << "show_debugger: true\n";
    if (cfg.hostMode)     f << "host_mode: true\n";
    if (!cfg.lastDirDisk.empty())
        f << "last_dir_disk: \""  << cfg.lastDirDisk  << "\"\n";
    if (!cfg.lastDirRom.empty())
        f << "last_dir_rom: \""   << cfg.lastDirRom   << "\"\n";
    if (!cfg.lastDirState.empty())
        f << "last_dir_state: \"" << cfg.lastDirState << "\"\n";
    if (cfg.historySize != 0)
        f << "history_size: " << cfg.historySize << "\n";
    if (cfg.historyWatchAddr != 0 || cfg.historyWatchLen != 0) {
        f << "history_watch_addr: 0o" << std::oct << cfg.historyWatchAddr
          << std::dec << "\n";
        f << "history_watch_len: "    << cfg.historyWatchLen << "\n";
    }
    if (cfg.historyReadWatchAddr != 0 || cfg.historyReadWatchLen != 0) {
        f << "history_read_watch_addr: 0o" << std::oct
          << cfg.historyReadWatchAddr << std::dec << "\n";
        f << "history_read_watch_len: "   << cfg.historyReadWatchLen << "\n";
    }
    if (cfg.autoSnapOnReset) f << "auto_snap_on_reset: true\n";
}

/* Starting folder for a file dialog of the given kind: the remembered
 * last-used dir if present, otherwise a sensible default next to the
 * executable (assets/disks for Disk, assets/rom for Rom, exe root for
 * State). */
std::string initialDirFor(ms0515_frontend::FileDialogKind kind,
                          const Config &cfg)
{
    using K = ms0515_frontend::FileDialogKind;
    const std::string &last =
        kind == K::Disk  ? cfg.lastDirDisk  :
        kind == K::Rom   ? cfg.lastDirRom   : cfg.lastDirState;
    if (!last.empty()) return last;
    std::string base = getExeDir();           /* has trailing separator */
    switch (kind) {
    case K::Disk:  return base + "assets/disks";
    case K::Rom:   return base + "assets/rom";
    case K::State: return base;
    }
    return base;
}

/* Update the cached last-used dir for `kind` to the parent folder of
 * `chosenPath` and persist the Config.  Called after every successful
 * file-dialog selection. */
void rememberDirFor(Config &cfg, ms0515_frontend::FileDialogKind kind,
                    const std::string &chosenPath)
{
    std::string dir = std::filesystem::path(chosenPath).parent_path().string();
    if (dir.empty()) return;
    using K = ms0515_frontend::FileDialogKind;
    switch (kind) {
    case K::Disk:  cfg.lastDirDisk  = dir; break;
    case K::Rom:   cfg.lastDirRom   = dir; break;
    case K::State: cfg.lastDirState = dir; break;
    }
    saveConfig(cfg);
}

/* Build a path next to the executable with a timestamped filename:
 *   <exeDir>/<prefix>_YYYY-MM-DD_HHMMSS<ext>
 * Used by the screenshot tool and the auto-snapshot-on-reset feature. */
std::string timestampedPath(std::string_view prefix, std::string_view ext)
{
    std::time_t t = std::time(nullptr);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", &tm);
    return getExeDir() + std::string{prefix} + "_" + buf + std::string{ext};
}

/* Save a screenshot of the emulator framebuffer as PNG.
 * Returns the file path on success, empty string on failure. */
std::string saveScreenshot(const ms0515_frontend::Video &video,
                           const std::string &path)
{
    std::string outPath = path;
    if (outPath.empty()) {
        /* Auto-generate filename with timestamp in the exe directory. */
        std::time_t t = std::time(nullptr);
        std::tm tm;
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), "ms0515_%Y-%m-%d_%H%M%S.png", &tm);
        outPath = getExeDir() + buf;
    }
    int rc = stbi_write_png(outPath.c_str(),
                            ms0515_frontend::kScreenWidth,
                            ms0515_frontend::kScreenHeight,
                            4,  /* RGBA */
                            video.pixels(),
                            ms0515_frontend::kScreenWidth * 4);
    if (!rc)
        return {};
    std::fprintf(stderr, "Screenshot saved: %s\n", outPath.c_str());
    return outPath;
}

/* Search a handful of likely locations for the default ROM.  The working
 * directory may be anywhere (the user double-clicked the .exe, the IDE
 * spawned it from build/Release/frontend, etc.), so we try several
 * candidates relative to the executable's own path.  Returns the empty
 * string if nothing is found. */
std::string findDefaultRom()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    /* 1. The SDL-supplied base path, i.e. the directory containing the
     *    executable.  This is the most reliable anchor on Windows. */
    std::vector<fs::path> roots;
    if (char *base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    /* 2. Current working directory. */
    roots.emplace_back(fs::current_path(ec));

    const char *rels[] = {
        "assets/rom/ms0515-roma.rom",
    };

    for (const auto &root : roots) {
        for (const char *rel : rels) {
            fs::path candidate = root / rel;
            if (fs::exists(candidate, ec)) {
                return candidate.lexically_normal().string();
            }
        }
    }
    return {};
}

/* Quick disk-image format gate.  Each --diskN-sideM CLI option
 * targets one physical side and must be a 409600-byte SS image;
 * each --diskN option targets a whole drive and must be a 819200-
 * byte DS image.  Files of other sizes are rejected before
 * fdc_attach() opens them — so unknown formats fail early with a
 * clear stderr message rather than getting silently mounted as
 * garbage.  Returns std::nullopt on success, or a human-readable
 * reason on failure. */
static std::optional<std::string>
validateSingleSideImage(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return std::format("cannot stat '{}': {}", path, ec.message());
    if (sz == 409600)
        return std::nullopt;
    if (sz == 819200)
        return std::format(
            "'{}' is a double-sided image (819200 bytes).  Use "
            "--diskN (or -dN) to mount a whole double-sided drive "
            "from one image.",
            path);
    return std::format(
        "'{}' has unrecognised disk format (size {} bytes; expected 409600 "
        "for a single-side image).",
        path, static_cast<unsigned long long>(sz));
}

static std::optional<std::string>
validateDoubleSidedImage(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (ec)
        return std::format("cannot stat '{}': {}", path, ec.message());
    if (sz == 819200)
        return std::nullopt;
    if (sz == 409600)
        return std::format(
            "'{}' is a single-side image (409600 bytes).  Use "
            "--diskN-side0 (or -dNs0) to mount it on one side of "
            "a drive.",
            path);
    return std::format(
        "'{}' has unrecognised disk format (size {} bytes; expected 819200 "
        "for a double-sided image).",
        path, static_cast<unsigned long long>(sz));
}


/* Discover all .rom files in assets/rom/ directories relative to the
 * executable and the current working directory. */
std::vector<std::string> discoverRoms()
{
    namespace fs = std::filesystem;
    std::error_code ec;

    std::vector<fs::path> roots;
    if (char *base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    roots.emplace_back(fs::current_path(ec));

    std::vector<std::string> result;
    for (const auto &root : roots) {
        fs::path romDir = root / "assets" / "rom";
        if (!fs::is_directory(romDir, ec)) continue;
        for (const auto &entry : fs::directory_iterator(romDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != ".rom") continue;
            std::string path = entry.path().lexically_normal().string();
            /* Avoid duplicates when exe dir == cwd. */
            if (std::find(result.begin(), result.end(), path) == result.end())
                result.push_back(path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

/* Single-side disk-mount option table.  Each entry binds a long form
 * (`--diskN-sideM`) and a short alias (`-dNsM`) to a (drive, side)
 * pair. */
struct DiskOption {
    const char *longForm;
    const char *shortForm;
    int         drive;
    int         side;
};
static constexpr DiskOption kDiskOptions[] = {
    {"--disk0-side0", "-d0s0", 0, 0},
    {"--disk0-side1", "-d0s1", 0, 1},
    {"--disk1-side0", "-d1s0", 1, 0},
    {"--disk1-side1", "-d1s1", 1, 1},
};

/* Double-sided disk-mount option table.  Each entry binds a long
 * form (`--diskN`) and a short alias (`-dN`) to a drive index. */
struct DoubleSidedOption {
    const char *longForm;
    const char *shortForm;
    int         drive;
};
static constexpr DoubleSidedOption kDoubleSidedOptions[] = {
    {"--disk0", "-d0", 0},
    {"--disk1", "-d1", 1},
};

/* Detects the legacy CLI options we removed (--fd0..fd3, --disk,
 * --drive) and emits a friendly migration message naming the
 * replacement.  Returns `true` if `arg` was a legacy option (the
 * caller should also skip its accompanying value, if any). */
static bool reportRetiredArg(const std::string &arg)
{
    if (arg == "--fd0" || arg == "--fd1" ||
        arg == "--fd2" || arg == "--fd3") {
        std::fprintf(stderr,
            "error: '%s' was removed.  Use the disk/side names instead:\n"
            "  --fd0 → --disk0-side0   (-d0s0)\n"
            "  --fd1 → --disk1-side0   (-d1s0)\n"
            "  --fd2 → --disk0-side1   (-d0s1)\n"
            "  --fd3 → --disk1-side1   (-d1s1)\n",
            arg.c_str());
        return true;
    }
    if (arg == "--disk" || arg == "--drive") {
        std::fprintf(stderr,
            "error: '%s' was removed.  Use --diskN-sideM (or -dNsM) "
            "to mount one side of a drive.\n",
            arg.c_str());
        return true;
    }
    return false;
}

CliArgs parseArgs(int argc, char **argv)
{
    CliArgs out;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--rom" && i + 1 < argc) {
            out.romPath = argv[++i];
        } else if (reportRetiredArg(a)) {
            /* Skip the path value too if there is one. */
            if (i + 1 < argc) ++i;
        } else if (auto *opt = std::find_if(
                       std::begin(kDiskOptions), std::end(kDiskOptions),
                       [&](const DiskOption &o) {
                           return a == o.longForm || a == o.shortForm;
                       });
                   opt != std::end(kDiskOptions) && i + 1 < argc) {
            out.fdPath[fdcUnitFor(opt->drive, opt->side)] = argv[++i];
        } else if (auto *dsOpt = std::find_if(
                       std::begin(kDoubleSidedOptions),
                       std::end(kDoubleSidedOptions),
                       [&](const DoubleSidedOption &o) {
                           return a == o.longForm || a == o.shortForm;
                       });
                   dsOpt != std::end(kDoubleSidedOptions) && i + 1 < argc) {
            out.dsPath[dsOpt->drive] = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            out.maxFrames = std::atoi(argv[++i]);
        } else if (a == "--screen-dump" && i + 1 < argc) {
            out.screenDumpPath = argv[++i];
        } else if (a == "--screenshot" && i + 1 < argc) {
            out.screenshotPath = argv[++i];
        } else if (a == "--screenshot-frame" && i + 1 < argc) {
            out.screenshotFrame = std::atoi(argv[++i]);
        } else if (a == "--history-size" && i + 1 < argc) {
            out.historySize = std::max(0, std::atoi(argv[++i]));
        } else if (a == "--history-watch-addr" && i + 1 < argc) {
            out.historyWatchAddr = parseNumber(argv[++i]);
        } else if (a == "--history-watch-len" && i + 1 < argc) {
            out.historyWatchLen  = parseNumber(argv[++i]);
        } else if (a == "--history-read-watch-addr" && i + 1 < argc) {
            out.historyReadWatchAddr = parseNumber(argv[++i]);
        } else if (a == "--history-read-watch-len" && i + 1 < argc) {
            out.historyReadWatchLen  = parseNumber(argv[++i]);
        } else {
            std::fprintf(stderr, "warning: unknown argument '%s'\n", a.c_str());
        }
    }
    /* If a screenshot frame is set but no explicit --frames, auto-stop
     * after the screenshot so headless runs don't hang. */
    if (out.screenshotFrame > 0 && out.maxFrames <= 0) {
        out.maxFrames = out.screenshotFrame;
    }
    return out;
}


/* Render the debugger ImGui window.  Drives run/step through the
 * supplied Debugger and displays live CPU state. */
void drawDebuggerWindow(ms0515::Debugger &dbg,
                        bool &running,
                        const std::string &romStatus,
                        bool &open)
{
    if (!ImGui::Begin("Debugger", &open)) {
        ImGui::End();
        return;
    }

    /* Status */
    ImGui::TextUnformatted(romStatus.c_str());
    const auto &cpu = dbg.emulator().cpu();
    ImGui::Text("CPU: PC=%06o  %s",
                cpu.r[7],
                cpu.halted  ? "HALTED" :
                cpu.waiting ? "WAIT"   : "running");
    ImGui::Separator();

    /* Execution controls */
    if (ImGui::Button(running ? "Pause" : "Run")) {
        running = !running;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step") && !running) {
        dbg.stepInstruction();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step Over") && !running) {
        dbg.stepOver();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        dbg.reset();
    }

    ImGui::Separator();

    /* Registers */
    ImGui::TextUnformatted(dbg.formatRegisters().c_str());

    ImGui::Separator();

    /* Disassembly around PC */
    if (ImGui::CollapsingHeader("Disassembly", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto lines = dbg.disassembleAtPc(16);
        for (const auto &ins : lines) {
            auto line = std::format("{:06o}: {}", ins.address, ins.text());
            ImGui::TextUnformatted(line.c_str());
        }
    }

    /* Breakpoint editor */
    if (ImGui::CollapsingHeader("Breakpoints")) {
        static char  bpBuf[16] = "";
        ImGui::InputText("addr (octal)", bpBuf, sizeof bpBuf,
                         ImGuiInputTextFlags_CharsHexadecimal |
                         ImGuiInputTextFlags_AutoSelectAll);
        ImGui::SameLine();
        if (ImGui::Button("Add")) {
            unsigned val = 0;
            auto [ptr, ec] = std::from_chars(bpBuf, bpBuf + std::strlen(bpBuf),
                                             val, 8);
            if (ec == std::errc{}) {
                dbg.addBreakpoint(static_cast<uint16_t>(val));
            }
        }
        std::vector<uint16_t> toRemove;
        for (uint16_t addr : dbg.breakpoints()) {
            auto label = std::format("x##{:06o}", addr);
            if (ImGui::SmallButton(label.c_str())) toRemove.push_back(addr);
            ImGui::SameLine();
            auto addrStr = std::format("{:06o}", addr);
            ImGui::TextUnformatted(addrStr.c_str());
        }
        for (uint16_t addr : toRemove) dbg.removeBreakpoint(addr);
    }

    ImGui::End();
}

} /* anonymous namespace */

int main(int argc, char **argv)
{
    CliArgs cli = parseArgs(argc, argv);
    ms0515_frontend::platformInit();

    /* ── SDL init ───────────────────────────────────────────────────────── */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Load config now that SDL is up (so SDL_GetBasePath works). */
    Config config = loadConfig();

    /* CLI args override config values. */
    for (int i = 0; i < 4; ++i) {
        if (cli.fdPath[i].empty() && !config.fdPath[i].empty())
            cli.fdPath[i] = config.fdPath[i];
    }
    for (int i = 0; i < 2; ++i) {
        if (cli.dsPath[i].empty() && !config.dsPath[i].empty())
            cli.dsPath[i] = config.dsPath[i];
    }

    /* Resolve the ROM path: CLI > config > auto-detect. */
    if (cli.romPath.empty() && !config.romPath.empty())
        cli.romPath = config.romPath;
    if (cli.romPath.empty())
        cli.romPath = findDefaultRom();

    const int scale            = 1;
    const int winWidthNoDbg    = ms0515_frontend::kScreenWidth  * scale + 20;
    const int winHeight        = ms0515_frontend::kScreenHeight * scale + 100;
    const int winWidth         = winWidthNoDbg;

    SDL_Window *window = SDL_CreateWindow(
        "MS 0515 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winWidth, winHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(
        window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *frameTex = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING,
        ms0515_frontend::kScreenWidth,
        ms0515_frontend::kScreenHeight);

    /* ── ImGui init ─────────────────────────────────────────────────────── */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    /* Load a font with Cyrillic glyphs so the on-screen keyboard labels
     * and any Russian text in UI can render.  Default ImGui font is
     * ASCII-only.  Font paths come from the platform layer. */
    {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;
        static const ImWchar rangesMain[] = {
            0x0020, 0x00FF,   /* Latin + Latin-1 (incl. ¤ ¬) */
            0x0400, 0x04FF,   /* Cyrillic */
            0x2190, 0x2199,   /* Arrows ← ↑ → ↓ ↖ ↗ ↘ ↙ */
            0,
        };
        for (const auto &path : ms0515_frontend::systemFontCandidates()) {
            if (std::filesystem::exists(path)) {
                io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f,
                                             &cfg, rangesMain);
                break;
            }
        }
        /* Symbol fallback for arrow glyphs (incl. diagonal ↖ ↗ ↘ ↙)
         * which some primary fonts omit.  Merge into the primary font. */
        ImFontConfig sym;
        sym.MergeMode   = true;
        sym.OversampleH = 2;
        sym.OversampleV = 2;
        static const ImWchar rangesSym[] = {
            0x2190, 0x2199,
            0,
        };
        for (const auto &path : ms0515_frontend::symbolFontCandidates()) {
            if (std::filesystem::exists(path)) {
                io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f,
                                             &sym, rangesSym);
                break;
            }
        }
    }


    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    /* ── Emulator + debugger ───────────────────────────────────────────── */
    ms0515::Emulator emu;
    std::string      currentRomPath = cli.romPath;
    std::string      romStatus;
    auto availableRoms = discoverRoms();

    if (currentRomPath.empty()) {
        romStatus = "ROM: <not found - pass --rom <path>>";
        std::fprintf(stderr, "%s\n", romStatus.c_str());
    } else if (!emu.loadRomFile(currentRomPath)) {
        romStatus = "ROM: FAILED to load '" + currentRomPath + "'";
        std::fprintf(stderr, "%s\n", romStatus.c_str());
    } else {
        romStatus = "ROM: " + currentRomPath;
    }
    for (int drive = 0; drive < 2; ++drive) {
        const bool wantDs = !cli.dsPath[drive].empty();
        const bool wantSide0 = !cli.fdPath[fdcUnitFor(drive, 0)].empty();
        const bool wantSide1 = !cli.fdPath[fdcUnitFor(drive, 1)].empty();

        if (wantDs && (wantSide0 || wantSide1)) {
            std::fprintf(stderr,
                "error: --disk%d (-d%d) is mutually exclusive with "
                "--disk%d-sideN (-d%dsN); pick one.  Skipping drive %d.\n",
                drive, drive, drive, drive, drive);
            continue;
        }

        if (wantDs) {
            if (auto err = validateDoubleSidedImage(cli.dsPath[drive])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d (double-sided): %s\n",
                    drive, err->c_str());
                continue;
            }
            int unit0 = fdcUnitFor(drive, 0);
            int unit1 = fdcUnitFor(drive, 1);
            bool ok0 = emu.mountDisk(unit0, cli.dsPath[drive]);
            bool ok1 = emu.mountDisk(unit1, cli.dsPath[drive]);
            if (ok0 && ok1) {
                config.dsPath[drive] = cli.dsPath[drive];
                /* DS-mount owns both sides — drop any stale per-side
                 * config so the next save reflects reality. */
                config.fdPath[unit0].clear();
                config.fdPath[unit1].clear();
            } else {
                std::fprintf(stderr,
                    "error: failed to mount double-sided image '%s' "
                    "on drive %d\n",
                    cli.dsPath[drive].c_str(), drive);
                if (ok0) emu.unmountDisk(unit0);
                if (ok1) emu.unmountDisk(unit1);
            }
            continue;
        }

        for (int side = 0; side < 2; ++side) {
            int unit = fdcUnitFor(drive, side);
            if (cli.fdPath[unit].empty()) continue;
            if (auto err = validateSingleSideImage(cli.fdPath[unit])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d side %d: %s\n",
                    drive, side, err->c_str());
                continue;
            }
            if (emu.mountDisk(unit, cli.fdPath[unit])) {
                config.fdPath[unit] = cli.fdPath[unit];
            } else {
                std::fprintf(stderr,
                    "error: failed to mount '%s' on disk %d side %d\n",
                    cli.fdPath[unit].c_str(), drive, side);
            }
        }
    }
    saveConfig(config);
    /* Enable the 512 KB RAM disk expansion board (EX0:). */
    emu.enableRamDisk();

    /* Binary event history: CLI --history-size overrides the yaml
     * history_size setting.  Default (both unset) keeps it off. */
    int histSize = cli.historySize >= 0 ? cli.historySize : config.historySize;
    if (histSize > 0)
        emu.enableHistory(static_cast<std::size_t>(histSize));

    /* Optional memory-write watchpoint.  CLI values override yaml. */
    int watchAddr = cli.historyWatchAddr >= 0
                  ? cli.historyWatchAddr : config.historyWatchAddr;
    int watchLen  = cli.historyWatchLen  >= 0
                  ? cli.historyWatchLen  : config.historyWatchLen;
    if (watchLen > 0)
        emu.setMemoryWatch(static_cast<std::uint16_t>(watchAddr),
                           static_cast<std::uint16_t>(watchLen));

    int readWatchAddr = cli.historyReadWatchAddr >= 0
                      ? cli.historyReadWatchAddr : config.historyReadWatchAddr;
    int readWatchLen  = cli.historyReadWatchLen  >= 0
                      ? cli.historyReadWatchLen  : config.historyReadWatchLen;
    if (readWatchLen > 0)
        emu.setReadWatch(static_cast<std::uint16_t>(readWatchAddr),
                         static_cast<std::uint16_t>(readWatchLen));

    emu.reset();

    ms0515::Debugger dbg(emu);
    ms0515_frontend::Video video;

    /* ── Screen reader (VRAM → text output, optional) ───────────────────── */
    ms0515::ScreenReader screenReader;
    screenReader.buildFont({emu.board().mem.rom, MEM_ROM_SIZE});
    FILE *screenDumpFile = nullptr;
    if (!cli.screenDumpPath.empty()) {
        if (cli.screenDumpPath == "stderr") {
            screenDumpFile = stderr;
        } else if (cli.screenDumpPath == "stdout") {
            screenDumpFile = stdout;
        } else {
            screenDumpFile = std::fopen(cli.screenDumpPath.c_str(), "w");
            if (!screenDumpFile)
                std::fprintf(stderr, "warning: cannot open screen dump '%s'\n",
                             cli.screenDumpPath.c_str());
        }
        screenReader.setOutput(screenDumpFile);
    }

    /* ── Audio ──────────────────────────────────────────────────────────── */
    ms0515_frontend::Audio audio;
    if (!audio.init()) {
        std::fprintf(stderr, "warning: audio init failed, continuing without sound\n");
    }
    /* Wire the board's sound callback to the Audio transition logger. */
    emu.setSoundCallback([&audio, &emu](int value) {
        audio.addTransition(emu.board().frame_cycle_pos, value);
    });

    ms0515_frontend::PhysicalKeyboard physKbd;
    physKbd.setHostMode(config.hostMode);

    bool     running       = true;  /* emulator is running (not paused) */
    bool     quit          = false;
    bool     wantScreenshot = false;
    uint32_t frameCounter  = 0;     /* monotonic, drives flash attribute */
    uint32_t emuFramesSinceReset = 0;
    uint32_t hostMsAtStart       = SDL_GetTicks();
    uint32_t hostMsAtLastReset   = hostMsAtStart;
    /* Speed control — decouple emulation rate from monitor VSync. */
    float    targetSpeed         = 100.0f;  /* percent, 20–500 */
    float    emuTimeAccumMs      = 0.0f;    /* accumulated emu time to run */
    uint32_t lastTickMs          = SDL_GetTicks();
    constexpr float kFrameMs     = 20.0f;   /* one emulated frame = 20 ms */
    /* FPS / speed tracking — sliding 1-second window. */
    uint32_t fpsWindowStartMs    = hostMsAtStart;
    uint32_t emuFramesInWindow   = 0;
    float    fpsDisplay          = 0.0f;
    float    speedDisplay        = 0.0f;
    bool     showDebugger  = config.showDebugger;
    bool     prevShowDebugger = showDebugger;
    bool     showKeyboard  = config.showKeyboard;
    bool     prevShowKeyboard = showKeyboard;
    bool     showScreen    = true;
    /* Force a resize on the first frame so the host window fits the screen
     * caption + padding exactly (initial SDL size is only an approximation). */
    bool     prevShowScreen = !showScreen;
    ms0515_frontend::OnScreenKeyboard osk;
    osk.loadLayout();
    bool     audioOn       = true;  /* user-togglable audio mute */
    bool     ramDiskOn     = true;  /* enableRamDisk() above */
    /* Per-FDC-unit mount tracker.  When a drive is mounted as
     * double-sided, both side units carry the SAME path and
     * `mountedAsDs[drive]` is set; "Unmount" then drops both sides
     * in one click. */
    std::string mountedFd[4];
    bool        mountedAsDs[2] = {false, false};
    auto refreshMountState = [&]() {
        for (int drive = 0; drive < 2; ++drive) {
            int unit0 = fdcUnitFor(drive, 0);
            int unit1 = fdcUnitFor(drive, 1);
            if (!cli.dsPath[drive].empty()) {
                mountedFd[unit0]  = cli.dsPath[drive];
                mountedFd[unit1]  = cli.dsPath[drive];
                mountedAsDs[drive] = true;
            } else {
                mountedFd[unit0]  = cli.fdPath[unit0];
                mountedFd[unit1]  = cli.fdPath[unit1];
                mountedAsDs[drive] = false;
            }
        }
    };
    refreshMountState();
    /* Non-empty mountErrorMessage means a modal popup is shown next
     * frame.  Used for friendly format-detect / mount-failure feedback
     * inside the GUI; the CLI path still routes errors to stderr. */
    std::string mountErrorMessage;
    bool        mountErrorPending = false;
    int      menuBarHeight = 0;     /* updated each frame once menu drawn */

    /* ── Main loop ─────────────────────────────────────────────────────── */
    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) {
                quit = true;
            } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                physKbd.handleEvent(ev, emu, io.WantCaptureKeyboard);

                /* Host-mode hotkeys will be handled here in the future. */
            }
        }

        /* Run emulated frames based on real elapsed time × speed factor.
         * stepFrame() also drives ms7004_tick from a synthetic timeline
         * (see Emulator::stepFrame), so no separate keyTick call. */
        if (running) {
            uint32_t nowTick = SDL_GetTicks();
            float realDeltaMs = static_cast<float>(nowTick - lastTickMs);
            /* Clamp large deltas (e.g. after window drag or breakpoint) to
             * avoid burst-catching-up hundreds of frames at once. */
            if (realDeltaMs > 200.0f) realDeltaMs = 200.0f;
            emuTimeAccumMs += realDeltaMs * (targetSpeed / 100.0f);
            lastTickMs = nowTick;

            bool audioEnabled = audioOn && (targetSpeed == 100.0f);
            while (emuTimeAccumMs >= kFrameMs && running) {
                if (audioEnabled) audio.beginFrame();
                bool ok = emu.stepFrame();
                if (audioEnabled) audio.endFrame(emu.board().frame_cycle_pos);

                /* Auto-snapshot the moment the CPU spontaneously re-enters
                 * POST.  Always clear the latch so the next event is
                 * caught afresh; only write the file if the user has
                 * enabled the feature. */
                if (emu.board().unexpected_reset) {
                    emu.board().unexpected_reset = false;
                    if (config.autoSnapOnReset) {
                        std::string p = timestampedPath("state_post", ".ms0515");
                        if (auto r = emu.saveState(p); !r) {
                            std::fprintf(stderr,
                                "Auto-snapshot on POST failed: %s\n",
                                r.error().c_str());
                        } else {
                            std::fprintf(stderr,
                                "Auto-snapshot on POST: %s\n", p.c_str());
                        }
                    }
                }

                if (!ok) {
                    running = false;
                    emuTimeAccumMs = 0.0f;
                    break;
                }
                emuTimeAccumMs -= kFrameMs;
                ++emuFramesSinceReset;
                ++emuFramesInWindow;
            }
            /* Update screen reader ~5 Hz regardless of emu speed. */
            if (frameCounter % 10 == 0)
                screenReader.update({emu.vram(), MEM_VRAM_SIZE}, emu.isHires());
        } else {
            lastTickMs = SDL_GetTicks();
            emuTimeAccumMs = 0.0f;
        }

        /* Sliding 1-second speed measurement window. */
        uint32_t nowMs = SDL_GetTicks();
        uint32_t winMs = nowMs - fpsWindowStartMs;
        if (winMs >= 1000) {
            /* Actual emulated frames per second → actual speed %. */
            float emuFps = emuFramesInWindow * 1000.0f / static_cast<float>(winMs);
            fpsDisplay   = emuFps;
            speedDisplay = emuFps / 50.0f * 100.0f;
            emuFramesInWindow = 0;
            fpsWindowStartMs  = nowMs;
        }

        if (cli.maxFrames > 0 && (int)frameCounter >= cli.maxFrames) {
            quit = true;
        }

        /* ── Video decode + texture upload ─────────────────────────── */
        ++frameCounter;
        video.render(emu, frameCounter);
        SDL_UpdateTexture(frameTex, nullptr, video.pixels(),
                          ms0515_frontend::kScreenWidth * 4);

        /* Save screenshot if requested (CLI or interactive). */
        if (!cli.screenshotPath.empty() &&
            (int)frameCounter == (cli.screenshotFrame > 0 ? cli.screenshotFrame : cli.maxFrames)) {
            saveScreenshot(video, cli.screenshotPath);
        }
        if (wantScreenshot) {
            saveScreenshot(video, {});
            wantScreenshot = false;
        }

        /* ── ImGui frame ───────────────────────────────────────────── */
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* Main menu bar */
        if (ImGui::BeginMainMenuBar()) {
            menuBarHeight = (int)ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu("File")) {
                /* Per-drive submenu — flat list of three mount actions
                 * (DS image, side 0, side 1) plus Unmount.  Mounting
                 * anything on one side automatically detaches the
                 * other half of a previous DS mount, so the drive is
                 * always in a coherent state.  Mounting a fresh DS
                 * image replaces both sides at once. */
                for (int drive = 0; drive < 2; ++drive) {
                    int unit0 = fdcUnitFor(drive, 0);
                    int unit1 = fdcUnitFor(drive, 1);
                    const bool side0 = !mountedFd[unit0].empty();
                    const bool side1 = !mountedFd[unit1].empty();
                    const bool any   = side0 || side1;

                    /* Compact summary for the top-level label: how the
                     * drive is currently driven, not which file. */
                    const char *summary;
                    if (!any)                     summary = "empty";
                    else if (mountedAsDs[drive])  summary = "image";
                    else if (side0 && side1)      summary = "both sides";
                    else if (side0)               summary = "0 side";
                    else                          summary = "1 side";
                    auto driveLabel = std::format(
                        "Disk {}: {}", drive, summary);
                    if (ImGui::BeginMenu(driveLabel.c_str())) {
                        /* When a DS image is currently mounted, picking
                         * up just one side means dropping the other to
                         * stay coherent — that's the same regardless
                         * of which mount action triggered it. */
                        auto detachDsRemnant = [&](int keepUnit) {
                            if (!mountedAsDs[drive]) return;
                            int otherUnit = (keepUnit == unit0) ? unit1 : unit0;
                            emu.unmountDisk(otherUnit);
                            mountedFd[otherUnit].clear();
                            config.fdPath[otherUnit].clear();
                            mountedAsDs[drive] = false;
                            config.dsPath[drive].clear();
                        };

                        /* Show the currently mounted DS file alongside
                         * the "Mount image" entry; for SS-only or
                         * empty drives nothing is appended. */
                        std::string mountImageLabel = "Mount image...";
                        if (mountedAsDs[drive]) {
                            mountImageLabel += "    [" +
                                std::filesystem::path(mountedFd[unit0])
                                    .filename().string() + "]";
                        }
                        if (ImGui::MenuItem(mountImageLabel.c_str())) {
                            auto title = std::format(
                                "Select double-sided image for drive {}", drive);
                            std::string p = ms0515_frontend::openFileDialog(
                                window, title.c_str(),
                                ms0515_frontend::FileDialogKind::Disk,
                                initialDirFor(
                                    ms0515_frontend::FileDialogKind::Disk,
                                    config));
                            if (!p.empty()) {
                                if (auto err = validateDoubleSidedImage(p)) {
                                    mountErrorMessage = std::format(
                                        "Cannot mount disk {}:\n\n{}",
                                        drive, *err);
                                    mountErrorPending = true;
                                } else {
                                    emu.unmountDisk(unit0);
                                    emu.unmountDisk(unit1);
                                    bool ok = emu.mountDisk(unit0, p)
                                           && emu.mountDisk(unit1, p);
                                    if (ok) {
                                        mountedFd[unit0] = p;
                                        mountedFd[unit1] = p;
                                        mountedAsDs[drive] = true;
                                        config.dsPath[drive] = p;
                                        config.fdPath[unit0].clear();
                                        config.fdPath[unit1].clear();
                                        rememberDirFor(config,
                                            ms0515_frontend::FileDialogKind::Disk,
                                            p);
                                    } else {
                                        emu.unmountDisk(unit0);
                                        emu.unmountDisk(unit1);
                                        mountErrorMessage = std::format(
                                            "Failed to mount '{}' on disk {}.",
                                            p, drive);
                                        mountErrorPending = true;
                                    }
                                }
                            }
                        }

                        for (int side = 0; side < 2; ++side) {
                            int unit       = (side == 0) ? unit0 : unit1;
                            int otherUnit  = (side == 0) ? unit1 : unit0;
                            /* Append per-side state.  Filename when
                             * this side is mounted; "[empty]" only if
                             * the OTHER side has something — that
                             * makes the contrast useful.  When the
                             * drive is fully empty or DS-mounted both
                             * side items stay plain. */
                            std::string label = std::format(
                                "Mount side {}...", side);
                            if (!mountedAsDs[drive]) {
                                if (!mountedFd[unit].empty()) {
                                    label += "    [" +
                                        std::filesystem::path(mountedFd[unit])
                                            .filename().string() + "]";
                                } else if (!mountedFd[otherUnit].empty()) {
                                    label += "    [empty]";
                                }
                            }
                            if (ImGui::MenuItem(label.c_str())) {
                                auto title = std::format(
                                    "Select single-side image for "
                                    "disk {} side {}",
                                    drive, side);
                                std::string p = ms0515_frontend::openFileDialog(
                                    window, title.c_str(),
                                    ms0515_frontend::FileDialogKind::Disk,
                                    initialDirFor(
                                        ms0515_frontend::FileDialogKind::Disk,
                                        config));
                                if (!p.empty()) {
                                    if (auto err = validateSingleSideImage(p)) {
                                        mountErrorMessage = std::format(
                                            "Cannot mount disk {} side {}:"
                                            "\n\n{}",
                                            drive, side, *err);
                                        mountErrorPending = true;
                                    } else {
                                        detachDsRemnant(unit);
                                        emu.unmountDisk(unit);
                                        if (emu.mountDisk(unit, p)) {
                                            mountedFd[unit] = p;
                                            config.fdPath[unit] = p;
                                            rememberDirFor(config,
                                                ms0515_frontend::FileDialogKind::Disk,
                                                p);
                                        } else {
                                            mountErrorMessage = std::format(
                                                "Failed to mount '{}' on "
                                                "disk {} side {}.",
                                                p, drive, side);
                                            mountErrorPending = true;
                                        }
                                    }
                                }
                            }
                        }
                        if (ImGui::MenuItem("Unmount", nullptr, false, any)) {
                            emu.unmountDisk(unit0);
                            emu.unmountDisk(unit1);
                            mountedFd[unit0].clear();
                            mountedFd[unit1].clear();
                            mountedAsDs[drive] = false;
                            config.fdPath[unit0].clear();
                            config.fdPath[unit1].clear();
                            config.dsPath[drive].clear();
                            saveConfig(config);
                        }
                        ImGui::EndMenu();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Screenshot")) {
                    wantScreenshot = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {
                    quit = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Machine")) {
                if (ImGui::MenuItem("Reset")) {
                    dbg.reset();
                    /* If the emulator paused itself because the CPU
                     * halted, Reset must also clear the paused state —
                     * otherwise the user resets but stepFrame never
                     * runs again and the screen stays frozen on the
                     * pre-reset VRAM contents. */
                    running = true;
                    emuTimeAccumMs = 0.0f;
                    emuFramesSinceReset = 0;
                    hostMsAtLastReset   = SDL_GetTicks();
                }
                if (ImGui::BeginMenu("ROM")) {
                    for (const auto &romPath : availableRoms) {
                        std::string label =
                            std::filesystem::path(romPath).filename().string();
                        bool selected = (romPath == currentRomPath);
                        if (ImGui::MenuItem(label.c_str(), nullptr, selected)) {
                            if (!selected && emu.loadRomFile(romPath)) {
                                currentRomPath = romPath;
                                romStatus = "ROM: " + currentRomPath;
                                config.romPath = currentRomPath;
                                saveConfig(config);
                                dbg.reset();
                                running = true;
                                emuTimeAccumMs = 0.0f;
                                emuFramesSinceReset = 0;
                                hostMsAtLastReset   = SDL_GetTicks();
                            }
                        }
                    }
                    if (availableRoms.empty())
                        ImGui::MenuItem("(no ROMs found)", nullptr, false, false);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Browse...")) {
                        std::string p = ms0515_frontend::openFileDialog(
                            window, "Select ROM",
                            ms0515_frontend::FileDialogKind::Rom,
                            initialDirFor(ms0515_frontend::FileDialogKind::Rom,
                                          config));
                        if (!p.empty() && p != currentRomPath &&
                            emu.loadRomFile(p))
                        {
                            currentRomPath = p;
                            romStatus = "ROM: " + currentRomPath;
                            config.romPath = currentRomPath;
                            rememberDirFor(config,
                                ms0515_frontend::FileDialogKind::Rom, p);
                            dbg.reset();
                            running = true;
                            emuTimeAccumMs = 0.0f;
                            emuFramesSinceReset = 0;
                            hostMsAtLastReset   = SDL_GetTicks();
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                if (ImGui::MenuItem(running ? "Pause" : "Resume")) {
                    running = !running;
                }
                if (ImGui::BeginMenu(std::format("Speed: {:.0f}%", targetSpeed).c_str())) {
                    constexpr float presets[] = {20, 50, 100, 200, 500};
                    for (float p : presets) {
                        auto label = std::format("{:.0f}%", p);
                        if (ImGui::MenuItem(label.c_str(), nullptr,
                                            targetSpeed == p)) {
                            targetSpeed = p;
                            emuTimeAccumMs = 0.0f;
                        }
                    }
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::SliderFloat("##speed", &targetSpeed,
                                           20.0f, 500.0f, "%.0f%%")) {
                        emuTimeAccumMs = 0.0f;
                    }
                    ImGui::EndMenu();
                }
                bool speedIs100 = (targetSpeed == 100.0f);
                if (ImGui::MenuItem("Audio", nullptr, audioOn, speedIs100)) {
                    audioOn = !audioOn;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Auto-snapshot on POST entry",
                                    nullptr, config.autoSnapOnReset)) {
                    config.autoSnapOnReset = !config.autoSnapOnReset;
                    saveConfig(config);
                }
                if (ImGui::MenuItem("Save State...")) {
                    bool wasRunning = running;
                    running = false;
                    std::string path = ms0515_frontend::saveFileDialog(
                        window, "Save State", "state.ms0515",
                        ms0515_frontend::FileDialogKind::State,
                        initialDirFor(ms0515_frontend::FileDialogKind::State,
                                      config));
                    if (!path.empty()) {
                        if (auto r = emu.saveState(path); !r) {
                            std::fprintf(stderr, "Save state failed: %s\n",
                                         r.error().c_str());
                        } else {
                            rememberDirFor(config,
                                ms0515_frontend::FileDialogKind::State, path);
                        }
                    }
                    running = wasRunning;
                }
                if (ImGui::MenuItem("Load State...")) {
                    bool wasRunning = running;
                    running = false;
                    std::string path = ms0515_frontend::openFileDialog(
                        window, "Load State",
                        ms0515_frontend::FileDialogKind::State,
                        initialDirFor(ms0515_frontend::FileDialogKind::State,
                                      config));
                    if (!path.empty()) {
                        rememberDirFor(config,
                            ms0515_frontend::FileDialogKind::State, path);
                        if (auto r = emu.loadState(path); !r) {
                            std::fprintf(stderr, "Load state failed: %s\n",
                                         r.error().c_str());
                        } else {
                            for (int i = 0; i < 4; ++i) {
                                mountedFd[i] = emu.diskPath(i);
                                config.fdPath[i] = mountedFd[i];
                            }
                            saveConfig(config);
                        }
                    }
                    running = wasRunning;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Components")) {
                if (ImGui::MenuItem("512 KB RAM disk (EX0:)",
                                    nullptr, ramDiskOn)) {
                    /* Toggle is visual only until core supports disabling;
                     * currently RAM disk stays on. */
                    if (!ramDiskOn) {
                        emu.enableRamDisk();
                        ramDiskOn = true;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Screen", nullptr, &showScreen);
                ImGui::MenuItem("Debugger", nullptr, &showDebugger);
                ImGui::MenuItem("On-screen keyboard", nullptr, &showKeyboard);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        /* Disk-mount error modal — opened the frame after a mount
         * attempt fails (format-detect rejection, fdc_attach failure,
         * etc.).  Stays modal until the user clicks OK. */
        if (mountErrorPending) {
            ImGui::OpenPopup("Disk mount error");
            mountErrorPending = false;
        }
        /* Pin the popup width so a long path doesn't blow it up; the
         * height still auto-fits the wrapped message.  Anchor it to
         * the centre of the host window — without this ImGui places
         * the popup wherever it last was, which is unpredictable. */
        {
            ImVec2 vpCentre = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(vpCentre, ImGuiCond_Appearing,
                                    ImVec2(0.5f, 0.5f));
        }
        ImGui::SetNextWindowSizeConstraints(ImVec2(400, 0),
                                            ImVec2(400, FLT_MAX));
        if (ImGui::BeginPopupModal("Disk mount error", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoResize)) {
            /* Split the message: the headline (everything up to the
             * first newline) is rendered left-aligned and wrapped;
             * the detail (everything after the first newline) is
             * rendered centred line by line.  ImGui has no built-in
             * centred TextWrapped, so for the detail block we step
             * through paragraph by paragraph, ask the font for the
             * word-wrap break inside each, and SetCursorPosX so the
             * line lands centred on the available content region. */
            const std::string &msg = mountErrorMessage;
            std::size_t splitAt = msg.find('\n');
            std::string headline = (splitAt == std::string::npos)
                                 ? msg : msg.substr(0, splitAt);
            std::string detail   = (splitAt == std::string::npos)
                                 ? std::string{}
                                 : msg.substr(splitAt + 1);
            /* Skip extra leading newlines so we don't render a
             * pointless empty line between headline and detail. */
            while (!detail.empty() && detail.front() == '\n')
                detail.erase(detail.begin());

            ImGui::PushTextWrapPos(0.0f);  /* wrap to content region */
            ImGui::TextUnformatted(headline.c_str());
            ImGui::PopTextWrapPos();

            if (!detail.empty()) {
                ImGui::Spacing();
                ImFont *font    = ImGui::GetFont();
                const char *p   = detail.data();
                const char *end = p + detail.size();
                float wrapWidth = ImGui::GetContentRegionAvail().x;

                while (p <= end) {
                    const char *paraEnd = p;
                    while (paraEnd < end && *paraEnd != '\n') ++paraEnd;

                    if (p == paraEnd) {
                        ImGui::Spacing();
                    } else {
                        const char *ls = p;
                        while (ls < paraEnd) {
                            const char *le = font->CalcWordWrapPositionA(
                                1.0f, ls, paraEnd, wrapWidth);
                            if (le == ls)
                                le = (ls + 1 <= paraEnd) ? ls + 1 : paraEnd;

                            ImVec2 sz = ImGui::CalcTextSize(ls, le);
                            float avail = ImGui::GetContentRegionAvail().x;
                            float pad   = (avail - sz.x) * 0.5f;
                            if (pad > 0)
                                ImGui::SetCursorPosX(
                                    ImGui::GetCursorPosX() + pad);
                            ImGui::TextUnformatted(ls, le);

                            ls = le;
                            while (ls < paraEnd && *ls == ' ') ++ls;
                        }
                    }

                    if (paraEnd >= end) break;
                    p = paraEnd + 1;
                }
            }

            ImGui::Separator();
            const float btnW = 120.0f;
            float availW = ImGui::GetContentRegionAvail().x;
            float btnPad = (availW - btnW) * 0.5f;
            if (btnPad > 0)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btnPad);
            if (ImGui::Button("OK", ImVec2(btnW, 0))) {
                mountErrorMessage.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        /* ── Layout math ───────────────────────────────────────────────
         * Compute the size each inner window will occupy so we can grow
         * or shrink the SDL host window when any of them is toggled. */
        const int screenContentW = ms0515_frontend::kScreenWidth  * scale;
        const int screenContentH = ms0515_frontend::kScreenHeight * scale;
        const ImGuiStyle &imstyle = ImGui::GetStyle();
        const float titleBarH =
            ImGui::GetFontSize() + imstyle.FramePadding.y * 2.0f;
        const int scrWinW = screenContentW +
                            (int)(imstyle.WindowPadding.x * 2.0f) + 2;
        const int scrWinH = screenContentH +
                            (int)(imstyle.WindowPadding.y * 2.0f + titleBarH) + 2;
        const int dbgWinW = 380;
        const int dbgWinH = std::max(scrWinH, 360);
        const int oskWinW = (int)osk.pixelWidth();
        const int oskWinH = (int)osk.pixelHeight();
        const int statusBarH =
            (int)(ImGui::GetTextLineHeightWithSpacing() * 2.0f +
                  imstyle.WindowPadding.y * 2.0f + 2.0f);

        /* Screen window — framebuffer wrapped in an ImGui window with a
         * caption, same style as the debugger and on-screen keyboard. */
        if (showScreen) {
            ImGui::SetNextWindowPos(
                ImVec2(8.0f, (float)(menuBarHeight + 8)),
                ImGuiCond_FirstUseEver);
            ImGuiWindowFlags f = ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize;
            if (ImGui::Begin("Screen (MS0515)", &showScreen, f)) {
                ImGui::Image((ImTextureID)(intptr_t)frameTex,
                             ImVec2((float)screenContentW,
                                    (float)screenContentH));
            }
            ImGui::End();
        }

        /* Place the debugger to the right of the screen window on first use. */
        if (showDebugger) {
            int dbgX = 8 + (showScreen ? scrWinW + 8 : 0);
            ImGui::SetNextWindowPos(
                ImVec2((float)dbgX, (float)(menuBarHeight + 8)),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2((float)dbgWinW, (float)dbgWinH),
                ImGuiCond_FirstUseEver);
            drawDebuggerWindow(dbg, running, romStatus, showDebugger);
        }

        /* On-screen MS7004 keyboard — initial position below the top row. */
        if (showKeyboard) {
            int topRowH = std::max(showScreen ? scrWinH : 0,
                                   showDebugger ? dbgWinH : 0);
            ImGui::SetNextWindowPos(
                ImVec2(8.0f, (float)(menuBarHeight + 8 + topRowH + 8)),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2((float)oskWinW, (float)oskWinH),
                ImGuiCond_FirstUseEver);
            osk.draw(emu, showKeyboard);
        }

        /* Resize the SDL host window whenever the set of visible inner
         * windows changes, sized to exactly contain them. */
        if (showScreen   != prevShowScreen   ||
            showDebugger != prevShowDebugger ||
            showKeyboard != prevShowKeyboard) {
            int topRowW = 8
                        + (showScreen   ? scrWinW + 8 : 0)
                        + (showDebugger ? dbgWinW + 8 : 0);
            if (!showScreen && !showDebugger) topRowW = 16;
            int topRowH = std::max(showScreen   ? scrWinH : 0,
                                   showDebugger ? dbgWinH : 0);
            int totalW = std::max(topRowW,
                                  showKeyboard ? oskWinW + 16 : 0);
            if (totalW < 320) totalW = 320;
            int totalH = menuBarHeight + 8 + topRowH
                       + (showKeyboard ? oskWinH + 16 : 0)
                       + statusBarH + 8;
            if (totalH < 200) totalH = 200;
            SDL_SetWindowSize(window, totalW, totalH);
            prevShowScreen   = showScreen;
            prevShowDebugger = showDebugger;
            prevShowKeyboard = showKeyboard;
        }

        /* Status bar (two lines) — pinned to the bottom of the host window. */
        {
            int cw = 0, ch = 0;
            SDL_GetWindowSize(window, &cw, &ch);
            const ImGuiStyle &st = ImGui::GetStyle();
            const float lineH   = ImGui::GetTextLineHeightWithSpacing();
            const float statusH =
                lineH * 2.0f + st.WindowPadding.y * 2.0f + 2.0f;
            ImGui::SetNextWindowPos(ImVec2(0.0f, (float)ch - statusH));
            ImGui::SetNextWindowSize(ImVec2((float)cw, statusH));
            ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav;
            if (ImGui::Begin("##statusbar", nullptr, f)) {
                /* Line 1: CPU state, FPS, time. */
                const auto &cpu = emu.cpu();
                const char *state = cpu.halted  ? "HALT" :
                                    cpu.waiting ? "WAIT" :
                                    running     ? "RUN"  : "PAUSE";
                ImVec4 col = cpu.halted  ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                             cpu.waiting ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                             running     ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) :
                                           ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                ImGui::TextColored(col, "%s", state);
                ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

                ImGui::Text("%.0f%%/%.0f%%  %.0f fps",
                            speedDisplay, targetSpeed, fpsDisplay);
                ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

                uint32_t hostS = (SDL_GetTicks() - hostMsAtLastReset) / 1000;
                uint32_t emuMs = emuFramesSinceReset * 20;  /* 50 Hz frame */
                uint32_t emuS  = emuMs / 1000;
                ImGui::Text("host %02u:%02u:%02u  emu %02u:%02u:%02u",
                            hostS / 3600, (hostS / 60) % 60, hostS % 60,
                            emuS  / 3600, (emuS  / 60) % 60, emuS  % 60);
                ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

                /* Host mode indicator — keyboard disconnected from emulator */
                if (physKbd.hostMode()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "HOST");
                    ImGui::SameLine();
                }

                auto modCol = [](bool on) {
                    return on ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f)
                              : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
                };
                bool vrOn  = emu.keyHeld(MS7004_KEY_SHIFT_L)
                          || emu.keyHeld(MS7004_KEY_SHIFT_R);
                bool suOn  = emu.keyHeld(MS7004_KEY_CTRL);
                bool kmpOn = emu.keyHeld(MS7004_KEY_COMPOSE);
                ImGui::TextColored(modCol(vrOn),            "\xd0\x92\xd0\xa0");
                ImGui::SameLine();
                ImGui::TextColored(modCol(suOn),            "\xd0\xa1\xd0\xa3");
                ImGui::SameLine();
                ImGui::TextColored(modCol(emu.capsOn()),    "\xd0\xa4\xd0\x9a\xd0\xa1");
                ImGui::SameLine();
                ImGui::TextColored(modCol(kmpOn),           "\xd0\x9a\xd0\x9c\xd0\x9f");
                ImGui::SameLine();
                if (emu.ruslatOn()) {
                    ImGui::TextColored(modCol(true),         "\xd0\xa0\xd0\xa3\xd0\xa1");
                } else {
                    ImGui::TextColored(modCol(true),         "\xd0\x9b\xd0\x90\xd0\xa2");
                }

                /* Line 2: per-drive mount summary.  One column per
                 * physical drive, mode-dependent format:
                 *   empty  →  "Disk 0: empty"
                 *   DS     →  "Disk 0: <name> (DS)"
                 *   SS     →  "Disk 0: <side0> / <side1>"  (either may
                 *             be (empty)) */
                for (int drive = 0; drive < 2; ++drive) {
                    int unit0 = fdcUnitFor(drive, 0);
                    int unit1 = fdcUnitFor(drive, 1);
                    if (drive > 0) {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("|");
                        ImGui::SameLine();
                    }
                    if (!mountedFd[unit0].empty() ||
                        !mountedFd[unit1].empty()) {
                        if (mountedAsDs[drive]) {
                            std::string n = std::filesystem::path(
                                mountedFd[unit0]).filename().string();
                            ImGui::Text("Disk %d: %s (DS)", drive, n.c_str());
                        } else {
                            auto sideName = [&](int unit) -> std::string {
                                if (mountedFd[unit].empty()) return "(empty)";
                                return std::filesystem::path(mountedFd[unit])
                                           .filename().string();
                            };
                            ImGui::Text("Disk %d: %s / %s",
                                drive,
                                sideName(unit0).c_str(),
                                sideName(unit1).c_str());
                        }
                    } else {
                        ImGui::Text("Disk %d: empty", drive);
                    }
                }
            }
            ImGui::End();
        }

        /* ── Render ────────────────────────────────────────────────── */
        /* The framebuffer is drawn via ImGui::Image inside the "Screen"
         * window, so all host-window content comes from ImGui. */
        SDL_SetRenderDrawColor(renderer, 40, 40, 48, 255);
        SDL_RenderClear(renderer);

        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);
    }

    /* ── Save config on exit ──────────────────────────────────────────── */
    config.showKeyboard = showKeyboard;
    config.showDebugger = showDebugger;
    config.hostMode     = physKbd.hostMode();
    saveConfig(config);

    /* ── Shutdown ───────────────────────────────────────────────────────── */
    if (screenDumpFile && screenDumpFile != stderr && screenDumpFile != stdout)
        std::fclose(screenDumpFile);
    audio.shutdown();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyTexture(frameTex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
