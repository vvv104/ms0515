/*
 * main.cpp — MS0515 emulator frontend (SDL2 + Dear ImGui).
 *
 * Runs the core emulator in a 50/60/72 Hz frame loop and presents the
 * decoded framebuffer through an SDL_Renderer texture.  A Dear ImGui
 * debugger overlay provides register/disassembly/breakpoint views and
 * run/step controls.
 *
 * CLI:
 *   ms0515 [--rom <path>] [--fd0 <path>] [--fd1 <path>]
 *          [--fd2 <path>] [--fd3 <path>]
 *          [--disk <path> [--drive N]]     (legacy, maps to --fdN)
 *          [--screen-dump stderr|stdout|<path>]
 *          [--trace stderr|<path>]
 *
 * FD0..FD3 are the four logical floppy units addressable via bits 1:0
 * of System Register A.  Each disk image represents one side of a
 * physical diskette; the BIOS treats every side as an independent unit.
 *
 * Defaults: loads reference/docs/ms0515-roma.rom if --rom is not given.
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
#include "ScreenReader.hpp"
#include "Video.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliArgs {
    std::string romPath;
    std::string fdPath[4];     /* FD0..FD3 */
    std::string tracePath;
    std::string screenDumpPath; /* --screen-dump: VRAM text output */
    std::string screenshotPath;
    int         maxFrames = 0;      /* 0 = run forever */
    int         screenshotFrame = 0; /* frame number to take screenshot */
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
    std::string romPath;
    bool showKeyboard = false;
    bool showDebugger = false;
    bool hostMode     = false;

    bool isDefault() const {
        for (int i = 0; i < 4; ++i)
            if (!fdPath[i].empty()) return false;
        if (!romPath.empty() || showKeyboard || showDebugger || hostMode)
            return false;
        return true;
    }
};

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

        if (key == "fd0") cfg.fdPath[0] = val;
        else if (key == "fd1") cfg.fdPath[1] = val;
        else if (key == "fd2") cfg.fdPath[2] = val;
        else if (key == "fd3") cfg.fdPath[3] = val;
        else if (key == "rom") cfg.romPath = val;
        else if (key == "show_keyboard") cfg.showKeyboard = (val == "true");
        else if (key == "show_debugger") cfg.showDebugger = (val == "true");
        else if (key == "host_mode")     cfg.hostMode     = (val == "true");
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
    for (int i = 0; i < 4; ++i) {
        if (!cfg.fdPath[i].empty())
            f << "fd" << i << ": \"" << cfg.fdPath[i] << "\"\n";
    }
    if (!cfg.romPath.empty())
        f << "rom: \"" << cfg.romPath << "\"\n";
    if (cfg.showKeyboard) f << "show_keyboard: true\n";
    if (cfg.showDebugger) f << "show_debugger: true\n";
    if (cfg.hostMode)     f << "host_mode: true\n";
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

