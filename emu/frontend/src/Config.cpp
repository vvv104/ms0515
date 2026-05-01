#define _CRT_SECURE_NO_WARNINGS
#include "Config.hpp"

#include <SDL.h>

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>

namespace ms0515_frontend {

bool Config::isDefault() const
{
    for (int i = 0; i < 4; ++i)
        if (!fdPath[i].empty()) return false;
    for (int i = 0; i < 2; ++i)
        if (!dsPath[i].empty()) return false;
    if (!romPath.empty() || showKeyboard || showDebugger || hostMode)
        return false;
    if (historySize != 0) return false;
    if (historyWatchAddr != 0 || historyWatchLen != 0) return false;
    if (historyReadWatchAddr != 0 || historyReadWatchLen != 0)
        return false;
    if (autoSnapOnReset) return false;
    if (kbdTypingDelayMs >= 0 || kbdTypingPeriodMs >= 0 ||
        kbdGameDelayMs   >= 0 || kbdGamePeriodMs   >= 0 ||
        kbdAutoGameMode  >= 0) return false;
    if (fullscreen) return false;
    return true;
}

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

int parseNumber(const std::string &s)
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
        if (line.empty() || line[0] == '#') continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
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
        /* Quiet migration of legacy fd0..fd3 keys. */
        else if (key == "fd0") cfg.fdPath[fdcUnitFor(0, 0)] = val;
        else if (key == "fd1") cfg.fdPath[fdcUnitFor(1, 0)] = val;
        else if (key == "fd2") cfg.fdPath[fdcUnitFor(0, 1)] = val;
        else if (key == "fd3") cfg.fdPath[fdcUnitFor(1, 1)] = val;
        else if (key == "rom") cfg.romPath = val;
        else if (key == "show_keyboard") cfg.showKeyboard = (val == "true");
        else if (key == "show_debugger") cfg.showDebugger = (val == "true");
        else if (key == "host_mode")     cfg.hostMode     = (val == "true");
        else if (key == "history_size") {
            try { cfg.historySize = std::stoi(val); }
            catch (...) { cfg.historySize = 0; }
            if (cfg.historySize < 0) cfg.historySize = 0;
        }
        else if (key == "history_watch_addr")
            cfg.historyWatchAddr = parseNumber(val);
        else if (key == "history_watch_len")
            cfg.historyWatchLen = parseNumber(val);
        else if (key == "history_read_watch_addr")
            cfg.historyReadWatchAddr = parseNumber(val);
        else if (key == "history_read_watch_len")
            cfg.historyReadWatchLen = parseNumber(val);
        else if (key == "auto_snap_on_reset")
            cfg.autoSnapOnReset = (val == "true");
        else if (key == "kbd_typing_delay_ms")  cfg.kbdTypingDelayMs  = parseNumber(val);
        else if (key == "kbd_typing_period_ms") cfg.kbdTypingPeriodMs = parseNumber(val);
        else if (key == "kbd_game_delay_ms")    cfg.kbdGameDelayMs    = parseNumber(val);
        else if (key == "kbd_game_period_ms")   cfg.kbdGamePeriodMs   = parseNumber(val);
        else if (key == "kbd_auto_game_mode")   cfg.kbdAutoGameMode   = (val == "true") ? 1 : 0;
        else if (key == "fullscreen")           cfg.fullscreen        = (val == "true");
        /* Unknown keys: silently ignored (forward/backward compat). */
    }
    return cfg;
}

void saveConfig(const Config &cfg)
{
    std::string path = configPath();
    if (cfg.isDefault()) {
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
        if (!cfg.dsPath[s.drive].empty()) continue;
        const auto &p = cfg.fdPath[fdcUnitFor(s.drive, s.side)];
        if (!p.empty())
            f << s.name << ": \"" << p << "\"\n";
    }
    if (!cfg.romPath.empty())
        f << "rom: \"" << cfg.romPath << "\"\n";
    if (cfg.showKeyboard) f << "show_keyboard: true\n";
    if (cfg.showDebugger) f << "show_debugger: true\n";
    if (cfg.hostMode)     f << "host_mode: true\n";
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
    if (cfg.kbdTypingDelayMs  >= 0)
        f << "kbd_typing_delay_ms: "  << cfg.kbdTypingDelayMs  << "\n";
    if (cfg.kbdTypingPeriodMs >= 0)
        f << "kbd_typing_period_ms: " << cfg.kbdTypingPeriodMs << "\n";
    if (cfg.kbdGameDelayMs    >= 0)
        f << "kbd_game_delay_ms: "    << cfg.kbdGameDelayMs    << "\n";
    if (cfg.kbdGamePeriodMs   >= 0)
        f << "kbd_game_period_ms: "   << cfg.kbdGamePeriodMs   << "\n";
    if (cfg.kbdAutoGameMode   >= 0)
        f << "kbd_auto_game_mode: "
          << (cfg.kbdAutoGameMode ? "true" : "false") << "\n";
    if (cfg.fullscreen) f << "fullscreen: true\n";
}

std::string initialDirFor(FileDialogKind kind)
{
    std::string base = getExeDir();
    switch (kind) {
    case FileDialogKind::Disk:  return base + "assets/disks";
    case FileDialogKind::Rom:   return base + "assets/rom";
    case FileDialogKind::State: return base;
    }
    return base;
}

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

} /* namespace ms0515_frontend */
