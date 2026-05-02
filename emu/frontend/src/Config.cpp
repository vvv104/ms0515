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
    if (kbdTypingDelayMs >= 0 || kbdTypingPeriodMs >= 0 ||
        kbdGameDelayMs   >= 0 || kbdGamePeriodMs   >= 0 ||
        kbdAutoGameMode  >= 0) return false;
    if (fullscreen) return false;
    return true;
}

std::string Paths::exeDir()
{
    if (char *base = SDL_GetBasePath()) {
        std::string dir(base);
        SDL_free(base);
        return dir;
    }
    return {};
}

std::string Config::path()
{
    return Paths::exeDir() + "ms0515.yaml";
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

Config Config::load()
{
    Config cfg;
    std::ifstream f(Config::path());
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
            cfg.historyWatchAddr = Paths::parseNumber(val);
        else if (key == "history_watch_len")
            cfg.historyWatchLen = Paths::parseNumber(val);
        else if (key == "history_read_watch_addr")
            cfg.historyReadWatchAddr = Paths::parseNumber(val);
        else if (key == "history_read_watch_len")
            cfg.historyReadWatchLen = Paths::parseNumber(val);
        else if (key == "kbd_typing_delay_ms")  cfg.kbdTypingDelayMs  = Paths::parseNumber(val);
        else if (key == "kbd_typing_period_ms") cfg.kbdTypingPeriodMs = Paths::parseNumber(val);
        else if (key == "kbd_game_delay_ms")    cfg.kbdGameDelayMs    = Paths::parseNumber(val);
        else if (key == "kbd_game_period_ms")   cfg.kbdGamePeriodMs   = Paths::parseNumber(val);
        else if (key == "kbd_auto_game_mode")   cfg.kbdAutoGameMode   = (val == "true") ? 1 : 0;
        else if (key == "fullscreen")           cfg.fullscreen        = (val == "true");
        /* Unknown keys: silently ignored (forward/backward compat). */
    }
    return cfg;
}

void Config::save() const
{
    std::string cfgPath = Config::path();
    if (isDefault()) {
        std::filesystem::remove(cfgPath);
        return;
    }
    std::ofstream f(cfgPath);
    if (!f) return;
    f << "# MS0515 emulator configuration\n";

    /* Double-sided drives take precedence — when set, the side-N
     * fields below are not also written for the same drive. */
    static constexpr const char *kDsKeys[] = {"disk0", "disk1"};
    for (int drive = 0; drive < 2; ++drive) {
        if (!dsPath[drive].empty())
            f << kDsKeys[drive] << ": \"" << dsPath[drive] << "\"\n";
    }
    static constexpr struct { int drive, side; const char *name; } kSlots[] = {
        {0, 0, "disk0_side0"}, {0, 1, "disk0_side1"},
        {1, 0, "disk1_side0"}, {1, 1, "disk1_side1"},
    };
    for (const auto &s : kSlots) {
        if (!dsPath[s.drive].empty()) continue;
        const auto &p = fdPath[fdcUnitFor(s.drive, s.side)];
        if (!p.empty())
            f << s.name << ": \"" << p << "\"\n";
    }
    if (!romPath.empty())
        f << "rom: \"" << romPath << "\"\n";
    if (showKeyboard) f << "show_keyboard: true\n";
    if (showDebugger) f << "show_debugger: true\n";
    if (hostMode)     f << "host_mode: true\n";
    if (historySize != 0)
        f << "history_size: " << historySize << "\n";
    if (historyWatchAddr != 0 || historyWatchLen != 0) {
        f << "history_watch_addr: 0o" << std::oct << historyWatchAddr
          << std::dec << "\n";
        f << "history_watch_len: "    << historyWatchLen << "\n";
    }
    if (historyReadWatchAddr != 0 || historyReadWatchLen != 0) {
        f << "history_read_watch_addr: 0o" << std::oct
          << historyReadWatchAddr << std::dec << "\n";
        f << "history_read_watch_len: "   << historyReadWatchLen << "\n";
    }
    if (kbdTypingDelayMs  >= 0)
        f << "kbd_typing_delay_ms: "  << kbdTypingDelayMs  << "\n";
    if (kbdTypingPeriodMs >= 0)
        f << "kbd_typing_period_ms: " << kbdTypingPeriodMs << "\n";
    if (kbdGameDelayMs    >= 0)
        f << "kbd_game_delay_ms: "    << kbdGameDelayMs    << "\n";
    if (kbdGamePeriodMs   >= 0)
        f << "kbd_game_period_ms: "   << kbdGamePeriodMs   << "\n";
    if (kbdAutoGameMode   >= 0)
        f << "kbd_auto_game_mode: "
          << (kbdAutoGameMode ? "true" : "false") << "\n";
    if (fullscreen) f << "fullscreen: true\n";
}

std::string Paths::initialDirFor(FileDialogKind kind)
{
    std::string base = exeDir();
    switch (kind) {
    case FileDialogKind::Disk:  return base + "assets/disks";
    case FileDialogKind::Rom:   return base + "assets/rom";
    case FileDialogKind::State: return base;
    }
    return base;
}

std::vector<std::filesystem::path> Paths::searchRoots()
{
    namespace fs = std::filesystem;
    std::error_code ec;
    std::vector<fs::path> roots;
    if (char *base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    roots.emplace_back(fs::current_path(ec));
    return roots;
}

std::string Paths::timestamped(std::string_view prefix, std::string_view ext)
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
    return exeDir() + std::string{prefix} + "_" + buf + std::string{ext};
}

} /* namespace ms0515_frontend */