CliArgs parseArgs(int argc, char **argv)
{
    CliArgs out;
    std::string legacyDisk;
    int         legacyDrive = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--rom" && i + 1 < argc) {
            out.romPath = argv[++i];
        } else if ((a == "--fd0" || a == "--fd1" ||
                    a == "--fd2" || a == "--fd3") && i + 1 < argc) {
            out.fdPath[a[4] - '0'] = argv[++i];
        } else if (a == "--disk" && i + 1 < argc) {
            legacyDisk = argv[++i];
        } else if (a == "--drive" && i + 1 < argc) {
            legacyDrive = std::atoi(argv[++i]);
        } else if (a == "--trace" && i + 1 < argc) {
            out.tracePath = argv[++i];
        } else if (a == "--frames" && i + 1 < argc) {
            out.maxFrames = std::atoi(argv[++i]);
        } else if (a == "--screen-dump" && i + 1 < argc) {
            out.screenDumpPath = argv[++i];
        } else if (a == "--screenshot" && i + 1 < argc) {
            out.screenshotPath = argv[++i];
        } else if (a == "--screenshot-frame" && i + 1 < argc) {
            out.screenshotFrame = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "warning: unknown argument '%s'\n", a.c_str());
        }
    }
    if (!legacyDisk.empty() && legacyDrive >= 0 && legacyDrive < 4) {
        out.fdPath[legacyDrive] = legacyDisk;
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
            FILE *f = std::fopen(path.c_str(), "rb");
            if (f) {
                std::fclose(f);
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
            FILE *fs = std::fopen(path.c_str(), "rb");
            if (fs) {
                std::fclose(fs);
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
        romStatus = "ROM: <not found — pass --rom <path>>";
        std::fprintf(stderr, "%s\n", romStatus.c_str());
    } else if (!emu.loadRomFile(currentRomPath)) {
        romStatus = "ROM: FAILED to load '" + currentRomPath + "'";
        std::fprintf(stderr, "%s\n", romStatus.c_str());
    } else {
        romStatus = "ROM: " + currentRomPath;
    }
    for (int i = 0; i < 4; ++i) {
        if (cli.fdPath[i].empty()) continue;
        if (emu.mountDisk(i, cli.fdPath[i])) {
            config.fdPath[i] = cli.fdPath[i];
        } else {
            std::fprintf(stderr,
                         "error: failed to mount '%s' on FD%d\n",
                         cli.fdPath[i].c_str(), i);
        }
    }
    saveConfig(config);
    /* Enable the 512 KB RAM disk expansion board (EX0:). */
    emu.enableRamDisk();
    if (!cli.tracePath.empty()) {
        if (!board_trace_open(&emu.board(), cli.tracePath.c_str())) {
            std::fprintf(stderr, "warning: failed to open trace '%s'\n",
                         cli.tracePath.c_str());
        }
    }

    emu.reset();

    ms0515::Debugger dbg(emu);
    ms0515_frontend::Video video;

    /* ── Screen reader (VRAM → text output, optional) ───────────────────── */
    ms0515_frontend::ScreenReader screenReader;
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
    std::string mountedFd[4];
    for (int i = 0; i < 4; ++i) mountedFd[i] = cli.fdPath[i];
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

        /* Advance ms7004 auto-repeat timer. */
        emu.keyTick(SDL_GetTicks());

        /* Run emulated frames based on real elapsed time × speed factor. */
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

        /* Save screenshot if requested */
        if (!cli.screenshotPath.empty() &&
            (int)frameCounter == (cli.screenshotFrame > 0 ? cli.screenshotFrame : cli.maxFrames)) {
            SDL_Surface *surf = SDL_CreateRGBSurfaceFrom(
                (void *)video.pixels(),
                ms0515_frontend::kScreenWidth,
                ms0515_frontend::kScreenHeight,
                32, ms0515_frontend::kScreenWidth * 4,
                0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
            if (surf) {
                SDL_SaveBMP(surf, cli.screenshotPath.c_str());
                SDL_FreeSurface(surf);
                std::fprintf(stderr, "Screenshot saved: %s (frame %u)\n",
                             cli.screenshotPath.c_str(), frameCounter);
            }
        }

        /* ── ImGui frame ───────────────────────────────────────────── */
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* Main menu bar */
        if (ImGui::BeginMainMenuBar()) {
            menuBarHeight = (int)ImGui::GetWindowSize().y;
            if (ImGui::BeginMenu("File")) {
                for (int i = 0; i < 4; ++i) {
                    std::string label;
                    if (mountedFd[i].empty())
                        label = std::format("Mount FD{}...", i);
                    else
                        label = std::format("Mount FD{}...  [{}]", i,
                                            mountedFd[i]);
                    if (ImGui::MenuItem(label.c_str())) {
                        auto title = std::format("Select image for FD{}", i);
                        std::string p = ms0515_frontend::openFileDialog(window, title.c_str());
                        if (!p.empty()) {
                            if (emu.mountDisk(i, p)) {
                                mountedFd[i] = p;
                                config.fdPath[i] = p;
                                saveConfig(config);
                            } else {
                                std::fprintf(stderr,
                                    "error: failed to mount '%s' on FD%d\n",
                                    p.c_str(), i);
                            }
                        }
                    }
                }
                ImGui::Separator();
                for (int i = 0; i < 4; ++i) {
                    auto label = std::format("Unmount FD{}", i);
                    if (ImGui::MenuItem(label.c_str(), nullptr, false,
                                        !mountedFd[i].empty())) {
                        emu.unmountDisk(i);
                        mountedFd[i].clear();
                        config.fdPath[i].clear();
                        saveConfig(config);
                    }
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
                                emuFramesSinceReset = 0;
                                hostMsAtLastReset   = SDL_GetTicks();
                            }
                        }
                    }
                    if (availableRoms.empty())
                        ImGui::MenuItem("(no ROMs found)", nullptr, false, false);
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
                if (ImGui::MenuItem("Save State...")) {
                    bool wasRunning = running;
                    running = false;
                    std::string path = ms0515_frontend::saveFileDialog(
                        window, "Save State", "state.ms0515");
                    if (!path.empty()) {
                        if (auto r = emu.saveState(path); !r)
                            std::fprintf(stderr, "Save state failed: %s\n",
                                         r.error().c_str());
                    }
                    running = wasRunning;
                }
                if (ImGui::MenuItem("Load State...")) {
                    bool wasRunning = running;
                    running = false;
                    std::string path = ms0515_frontend::openFileDialog(
                        window, "Load State");
                    if (!path.empty()) {
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

                uint32_t hostS = (SDL_GetTicks() - hostMsAtStart) / 1000;
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

                /* Line 2: disks (may be long paths/filenames). */
                for (int i = 0; i < 4; ++i) {
                    const char *name = "—";
                    std::string base;
                    if (!mountedFd[i].empty()) {
                        base = std::filesystem::path(mountedFd[i])
                               .filename().string();
                        name = base.c_str();
                    }
                    if (i > 0) ImGui::SameLine();
                    ImGui::Text("FD%d:%s", i, name);
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
    board_trace_close(&emu.board());
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
