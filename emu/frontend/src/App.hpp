/*
 * App.hpp — frontend application object.
 *
 * Owns SDL/ImGui resources, the emulator + debugger, all UI state
 * (mounted disks, ROM path, view toggles, FPS counters, fullscreen),
 * and the main loop.  Constructed from a parsed CliArgs; main()
 * delegates to App::run() and forwards its exit code.
 *
 * The functional split inside this class follows one rule: no method
 * may exceed ~100 lines.  Menus are sliced by top-level item, mount
 * actions by intent, render passes by phase.
 */
#pragma once

#include "Audio.hpp"
#include "Cli.hpp"
#include "Config.hpp"
#include "OnScreenKeyboard.hpp"
#include "PhysicalKeyboard.hpp"
#include "Video.hpp"

#include <ms0515/Debugger.hpp>
#include <ms0515/Emulator.hpp>
#include <ms0515/ScreenReader.hpp>
#include <ms0515/Terminal.hpp>

#include <SDL.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct ImFont;

namespace ms0515_frontend {

class App {
public:
    explicit App(CliArgs cli);
    ~App();

    App(const App &) = delete;
    App &operator=(const App &) = delete;

    int run();

private:
    /* ── Init / shutdown phases ──────────────────────────────────────── */
    bool initSdl();
    void initImGui();
    void initEmulator();
    void mountInitialDisks();          /* called by initEmulator */
    void applyKeyboardConfig();        /* called by initEmulator */
    void initScreenReader();           /* called by initEmulator */
    void initAudio();
    void shutdown();

    /* ── Main loop ──────────────────────────────────────────────────── */
    void pumpEvents(bool &quit);
    bool handleHotkey(const SDL_Event &ev);
    void tick();
    void renderFrame();

    /* ── Per-frame UI rendering ─────────────────────────────────────── */
    void drawMenuBar();
    void drawFileMenu();
    void drawFileDiskMenu(int drive);
    void drawMachineMenu();
    void drawRomSubmenu();
    void drawSpeedSubmenu();
    void drawComponentsMenu();
    void drawKeyboardSubmenu();
    void drawViewMenu();
    void drawMountErrorPopup();
    void resizeHostWindow();

    /* ── Mount / unmount actions ────────────────────────────────────── */
    /* Each helper handles one concrete user intent and updates both
     * `mountedFd_/mountedAsDs_` and `config_`.  Errors funnel into
     * `mountErrorMessage_` for the modal popup. */
    void mountDoubleSided(int drive, const std::string &path);
    void mountSingleSide(int unit, const std::string &path);
    void unmountDrive(int drive);
    void unmountUnit(int unit);
    void detachDsRemnant(int keepUnit);
    void refreshMountStateFromCli();

    /* ── Misc helpers ───────────────────────────────────────────────── */
    void loadRom(const std::string &path);
    void setFullscreen(bool on);
    void requestScreenshot() { wantScreenshot_ = true; }

    /* ── Inputs ─────────────────────────────────────────────────────── */
    CliArgs cli_;
    Config  config_;

    /* ── SDL / ImGui ────────────────────────────────────────────────── */
    SDL_Window   *window_   = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture  *frameTex_ = nullptr;

    /* ── Emulator + companions ──────────────────────────────────────── */
    ms0515::Emulator     emu_;
    ms0515::Debugger     dbg_{emu_};
    ms0515::ScreenReader screenReader_;
    ms0515::Terminal     terminal_;
    /* Per-row stable view of the screen.  Each tick we read a fresh
     * snapshot from VRAM; rows that are still mid-write (either
     * decode contains unknown-glyph cells, or content differs from
     * the previous raw sample of that row) are stitched in from
     * this stable view instead of being forwarded as-is.  A row
     * has to be both clean AND stable across two consecutive
     * samples to actually update stableView_ — that's how we filter
     * mid-scroll garbage like "      .SYS     2P 27-12-90"
     * (cells partially blanked while the OS copies content between
     * rows) which has no unknown-glyph signature but is still in
     * flux. */
    ms0515::ScreenReader::Snapshot stableView_;
    bool                           hasStableView_ = false;
    /* The previous tick's raw (unpatched) snapshot — what the
     * screen reader produced before any stitching.  Used to gate
     * stableView_ updates on per-row stability across two ticks. */
    ms0515::ScreenReader::Snapshot prevRawSnap_;
    bool                           hasPrevRawSnap_ = false;
    Video                video_;
    ImFont              *terminalFont_ = nullptr;
    Audio                audio_;
    PhysicalKeyboard     physKbd_;
    OnScreenKeyboard     osk_;
    std::FILE           *screenDumpFile_ = nullptr;

    /* ── Mount state ────────────────────────────────────────────────── */
    std::array<std::string, 4> mountedFd_;
    std::array<bool, 2>        mountedAsDs_{false, false};
    std::string                mountErrorMessage_;
    bool                       mountErrorPending_ = false;

    /* ── ROM / status ───────────────────────────────────────────────── */
    std::string              currentRomPath_;
    std::string              romStatus_;
    std::vector<std::string> availableRoms_;

    /* ── Run state ──────────────────────────────────────────────────── */
    bool running_       = true;
    bool wantScreenshot_= false;
    bool quit_          = false;

    /* ── Time / pacing ──────────────────────────────────────────────── */
    static constexpr float kFrameMs = 20.0f;  /* one emulated 50 Hz frame */
    float    targetSpeed_   = 100.0f;
    float    emuTimeAccumMs_= 0.0f;
    uint32_t lastTickMs_    = 0;
    uint32_t frameCounter_  = 0;
    uint32_t emuFramesSinceReset_ = 0;
    uint32_t hostMsAtLastReset_   = 0;
    uint32_t fpsWindowStartMs_    = 0;
    uint32_t emuFramesInWindow_   = 0;
    float    fpsDisplay_          = 0.0f;
    float    speedDisplay_        = 0.0f;

    /* ── UI flags ───────────────────────────────────────────────────── */
    bool showScreen_   = true;
    bool prevShowScreen_ = false;       /* force first-frame resize */
    bool showDebugger_ = false;
    bool prevShowDebugger_ = false;
    bool showKeyboard_ = false;
    bool prevShowKeyboard_ = false;
    bool showTerminal_ = false;
    bool prevShowTerminal_ = false;
    bool audioOn_      = true;
    bool ramDiskOn_    = true;
    bool fullscreenOn_ = false;
    int  menuBarHeight_= 0;

    /* ── Hotkey-intercept latches (Alt+Enter / ESC) ─────────────────── */
    bool swallowedReturnDown_ = false;
    bool swallowedEscDown_    = false;
};

} /* namespace ms0515_frontend */
