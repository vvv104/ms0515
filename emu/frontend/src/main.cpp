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

#include "Assets.hpp"
#include "Audio.hpp"
#include "Cli.hpp"
#include "Config.hpp"
#include "OnScreenKeyboard.hpp"
#include "PhysicalKeyboard.hpp"
#include "Platform.hpp"
#include <ms0515/ScreenReader.hpp>
#include "Ui.hpp"
#include "Video.hpp"

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

using ms0515_frontend::Config;
using ms0515_frontend::CliArgs;
using ms0515_frontend::fdcUnitFor;
using ms0515_frontend::loadConfig;
using ms0515_frontend::saveConfig;
using ms0515_frontend::parseArgs;
using ms0515_frontend::findDefaultRom;
using ms0515_frontend::discoverRoms;
using ms0515_frontend::validateSingleSideImage;
using ms0515_frontend::validateDoubleSidedImage;
using ms0515_frontend::saveScreenshot;
using ms0515_frontend::initialDirFor;
using ms0515_frontend::timestampedPath;

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

    /* Apply persisted fullscreen preference.  Use BORDERLESS desktop
     * fullscreen (no resolution change, instant alt-tab) — exclusive
     * fullscreen mucks with virtual desktops and is hostile to a
     * retro display anyway.  The setFullscreen() helper is defined
     * later, near the main loop, where lastTickMs is in scope. */
    bool fullscreenOn = config.fullscreen;
    if (fullscreenOn)
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);

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

    /* Apply persisted keyboard typematic settings (auto game-mode + presets). */
    {
        auto &kbd = emu.keyboard();
        if (config.kbdAutoGameMode >= 0)
            kbd.auto_game_mode = (config.kbdAutoGameMode != 0);
        if (config.kbdTypingDelayMs >= 0)
            kbd.repeat_typing_delay_ms  = (uint32_t)config.kbdTypingDelayMs;
        if (config.kbdTypingPeriodMs >= 0)
            kbd.repeat_typing_period_ms = (uint32_t)config.kbdTypingPeriodMs;
        if (config.kbdGameDelayMs >= 0)
            kbd.repeat_game_delay_ms    = (uint32_t)config.kbdGameDelayMs;
        if (config.kbdGamePeriodMs >= 0)
            kbd.repeat_game_period_ms   = (uint32_t)config.kbdGamePeriodMs;
        /* Live values follow the typing preset until a heuristic flips us */
        kbd.repeat_delay_ms  = kbd.repeat_typing_delay_ms;
        kbd.repeat_period_ms = kbd.repeat_typing_period_ms;
    }

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

    /* Toggle host fullscreen and clean up after the SDL call.  Defined
     * here (not next to the initial SDL_SetWindowFullscreen call) so it
     * can capture lastTickMs / emuTimeAccumMs / frameTex by reference. */
    auto setFullscreen = [&](bool on) {
        SDL_SetWindowFullscreen(window,
            on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        fullscreenOn = on;
        if (config.fullscreen != on) {
            config.fullscreen = on;
            saveConfig(config);
        }
    };
    /* Known issue: toggling fullscreen during early BIOS POST (first
     * couple of seconds) wedges the emulator — the framebuffer freezes
     * on the RAM-test stripe pattern (or a white screen) and the BIOS
     * gets stuck in a keyboard-byte wait loop at PC=0o175630.  Several
     * defensive measures were tried and did not help: SDL_RENDER_*_RESET
     * event handling, unconditional SDL_Texture recreation, ImGui
     * SDLRenderer2 device-object recreation, emu.keyReleaseAll(),
     * resetting lastTickMs/emuTimeAccumMs.  Wait until the OS prompt is
     * up before pressing Alt+Enter as a workaround. */
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
    /* Track frontend hotkeys we've intercepted so the matching KEYUP is
     * also swallowed.  Without this, the guest sees "key-up without
     * key-down" for the held intercept (e.g. ESC up after exiting
     * fullscreen) — not a problem for ESC (unmapped) but Enter would
     * fire a stray ВК release. */
    bool swallowedReturnDown = false;
    bool swallowedEscDown    = false;

    while (!quit) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            if (ev.type == SDL_QUIT) {
                quit = true;
            } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
                bool intercepted = false;
                /* Frontend hotkeys.  Skip when ImGui has keyboard focus
                 * (text input fields, search boxes etc.). */
                if (!io.WantCaptureKeyboard) {
                    auto sc = ev.key.keysym.scancode;
                    bool altHeld = (ev.key.keysym.mod & KMOD_ALT) != 0;

                    if (ev.type == SDL_KEYDOWN) {
                        if (sc == SDL_SCANCODE_RETURN && altHeld) {
                            setFullscreen(!fullscreenOn);
                            swallowedReturnDown = true;
                            intercepted = true;
                        } else if (sc == SDL_SCANCODE_ESCAPE && fullscreenOn) {
                            setFullscreen(false);
                            swallowedEscDown = true;
                            intercepted = true;
                        }
                    } else /* KEYUP */ {
                        if (sc == SDL_SCANCODE_RETURN && swallowedReturnDown) {
                            swallowedReturnDown = false;
                            intercepted = true;
                        } else if (sc == SDL_SCANCODE_ESCAPE && swallowedEscDown) {
                            swallowedEscDown = false;
                            intercepted = true;
                        }
                    }
                }
                if (!intercepted)
                    physKbd.handleEvent(ev, emu, io.WantCaptureKeyboard);
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

        /* Main menu bar — hidden in fullscreen so the emulated screen
         * fills the host display.  Exit via ESC or Alt+Enter. */
        menuBarHeight = 0;
        if (!fullscreenOn && ImGui::BeginMainMenuBar()) {
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
                                    ms0515_frontend::FileDialogKind::Disk));
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
                                        saveConfig(config);
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
                                        ms0515_frontend::FileDialogKind::Disk));
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
                                            saveConfig(config);
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
                            initialDirFor(ms0515_frontend::FileDialogKind::Rom));
                        if (!p.empty() && p != currentRomPath &&
                            emu.loadRomFile(p))
                        {
                            currentRomPath = p;
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
                        initialDirFor(ms0515_frontend::FileDialogKind::State));
                    if (!path.empty()) {
                        if (auto r = emu.saveState(path); !r) {
                            std::fprintf(stderr, "Save state failed: %s\n",
                                         r.error().c_str());
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
                        initialDirFor(ms0515_frontend::FileDialogKind::State));
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
                ImGui::Separator();
                if (ImGui::BeginMenu("Keyboard")) {
                    auto &kbd = emu.keyboard();
                    bool dirty = false;

                    bool autoGame = kbd.auto_game_mode;
                    if (ImGui::MenuItem("Auto game-mode", nullptr, &autoGame)) {
                        kbd.auto_game_mode = autoGame;
                        config.kbdAutoGameMode = autoGame ? 1 : 0;
                        dirty = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "When ON, watch for 'click off' (0x99) from a "
                            "game and swap typematic delay/period to the "
                            "game preset.  When OFF, always typing preset.\n"
                            "Sliders below edit whichever preset is active.");
                    }
                    ImGui::Separator();

                    /* Snap helper: round `v` to the nearest multiple of
                     * `step` and clamp to [min, max].  ImGui::SliderInt
                     * has no built-in step parameter. */
                    auto snap = [](int v, int step, int min, int max) {
                        v = ((v + step / 2) / step) * step;
                        if (v < min) v = min;
                        if (v > max) v = max;
                        return v;
                    };

                    /* Single pair of sliders editing the *active* preset.
                     * Active = game preset iff auto-game-mode is on AND the
                     * heuristic has detected click-off; typing otherwise. */
                    bool active_is_game = kbd.auto_game_mode && kbd.in_game_mode;
                    ImGui::TextDisabled("Editing %s preset",
                                        active_is_game ? "game" : "typing");
                    uint32_t *p_delay  = active_is_game
                        ? &kbd.repeat_game_delay_ms  : &kbd.repeat_typing_delay_ms;
                    uint32_t *p_period = active_is_game
                        ? &kbd.repeat_game_period_ms : &kbd.repeat_typing_period_ms;

                    int delay_min  = active_is_game ? 50  : 250;
                    int delay_max  = active_is_game ? 500 : 1000;
                    int delay_step = active_is_game ? 25  : 250;

                    int d = (int)*p_delay;
                    int p = (int)*p_period;
                    if (ImGui::SliderInt("Delay (ms)", &d, delay_min, delay_max)) {
                        d = snap(d, delay_step, delay_min, delay_max);
                        *p_delay = (uint32_t)d;
                        if (active_is_game) config.kbdGameDelayMs   = d;
                        else                config.kbdTypingDelayMs = d;
                        dirty = true;
                    }
                    if (ImGui::SliderInt("Period (ms)", &p, 10, 100)) {
                        p = snap(p, 5, 10, 100);
                        *p_period = (uint32_t)p;
                        if (active_is_game) config.kbdGamePeriodMs   = p;
                        else                config.kbdTypingPeriodMs = p;
                        dirty = true;
                    }
                    ms7004_recompute_live_repeat(&kbd);

                    if (dirty) saveConfig(config);
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Fullscreen", "Alt+Enter", fullscreenOn))
                    setFullscreen(!fullscreenOn);
                ImGui::Separator();
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

        ms0515_frontend::drawScreenWindow(
            window, frameTex,
            screenContentW, screenContentH,
            menuBarHeight, fullscreenOn, showScreen);

        /* Place the debugger to the right of the screen window on first use. */
        if (showDebugger && !fullscreenOn) {
            int dbgX = 8 + (showScreen ? scrWinW + 8 : 0);
            ImGui::SetNextWindowPos(
                ImVec2((float)dbgX, (float)(menuBarHeight + 8)),
                ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(
                ImVec2((float)dbgWinW, (float)dbgWinH),
                ImGuiCond_FirstUseEver);
            ms0515_frontend::drawDebuggerWindow(dbg, running, romStatus, showDebugger);
        }

        /* On-screen MS7004 keyboard — initial position below the top row. */
        if (showKeyboard && !fullscreenOn) {
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
         * windows changes, sized to exactly contain them.  Skipped in
         * fullscreen — SDL owns the window size in that mode. */
        if (!fullscreenOn &&
            (showScreen   != prevShowScreen   ||
             showDebugger != prevShowDebugger ||
             showKeyboard != prevShowKeyboard)) {
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

        /* Status bar pinned to the bottom of the host window.  Hidden
         * in fullscreen so the emulated screen owns the display. */
        if (!fullscreenOn) {
            ms0515_frontend::StatusBarState sbs{
                window, emu, physKbd, running,
                speedDisplay, targetSpeed, fpsDisplay,
                emuFramesSinceReset, hostMsAtLastReset,
                mountedFd, mountedAsDs,
            };
            ms0515_frontend::drawStatusBar(sbs);
        }

        /* ── Render ────────────────────────────────────────────────── */
        /* The framebuffer is drawn via ImGui::Image inside the "Screen"
         * window, so all host-window content comes from ImGui.  Use
         * pure black in fullscreen so the letterbox bars disappear
         * into the bezel; standard chrome-grey otherwise. */
        if (fullscreenOn)
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        else
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
