/*
 * Ui.hpp — Dear ImGui rendering of standalone windows/panels.
 *
 * The menu bar still lives in main.cpp because its actions touch a
 * lot of local state (mounted disks, ROM path, lambdas, modal
 * popups).  Self-contained UI elements with a tractable parameter
 * count have been moved here:
 *
 *   - drawDebuggerWindow:  CPU/disasm/breakpoint panel
 *   - drawScreenWindow:    framebuffer (windowed + fullscreen variants)
 *   - drawStatusBar:       bottom status bar with mounts/timers
 *
 * No new state is owned here — every function takes the live data it
 * needs by reference. */
#pragma once

#include <SDL.h>
#include <cstdint>
#include <string>

struct ImFont;

namespace ms0515 {
class Emulator;
class Debugger;
class Terminal;
} /* namespace ms0515 */

namespace ms0515_frontend {

class PhysicalKeyboard;

void drawDebuggerWindow(ms0515::Debugger &dbg,
                        bool &running,
                        const std::string &romStatus,
                        bool &open);

void drawScreenWindow(SDL_Window *window,
                      SDL_Texture *frameTex,
                      int          screenContentW,
                      int          screenContentH,
                      int          menuBarHeight,
                      bool         fullscreenOn,
                      bool        &showScreen);

struct StatusBarState {
    SDL_Window               *window;
    const ms0515::Emulator   &emu;
    const PhysicalKeyboard   &physKbd;
    bool                      running;
    float                     speedDisplay;
    float                     targetSpeed;
    float                     fpsDisplay;
    uint32_t                  emuFramesSinceReset;
    uint32_t                  hostMsAtLastReset;
    const std::string        *mountedFd;   /* [4] */
    const bool               *mountedAsDs; /* [2] */
};

void drawStatusBar(const StatusBarState &s);

/* Multiline scrollback view of Terminal::history().  Auto-scrolls to
 * the bottom while the user hasn't manually scrolled up; once they do
 * (typically to read older output) auto-scroll pauses until the view
 * is brought back to the bottom.  A "Clear" button drops the
 * accumulated history.  `monoFont` is the fixed-pitch face used to
 * render the scrollback so the OS's 80-column grid lines up; pass
 * nullptr to fall back to the default UI font. */
void drawTerminalWindow(ms0515::Terminal &term, bool &open,
                        ImFont *monoFont);

} /* namespace ms0515_frontend */
