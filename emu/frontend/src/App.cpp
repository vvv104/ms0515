#define _CRT_SECURE_NO_WARNINGS
#include "App.hpp"

#include "Assets.hpp"
#include "Platform.hpp"
#include "Ui.hpp"


#include <imgui.h>
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace ms0515_frontend {

/* ── Construction / destruction ─────────────────────────────────────── */

App::App(CliArgs cli) : cli_(std::move(cli)) {}

App::~App() { shutdown(); }

int App::run()
{
    if (!initSdl()) return 1;
    initImGui();
    initEmulator();
    initAudio();

    lastTickMs_         = SDL_GetTicks();
    hostMsAtLastReset_  = lastTickMs_;
    fpsWindowStartMs_   = lastTickMs_;
    prevShowScreen_     = !showScreen_;   /* force first-frame resize */
    prevShowDebugger_   = showDebugger_;
    prevShowKeyboard_   = showKeyboard_;
    prevShowTerminal_   = showTerminal_;

    quit_ = false;
    while (!quit_) {
        pumpEvents(quit_);
        tick();
        if (cli_.maxFrames > 0 && (int)frameCounter_ >= cli_.maxFrames)
            quit_ = true;
        renderFrame();
    }

    /* Persist UI toggles + host-mode flag on exit. */
    config_.showKeyboard = showKeyboard_;
    config_.showDebugger = showDebugger_;
    config_.hostMode     = physKbd_.hostMode();
    config_.save();
    return 0;
}

/* ── Init phases ────────────────────────────────────────────────────── */

bool App::initSdl()
{
    platformInit();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    /* Now that SDL is up (so SDL_GetBasePath works), load config and
     * fold its values into cli_ — CLI args take precedence, config
     * fills in anything left empty. */
    config_ = Config::load();
    for (int i = 0; i < 4; ++i)
        if (cli_.fdPath[i].empty() && !config_.fdPath[i].empty())
            cli_.fdPath[i] = config_.fdPath[i];
    for (int i = 0; i < 2; ++i)
        if (cli_.dsPath[i].empty() && !config_.dsPath[i].empty())
            cli_.dsPath[i] = config_.dsPath[i];
    if (cli_.romPath.empty() && !config_.romPath.empty())
        cli_.romPath = config_.romPath;
    if (cli_.romPath.empty())
        cli_.romPath = findDefaultRom();
    showDebugger_ = config_.showDebugger;
    showKeyboard_ = config_.showKeyboard;

    constexpr int scale       = 1;
    const int     winWidth    = kScreenWidth  * scale + 20;
    const int     winHeight   = kScreenHeight * scale + 100;
    window_ = SDL_CreateWindow(
        "MS 0515 Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        winWidth, winHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    /* Apply persisted fullscreen preference.  SDL_WINDOW_FULLSCREEN_DESKTOP
     * = borderless, no resolution change, instant alt-tab. */
    fullscreenOn_ = config_.fullscreen;
    if (fullscreenOn_)
        SDL_SetWindowFullscreen(window_, SDL_WINDOW_FULLSCREEN_DESKTOP);

    renderer_ = SDL_CreateRenderer(
        window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }
    frameTex_ = SDL_CreateTexture(
        renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
        kScreenWidth, kScreenHeight);
    return true;
}

void App::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    /* Cyrillic + Latin + arrow-glyph font.  ImGui's default is ASCII-only. */
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;
    static const ImWchar rangesMain[] = {
        0x0020, 0x00FF,   /* Latin + Latin-1 */
        0x0400, 0x04FF,   /* Cyrillic */
        0x2190, 0x2199,   /* Arrows ← ↑ → ↓ ↖ ↗ ↘ ↙ */
        0x2580, 0x259F,   /* Block elements (█ ▌ ▀ …) — Terminal
                             window's unknown-glyph marker is U+2588 */
        0,
    };
    for (const auto &path : systemFontCandidates()) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f, &cfg, rangesMain);
            break;
        }
    }
    /* Symbol fallback for diagonal arrows that some primary fonts omit. */
    ImFontConfig sym;
    sym.MergeMode   = true;
    sym.OversampleH = 2;
    sym.OversampleV = 2;
    static const ImWchar rangesSym[] = { 0x2190, 0x2199, 0 };
    for (const auto &path : symbolFontCandidates()) {
        if (std::filesystem::exists(path)) {
            io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f, &sym, rangesSym);
            break;
        }
    }

    /* Separate monospace font for the Terminal window — the OS draws on
     * a fixed 80-column grid, so the host-side mirror needs a fixed-pitch
     * face for columns to line up.  Reuses the same Latin + Cyrillic
     * range as the proportional UI font. */
    ImFontConfig mono;
    mono.OversampleH = 2;
    mono.OversampleV = 2;
    for (const auto &path : monoFontCandidates()) {
        if (std::filesystem::exists(path)) {
            terminalFont_ = io.Fonts->AddFontFromFileTTF(
                path.c_str(), 15.0f, &mono, rangesMain);
            break;
        }
    }

    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);
}

void App::initEmulator()
{
    /* ROM. */
    currentRomPath_ = cli_.romPath;
    availableRoms_  = discoverRoms();
    if (currentRomPath_.empty()) {
        romStatus_ = "ROM: <not found - pass --rom <path>>";
        std::fprintf(stderr, "%s\n", romStatus_.c_str());
    } else if (!emu_.loadRomFile(currentRomPath_)) {
        romStatus_ = "ROM: FAILED to load '" + currentRomPath_ + "'";
        std::fprintf(stderr, "%s\n", romStatus_.c_str());
    } else {
        romStatus_ = "ROM: " + currentRomPath_;
    }

    mountInitialDisks();
    config_.save();
    emu_.enableRamDisk();

    /* History ring + watchpoints — diagnostic features, routed through
     * the Debugger so the public Emulator API stays free of these.
     * CLI overrides config. */
    int histSize = cli_.historySize >= 0 ? cli_.historySize : config_.historySize;
    if (histSize > 0)
        dbg_.enableHistory(static_cast<std::size_t>(histSize));
    int watchAddr = cli_.historyWatchAddr >= 0
                  ? cli_.historyWatchAddr : config_.historyWatchAddr;
    int watchLen  = cli_.historyWatchLen  >= 0
                  ? cli_.historyWatchLen  : config_.historyWatchLen;
    if (watchLen > 0)
        dbg_.setMemoryWatch((uint16_t)watchAddr, (uint16_t)watchLen);
    int rwAddr = cli_.historyReadWatchAddr >= 0
               ? cli_.historyReadWatchAddr : config_.historyReadWatchAddr;
    int rwLen  = cli_.historyReadWatchLen  >= 0
               ? cli_.historyReadWatchLen  : config_.historyReadWatchLen;
    if (rwLen > 0)
        dbg_.setReadWatch((uint16_t)rwAddr, (uint16_t)rwLen);

    emu_.reset();
    applyKeyboardConfig();

    osk_.loadLayout();
    physKbd_.setHostMode(config_.hostMode);
    refreshMountStateFromCli();
}

void App::mountInitialDisks()
{
    /* CLI / config disk mounts.  Errors are reported on stderr but
     * don't abort startup — the user can mount via the menu. */
    for (int drive = 0; drive < 2; ++drive) {
        const bool wantDs    = !cli_.dsPath[drive].empty();
        const bool wantSide0 = !cli_.fdPath[fdcUnitFor(drive, 0)].empty();
        const bool wantSide1 = !cli_.fdPath[fdcUnitFor(drive, 1)].empty();

        if (wantDs && (wantSide0 || wantSide1)) {
            std::fprintf(stderr,
                "error: --disk%d (-d%d) is mutually exclusive with "
                "--disk%d-sideN (-d%dsN); pick one.  Skipping drive %d.\n",
                drive, drive, drive, drive, drive);
            continue;
        }
        if (wantDs) {
            if (auto err = validateDoubleSidedImage(cli_.dsPath[drive])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d (double-sided): %s\n",
                    drive, err->c_str());
                continue;
            }
            int u0 = fdcUnitFor(drive, 0), u1 = fdcUnitFor(drive, 1);
            bool ok0 = emu_.mountDisk(u0, cli_.dsPath[drive]);
            bool ok1 = emu_.mountDisk(u1, cli_.dsPath[drive]);
            if (ok0 && ok1) {
                config_.dsPath[drive] = cli_.dsPath[drive];
                config_.fdPath[u0].clear();
                config_.fdPath[u1].clear();
            } else {
                std::fprintf(stderr,
                    "error: failed to mount double-sided '%s' on drive %d\n",
                    cli_.dsPath[drive].c_str(), drive);
                if (ok0) emu_.unmountDisk(u0);
                if (ok1) emu_.unmountDisk(u1);
            }
            continue;
        }
        for (int side = 0; side < 2; ++side) {
            int unit = fdcUnitFor(drive, side);
            if (cli_.fdPath[unit].empty()) continue;
            if (auto err = validateSingleSideImage(cli_.fdPath[unit])) {
                std::fprintf(stderr,
                    "error: cannot mount disk %d side %d: %s\n",
                    drive, side, err->c_str());
                continue;
            }
            if (emu_.mountDisk(unit, cli_.fdPath[unit])) {
                config_.fdPath[unit] = cli_.fdPath[unit];
            } else {
                std::fprintf(stderr,
                    "error: failed to mount '%s' on disk %d side %d\n",
                    cli_.fdPath[unit].c_str(), drive, side);
            }
        }
    }
}

void App::applyKeyboardConfig()
{
    auto s = emu_.keyboardSettings();
    if (config_.kbdAutoGameMode    >= 0) s.autoGameMode    = (config_.kbdAutoGameMode != 0);
    if (config_.kbdTypingDelayMs   >= 0) s.typingDelayMs   = (uint32_t)config_.kbdTypingDelayMs;
    if (config_.kbdTypingPeriodMs  >= 0) s.typingPeriodMs  = (uint32_t)config_.kbdTypingPeriodMs;
    if (config_.kbdGameDelayMs     >= 0) s.gameDelayMs     = (uint32_t)config_.kbdGameDelayMs;
    if (config_.kbdGamePeriodMs    >= 0) s.gamePeriodMs    = (uint32_t)config_.kbdGamePeriodMs;
    emu_.applyKeyboardConfig(s);
}

void App::initAudio()
{
    if (!audio_.init())
        std::fprintf(stderr, "warning: audio init failed, continuing without sound\n");
    emu_.setSoundCallback([this](int value) {
        audio_.addTransition(emu_.frameCyclePos(), value);
    });
}

void App::shutdown()
{
    audio_.shutdown();

    if (frameTex_)  { SDL_DestroyTexture(frameTex_);   frameTex_  = nullptr; }
    if (renderer_)  { SDL_DestroyRenderer(renderer_);  renderer_  = nullptr; }
    if (window_) {
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
    }
}

/* ── Mount / unmount actions ────────────────────────────────────────── */

void App::refreshMountStateFromCli()
{
    for (int drive = 0; drive < 2; ++drive) {
        int u0 = fdcUnitFor(drive, 0), u1 = fdcUnitFor(drive, 1);
        if (!cli_.dsPath[drive].empty()) {
            mountedFd_[u0]    = cli_.dsPath[drive];
            mountedFd_[u1]    = cli_.dsPath[drive];
            mountedAsDs_[drive] = true;
        } else {
            mountedFd_[u0]    = cli_.fdPath[u0];
            mountedFd_[u1]    = cli_.fdPath[u1];
            mountedAsDs_[drive] = false;
        }
    }
}

void App::detachDsRemnant(int keepUnit)
{
    /* When a drive currently mounted as DS gets a single-side mount on
     * one slot, drop the partner so the same DS image isn't still
     * pretending to live on the other side. */
    int drive = (keepUnit & 1);    /* unit % 2 == drive (0 or 1) */
    if (!mountedAsDs_[drive]) return;
    int other = fdcUnitFor(drive, (keepUnit >= 2) ? 0 : 1);
    emu_.unmountDisk(other);
    mountedFd_[other].clear();
    mountedAsDs_[drive] = false;
    config_.dsPath[drive].clear();
}

void App::mountDoubleSided(int drive, const std::string &path)
{
    if (auto err = validateDoubleSidedImage(path)) {
        mountErrorMessage_ = std::format(
            "Cannot mount disk {}:\n\n{}", drive, *err);
        mountErrorPending_ = true;
        return;
    }
    int u0 = fdcUnitFor(drive, 0), u1 = fdcUnitFor(drive, 1);
    emu_.unmountDisk(u0);
    emu_.unmountDisk(u1);
    bool ok = emu_.mountDisk(u0, path) && emu_.mountDisk(u1, path);
    if (!ok) {
        emu_.unmountDisk(u0);
        emu_.unmountDisk(u1);
        mountErrorMessage_ = std::format(
            "Failed to mount '{}' on disk {}.", path, drive);
        mountErrorPending_ = true;
        return;
    }
    mountedFd_[u0]    = path;
    mountedFd_[u1]    = path;
    mountedAsDs_[drive] = true;
    config_.dsPath[drive]   = path;
    config_.fdPath[u0].clear();
    config_.fdPath[u1].clear();
    config_.save();
}

void App::mountSingleSide(int unit, const std::string &path)
{
    if (auto err = validateSingleSideImage(path)) {
        int drive = unit & 1;
        int side  = unit >= 2 ? 1 : 0;
        mountErrorMessage_ = std::format(
            "Cannot mount disk {} side {}:\n\n{}", drive, side, *err);
        mountErrorPending_ = true;
        return;
    }
    detachDsRemnant(unit);
    emu_.unmountDisk(unit);
    if (!emu_.mountDisk(unit, path)) {
        int drive = unit & 1;
        int side  = unit >= 2 ? 1 : 0;
        mountErrorMessage_ = std::format(
            "Failed to mount '{}' on disk {} side {}.", path, drive, side);
        mountErrorPending_ = true;
        return;
    }
    mountedFd_[unit]   = path;
    config_.fdPath[unit] = path;
    config_.save();
}

void App::unmountDrive(int drive)
{
    int u0 = fdcUnitFor(drive, 0), u1 = fdcUnitFor(drive, 1);
    emu_.unmountDisk(u0);
    emu_.unmountDisk(u1);
    mountedFd_[u0].clear();
    mountedFd_[u1].clear();
    mountedAsDs_[drive] = false;
    config_.dsPath[drive].clear();
    config_.fdPath[u0].clear();
    config_.fdPath[u1].clear();
    config_.save();
}

void App::unmountUnit(int unit)
{
    int drive = unit & 1;
    if (mountedAsDs_[drive]) {
        unmountDrive(drive);
        return;
    }
    emu_.unmountDisk(unit);
    mountedFd_[unit].clear();
    config_.fdPath[unit].clear();
    config_.save();
}

/* ── Misc helpers ───────────────────────────────────────────────────── */

void App::loadRom(const std::string &path)
{
    if (path.empty() || path == currentRomPath_) return;
    if (!emu_.loadRomFile(path)) {
        romStatus_ = "ROM: FAILED to load '" + path + "'";
        return;
    }
    currentRomPath_     = path;
    romStatus_          = "ROM: " + currentRomPath_;
    config_.romPath     = currentRomPath_;
    config_.save();
    dbg_.reset();
    running_              = true;
    emuTimeAccumMs_       = 0.0f;
    emuFramesSinceReset_  = 0;
    hostMsAtLastReset_    = SDL_GetTicks();
}

void App::setFullscreen(bool on)
{
    SDL_SetWindowFullscreen(window_,
        on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    fullscreenOn_ = on;
    if (config_.fullscreen != on) {
        config_.fullscreen = on;
        config_.save();
    }
}

/* ── Main-loop phases ───────────────────────────────────────────────── */

bool App::handleHotkey(const SDL_Event &ev)
{
    auto sc = ev.key.keysym.scancode;
    bool altHeld = (ev.key.keysym.mod & KMOD_ALT) != 0;
    if (ev.type == SDL_KEYDOWN) {
        if (sc == SDL_SCANCODE_RETURN && altHeld) {
            setFullscreen(!fullscreenOn_);
            swallowedReturnDown_ = true;
            return true;
        }
        if (sc == SDL_SCANCODE_ESCAPE && fullscreenOn_) {
            setFullscreen(false);
            swallowedEscDown_ = true;
            return true;
        }
    } else /* SDL_KEYUP */ {
        if (sc == SDL_SCANCODE_RETURN && swallowedReturnDown_) {
            swallowedReturnDown_ = false;
            return true;
        }
        if (sc == SDL_SCANCODE_ESCAPE && swallowedEscDown_) {
            swallowedEscDown_ = false;
            return true;
        }
    }
    return false;
}

void App::pumpEvents(bool &quit)
{
    SDL_Event ev;
    ImGuiIO &io = ImGui::GetIO();
    while (SDL_PollEvent(&ev)) {
        ImGui_ImplSDL2_ProcessEvent(&ev);
        if (ev.type == SDL_QUIT) {
            quit = true;
        } else if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
            bool intercepted = !io.WantCaptureKeyboard && handleHotkey(ev);
            if (!intercepted)
                physKbd_.handleEvent(ev, emu_, io.WantCaptureKeyboard);
        }
    }
}

void App::tick()
{
    /* Auto-repeat timer always advances. */
    emu_.keyTick(SDL_GetTicks());

    /* Run emulated frames based on real elapsed time × speed factor. */
    if (running_) {
        uint32_t nowTick = SDL_GetTicks();
        float realDeltaMs = static_cast<float>(nowTick - lastTickMs_);
        if (realDeltaMs > 200.0f) realDeltaMs = 200.0f;  /* clamp burst */
        emuTimeAccumMs_ += realDeltaMs * (targetSpeed_ / 100.0f);
        lastTickMs_ = nowTick;

        bool audioEnabled = audioOn_ && (targetSpeed_ == 100.0f);
        while (emuTimeAccumMs_ >= kFrameMs && running_) {
            if (audioEnabled) audio_.beginFrame();
            bool ok = emu_.stepFrame();
            if (audioEnabled) audio_.endFrame(emu_.frameCyclePos());
            if (!ok) {
                running_ = false;
                emuTimeAccumMs_ = 0.0f;
                break;
            }
            emuTimeAccumMs_ -= kFrameMs;
            ++emuFramesSinceReset_;
            ++emuFramesInWindow_;
            /* Skip sampling while the CPU is inside the BIOS scroll
             * routines.  Disasm of ROM-A located two address ranges
             * that perform mass VRAM copy:
             *
             *   - 0o165340–0o165502: scroll-up-by-1-cell-row (entry
             *     from 0o165324 when row counter reaches 030).
             *     Copies 1920 bytes from VRAM+1200 to VRAM+0
             *     (8 scanlines × 80 bytes × 24 cell rows of source),
             *     then clears the new bottom row.
             *
             *   - 0o167160–0o167334: coordinate-based scroll variant
             *     used by the OS terminal driver when scrolling a
             *     specific region.  Same MOV (R5)+,(R4)+ pattern.
             *
             * Sampling mid-routine catches partial states where some
             * cells have been copied but others haven't — exactly the
             * "      .SYS     2P 27-12-90" mid-scroll-copy garbage.
             * Skip those frames; the next emu frame after the routine
             * exits will be a clean post-scroll state.
             *
             * Address ranges are ROM-A specific.  ROM-B / different
             * OSes that ship their own scroll in RAM still benefit
             * from the Terminal::feedSample gates (clean/progressing/
             * no-adjacent-duplicate) as a fallback. */
            const uint16_t pc = emu_.pc();
            if ((pc >= 0165340u && pc <= 0165502u) ||
                (pc >= 0167160u && pc <= 0167334u))
                continue;

            terminal_.update(emu_);
        }
    } else {
        lastTickMs_     = SDL_GetTicks();
        emuTimeAccumMs_ = 0.0f;
    }

    /* Sliding 1-second speed measurement window. */
    uint32_t nowMs = SDL_GetTicks();
    uint32_t winMs = nowMs - fpsWindowStartMs_;
    if (winMs >= 1000) {
        float emuFps = emuFramesInWindow_ * 1000.0f / static_cast<float>(winMs);
        fpsDisplay_   = emuFps;
        speedDisplay_ = emuFps / 50.0f * 100.0f;
        emuFramesInWindow_ = 0;
        fpsWindowStartMs_  = nowMs;
    }
}

void App::renderFrame()
{
    /* ── Video decode + texture upload ─── */
    ++frameCounter_;
    video_.render(emu_, frameCounter_);
    SDL_UpdateTexture(frameTex_, nullptr, video_.pixels(), kScreenWidth * 4);

    /* CLI / hotkey screenshot. */
    if (!cli_.screenshotPath.empty() &&
        (int)frameCounter_ ==
            (cli_.screenshotFrame > 0 ? cli_.screenshotFrame : cli_.maxFrames))
        saveScreenshot(video_, cli_.screenshotPath);
    if (wantScreenshot_) {
        saveScreenshot(video_, {});
        wantScreenshot_ = false;
    }

    /* ── ImGui frame ─── */
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawMenuBar();
    drawMountErrorPopup();

    /* Layout math used by both the auto-resize and child-window
     * positioning helpers below. */
    const ImGuiStyle &imstyle = ImGui::GetStyle();
    const float titleBarH =
        ImGui::GetFontSize() + imstyle.FramePadding.y * 2.0f;
    const int screenContentW = kScreenWidth;
    const int screenContentH = kScreenHeight;
    const int scrWinW = screenContentW + (int)(imstyle.WindowPadding.x * 2.0f) + 2;
    const int scrWinH = screenContentH + (int)(imstyle.WindowPadding.y * 2.0f + titleBarH) + 2;
    const int dbgWinW  = 380;
    const int dbgWinH  = std::max(scrWinH, 360);
    const int termWinW = 690;
    const int termWinH = scrWinH;
    const int oskWinW  = (int)osk_.pixelWidth();
    const int oskWinH  = (int)osk_.pixelHeight();

    drawScreenWindow(window_, frameTex_, screenContentW, screenContentH,
                     menuBarHeight_, fullscreenOn_, showScreen_);

    if (showDebugger_ && !fullscreenOn_) {
        int dbgX = 8 + (showScreen_ ? scrWinW + 8 : 0);
        ImGui::SetNextWindowPos(
            ImVec2((float)dbgX, (float)(menuBarHeight_ + 8)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2((float)dbgWinW, (float)dbgWinH),
            ImGuiCond_FirstUseEver);
        drawDebuggerWindow(dbg_, running_, romStatus_, showDebugger_);
    }
    if (showKeyboard_ && !fullscreenOn_) {
        int topRowH = std::max({showScreen_   ? scrWinH  : 0,
                                showDebugger_ ? dbgWinH  : 0,
                                showTerminal_ ? termWinH : 0});
        ImGui::SetNextWindowPos(
            ImVec2(8.0f, (float)(menuBarHeight_ + 8 + topRowH + 8)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2((float)oskWinW, (float)oskWinH),
            ImGuiCond_FirstUseEver);
        osk_.draw(emu_, showKeyboard_);
    }

    if (showTerminal_ && !fullscreenOn_) {
        int termX = 8 + (showScreen_   ? scrWinW + 8 : 0)
                      + (showDebugger_ ? dbgWinW + 8 : 0);
        ImGui::SetNextWindowPos(
            ImVec2((float)termX, (float)(menuBarHeight_ + 8)),
            ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2((float)termWinW, (float)termWinH),
            ImGuiCond_FirstUseEver);
        drawTerminalWindow(terminal_, showTerminal_, terminalFont_);
    }

    if (!fullscreenOn_) resizeHostWindow();

    if (!fullscreenOn_) {
        StatusBarState sbs{
            window_, emu_, physKbd_, running_,
            speedDisplay_, targetSpeed_, fpsDisplay_,
            emuFramesSinceReset_, hostMsAtLastReset_,
            mountedFd_.data(), mountedAsDs_.data(),
        };
        drawStatusBar(sbs);
    }

    if (fullscreenOn_) SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    else               SDL_SetRenderDrawColor(renderer_, 40, 40, 48, 255);
    SDL_RenderClear(renderer_);
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

void App::resizeHostWindow()
{
    /* Resize the SDL host window whenever the set of visible inner
     * windows changes, sized to exactly contain them. */
    if (showScreen_   == prevShowScreen_   &&
        showDebugger_ == prevShowDebugger_ &&
        showKeyboard_ == prevShowKeyboard_ &&
        showTerminal_ == prevShowTerminal_) return;

    const ImGuiStyle &imstyle = ImGui::GetStyle();
    const float titleBarH = ImGui::GetFontSize() + imstyle.FramePadding.y * 2.0f;
    const int scrWinW  = kScreenWidth  + (int)(imstyle.WindowPadding.x * 2.0f) + 2;
    const int scrWinH  = kScreenHeight + (int)(imstyle.WindowPadding.y * 2.0f + titleBarH) + 2;
    const int dbgWinW  = 380;
    const int dbgWinH  = std::max(scrWinH, 360);
    const int termWinW = 690;
    const int termWinH = scrWinH;
    const int oskWinW  = (int)osk_.pixelWidth();
    const int oskWinH  = (int)osk_.pixelHeight();
    const int statusBarH = (int)(ImGui::GetTextLineHeightWithSpacing() * 2.0f
                                 + imstyle.WindowPadding.y * 2.0f + 2.0f);
    int topRowW = 8
                + (showScreen_   ? scrWinW  + 8 : 0)
                + (showDebugger_ ? dbgWinW  + 8 : 0)
                + (showTerminal_ ? termWinW + 8 : 0);
    if (!showScreen_ && !showDebugger_ && !showTerminal_) topRowW = 16;
    int topRowH = std::max({showScreen_   ? scrWinH  : 0,
                            showDebugger_ ? dbgWinH  : 0,
                            showTerminal_ ? termWinH : 0});
    int totalW  = std::max(topRowW, showKeyboard_ ? oskWinW + 16 : 0);
    if (totalW < 320) totalW = 320;
    int totalH  = menuBarHeight_ + 8 + topRowH
                + (showKeyboard_ ? oskWinH + 16 : 0)
                + statusBarH + 8;
    if (totalH < 200) totalH = 200;
    SDL_SetWindowSize(window_, totalW, totalH);
    prevShowScreen_   = showScreen_;
    prevShowDebugger_ = showDebugger_;
    prevShowKeyboard_ = showKeyboard_;
    prevShowTerminal_ = showTerminal_;
}

/* ── Menu rendering ─────────────────────────────────────────────────── */

void App::drawMenuBar()
{
    menuBarHeight_ = 0;
    if (fullscreenOn_) return;     /* in fullscreen the screen owns the display */
    if (!ImGui::BeginMainMenuBar()) return;

    menuBarHeight_ = (int)ImGui::GetWindowSize().y;
    drawFileMenu();
    drawMachineMenu();
    drawComponentsMenu();
    drawViewMenu();
    ImGui::EndMainMenuBar();
}

void App::drawFileMenu()
{
    if (!ImGui::BeginMenu("File")) return;

    /* Per-drive submenus first.  The pattern is the same for both
     * drives, so factor through drawFileDiskMenu(drive). */
    for (int drive = 0; drive < 2; ++drive)
        drawFileDiskMenu(drive);

    ImGui::Separator();
    if (ImGui::MenuItem("Screenshot")) requestScreenshot();
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4")) quit_ = true;
    ImGui::EndMenu();
}

void App::drawFileDiskMenu(int drive)
{
    int unit0 = fdcUnitFor(drive, 0);
    int unit1 = fdcUnitFor(drive, 1);
    const bool side0 = !mountedFd_[unit0].empty();
    const bool side1 = !mountedFd_[unit1].empty();
    const bool any   = side0 || side1;

    /* Compact summary for the top-level label: how the drive is
     * currently driven, not which file. */
    const char *summary;
    if      (!any)                  summary = "empty";
    else if (mountedAsDs_[drive])   summary = "image";
    else if (side0 && side1)        summary = "both sides";
    else if (side0)                 summary = "0 side";
    else                            summary = "1 side";
    auto driveLabel = std::format("Disk {}: {}", drive, summary);
    if (!ImGui::BeginMenu(driveLabel.c_str())) return;

    /* Show the currently mounted DS file alongside "Mount image" so
     * the user can see what's there at a glance. */
    std::string mountImageLabel = "Mount image...";
    if (mountedAsDs_[drive]) {
        mountImageLabel += "    [" +
            std::filesystem::path(mountedFd_[unit0]).filename().string() + "]";
    }
    if (ImGui::MenuItem(mountImageLabel.c_str())) {
        auto title = std::format(
            "Select double-sided image for drive {}", drive);
        std::string p = openFileDialog(
            window_, title.c_str(),
            FileDialogKind::Disk, Paths::initialDirFor(FileDialogKind::Disk));
        if (!p.empty()) mountDoubleSided(drive, p);
    }

    for (int side = 0; side < 2; ++side) {
        int unit       = (side == 0) ? unit0 : unit1;
        int otherUnit  = (side == 0) ? unit1 : unit0;
        std::string label = std::format("Mount side {}...", side);
        if (!mountedAsDs_[drive]) {
            if (!mountedFd_[unit].empty()) {
                label += "    [" +
                    std::filesystem::path(mountedFd_[unit])
                        .filename().string() + "]";
            } else if (!mountedFd_[otherUnit].empty()) {
                label += "    [empty]";
            }
        }
        if (ImGui::MenuItem(label.c_str())) {
            auto title = std::format(
                "Select single-side image for disk {} side {}", drive, side);
            std::string p = openFileDialog(
                window_, title.c_str(),
                FileDialogKind::Disk, Paths::initialDirFor(FileDialogKind::Disk));
            if (!p.empty()) mountSingleSide(unit, p);
        }
    }
    if (ImGui::MenuItem("Unmount", nullptr, false, any))
        unmountDrive(drive);
    ImGui::EndMenu();
}

void App::drawMachineMenu()
{
    if (!ImGui::BeginMenu("Machine")) return;

    if (ImGui::MenuItem("Reset")) {
        dbg_.reset();
        /* If the emulator paused itself because the CPU halted, Reset
         * also clears the paused state — otherwise stepFrame never
         * runs again and the screen stays frozen on pre-reset VRAM. */
        running_              = true;
        emuTimeAccumMs_       = 0.0f;
        emuFramesSinceReset_  = 0;
        hostMsAtLastReset_    = SDL_GetTicks();
    }
    drawRomSubmenu();

    ImGui::Separator();
    if (ImGui::MenuItem(running_ ? "Pause" : "Resume"))
        running_ = !running_;
    drawSpeedSubmenu();
    bool speedIs100 = (targetSpeed_ == 100.0f);
    if (ImGui::MenuItem("Audio", nullptr, audioOn_, speedIs100))
        audioOn_ = !audioOn_;

    ImGui::Separator();
    if (ImGui::MenuItem("Save State...")) {
        bool wasRunning = running_;
        running_ = false;
        std::string path = saveFileDialog(
            window_, "Save State", "state.ms0515",
            FileDialogKind::State, Paths::initialDirFor(FileDialogKind::State));
        if (!path.empty())
            if (auto r = emu_.saveState(path); !r)
                std::fprintf(stderr, "Save state failed: %s\n", r.error().c_str());
        running_ = wasRunning;
    }
    if (ImGui::MenuItem("Load State...")) {
        bool wasRunning = running_;
        running_ = false;
        std::string path = openFileDialog(
            window_, "Load State", FileDialogKind::State,
            Paths::initialDirFor(FileDialogKind::State));
        if (!path.empty()) {
            if (auto r = emu_.loadState(path); !r) {
                std::fprintf(stderr, "Load state failed: %s\n", r.error().c_str());
            } else {
                for (int i = 0; i < 4; ++i) {
                    mountedFd_[i] = emu_.diskPath(i);
                    config_.fdPath[i] = mountedFd_[i];
                }
                config_.save();
            }
        }
        running_ = wasRunning;
    }
    ImGui::EndMenu();
}

void App::drawRomSubmenu()
{
    if (!ImGui::BeginMenu("ROM")) return;
    for (const auto &romPath : availableRoms_) {
        std::string label =
            std::filesystem::path(romPath).filename().string();
        bool selected = (romPath == currentRomPath_);
        if (ImGui::MenuItem(label.c_str(), nullptr, selected))
            if (!selected) loadRom(romPath);
    }
    if (availableRoms_.empty())
        ImGui::MenuItem("(no ROMs found)", nullptr, false, false);
    ImGui::Separator();
    if (ImGui::MenuItem("Browse...")) {
        std::string p = openFileDialog(
            window_, "Select ROM", FileDialogKind::Rom,
            Paths::initialDirFor(FileDialogKind::Rom));
        loadRom(p);
    }
    ImGui::EndMenu();
}

void App::drawSpeedSubmenu()
{
    auto label = std::format("Speed: {:.0f}%", targetSpeed_);
    if (!ImGui::BeginMenu(label.c_str())) return;
    constexpr float presets[] = {20, 50, 100, 200, 500};
    for (float p : presets) {
        auto presetLabel = std::format("{:.0f}%", p);
        if (ImGui::MenuItem(presetLabel.c_str(), nullptr, targetSpeed_ == p)) {
            targetSpeed_    = p;
            emuTimeAccumMs_ = 0.0f;
        }
    }
    ImGui::Separator();
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::SliderFloat("##speed", &targetSpeed_, 20.0f, 500.0f, "%.0f%%"))
        emuTimeAccumMs_ = 0.0f;
    ImGui::EndMenu();
}

void App::drawComponentsMenu()
{
    if (!ImGui::BeginMenu("Components")) return;
    if (ImGui::MenuItem("512 KB RAM disk (EX0:)", nullptr, ramDiskOn_)) {
        /* Toggle is visual only until core supports disabling. */
        if (!ramDiskOn_) { emu_.enableRamDisk(); ramDiskOn_ = true; }
    }
    ImGui::Separator();
    drawKeyboardSubmenu();
    ImGui::EndMenu();
}

void App::drawKeyboardSubmenu()
{
    if (!ImGui::BeginMenu("Keyboard")) return;
    auto    settings = emu_.keyboardSettings();
    bool    dirty    = false;

    bool autoGame = settings.autoGameMode;
    if (ImGui::MenuItem("Auto game-mode", nullptr, &autoGame)) {
        settings.autoGameMode    = autoGame;
        config_.kbdAutoGameMode  = autoGame ? 1 : 0;
        dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "When ON, watch for 'click off' (0x99) from a game and swap "
            "typematic delay/period to the game preset.  When OFF, always "
            "typing preset.\nSliders below edit whichever preset is active.");
    }
    ImGui::Separator();

    /* Snap helper: round v to nearest multiple of step, clamp to [min,max]. */
    auto snap = [](int v, int step, int min, int max) {
        v = ((v + step / 2) / step) * step;
        if (v < min) v = min;
        if (v > max) v = max;
        return v;
    };

    const bool active_is_game = settings.autoGameMode
                             && emu_.keyboardInGameMode();
    ImGui::TextDisabled("Editing %s preset",
                        active_is_game ? "game" : "typing");
    uint32_t *p_delay  = active_is_game
        ? &settings.gameDelayMs  : &settings.typingDelayMs;
    uint32_t *p_period = active_is_game
        ? &settings.gamePeriodMs : &settings.typingPeriodMs;
    int delay_min  = active_is_game ? 50  : 250;
    int delay_max  = active_is_game ? 500 : 1000;
    int delay_step = active_is_game ? 25  : 250;

    int d = (int)*p_delay;
    int p = (int)*p_period;
    if (ImGui::SliderInt("Delay (ms)", &d, delay_min, delay_max)) {
        d = snap(d, delay_step, delay_min, delay_max);
        *p_delay = (uint32_t)d;
        if (active_is_game) config_.kbdGameDelayMs   = d;
        else                config_.kbdTypingDelayMs = d;
        dirty = true;
    }
    if (ImGui::SliderInt("Period (ms)", &p, 10, 100)) {
        p = snap(p, 5, 10, 100);
        *p_period = (uint32_t)p;
        if (active_is_game) config_.kbdGamePeriodMs   = p;
        else                config_.kbdTypingPeriodMs = p;
        dirty = true;
    }
    if (dirty) {
        emu_.applyKeyboardConfig(settings);
        config_.save();
    }
    ImGui::EndMenu();
}

void App::drawViewMenu()
{
    if (!ImGui::BeginMenu("View")) return;
    if (ImGui::MenuItem("Fullscreen", "Alt+Enter", fullscreenOn_))
        setFullscreen(!fullscreenOn_);
    ImGui::Separator();
    ImGui::MenuItem("Screen",             nullptr, &showScreen_);
    ImGui::MenuItem("Debugger",           nullptr, &showDebugger_);
    ImGui::MenuItem("On-screen keyboard", nullptr, &showKeyboard_);
    ImGui::MenuItem("Terminal",           nullptr, &showTerminal_);
    ImGui::EndMenu();
}

void App::drawMountErrorPopup()
{
    if (mountErrorPending_) {
        ImGui::OpenPopup("Disk mount error");
        mountErrorPending_ = false;
    }
    /* Anchor the popup to the viewport centre — without this ImGui
     * places it wherever it last was, which is unpredictable. */
    ImVec2 vpCentre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(vpCentre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(400, 0), ImVec2(400, FLT_MAX));
    if (!ImGui::BeginPopupModal("Disk mount error", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize |
                                ImGuiWindowFlags_NoResize)) return;

    /* Split message: headline (left-aligned, wrapped) + detail
     * (centred, paragraph-by-paragraph word-wrap). */
    const std::string &msg = mountErrorMessage_;
    std::size_t splitAt = msg.find('\n');
    std::string headline = (splitAt == std::string::npos)
                         ? msg : msg.substr(0, splitAt);
    std::string detail   = (splitAt == std::string::npos)
                         ? std::string{} : msg.substr(splitAt + 1);
    while (!detail.empty() && detail.front() == '\n')
        detail.erase(detail.begin());

    ImGui::PushTextWrapPos(0.0f);
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
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
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
        mountErrorMessage_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} /* namespace ms0515_frontend */
