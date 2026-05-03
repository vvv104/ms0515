#define _CRT_SECURE_NO_WARNINGS
#include "Ui.hpp"

#include "Config.hpp"           /* fdcUnitFor */
#include "PhysicalKeyboard.hpp"

#include <ms0515/Debugger.hpp>
#include <ms0515/Disassembler.hpp>
#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

namespace ms0515_frontend {

void drawDebuggerWindow(ms0515::Debugger &dbg,
                        bool &running,
                        const std::string &romStatus,
                        bool &open)
{
    if (!ImGui::Begin("Debugger", &open)) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(romStatus.c_str());
    const auto &emu = dbg.emulator();
    ImGui::Text("CPU: PC=%06o  %s",
                emu.pc(),
                emu.halted()  ? "HALTED" :
                emu.waiting() ? "WAIT"   : "running");
    ImGui::Separator();

    if (ImGui::Button(running ? "Pause" : "Run"))
        running = !running;
    ImGui::SameLine();
    if (ImGui::Button("Step") && !running)
        dbg.stepInstruction();
    ImGui::SameLine();
    if (ImGui::Button("Step Over") && !running)
        dbg.stepOver();
    ImGui::SameLine();
    if (ImGui::Button("Reset"))
        dbg.reset();

    ImGui::Separator();
    ImGui::TextUnformatted(dbg.formatRegisters().c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Disassembly", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto lines = dbg.disassembleAtPc(16);
        for (const auto &ins : lines) {
            auto line = std::format("{:06o}: {}", ins.address, ins.text());
            ImGui::TextUnformatted(line.c_str());
        }
    }

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
            if (ec == std::errc{})
                dbg.addBreakpoint(static_cast<uint16_t>(val));
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

void drawScreenWindow(SDL_Window *window,
                      SDL_Texture *frameTex,
                      int          screenContentW,
                      int          screenContentH,
                      int          menuBarHeight,
                      bool         fullscreenOn,
                      bool        &showScreen)
{
    if (fullscreenOn) {
        /* Fullscreen variant: borderless window covering the whole
         * host display, framebuffer letterboxed with aspect-ratio
         * preserved, no chrome.  Letterbox bars are SDL-cleared black
         * by the caller.  ESC or Alt+Enter exits (handled in main). */
        int W = 0, H = 0;
        SDL_GetWindowSize(window, &W, &H);
        float srcAspect = (float)screenContentW / (float)screenContentH;
        float dstAspect = (float)W / (float)H;
        float drawW, drawH;
        if (dstAspect > srcAspect) {
            drawH = (float)H;
            drawW = drawH * srcAspect;
        } else {
            drawW = (float)W;
            drawH = drawW / srcAspect;
        }
        float offX = ((float)W - drawW) * 0.5f;
        float offY = ((float)H - drawH) * 0.5f;

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2((float)W, (float)H));
        ImGuiWindowFlags fsFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBackground;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (ImGui::Begin("##fullscreen_screen", nullptr, fsFlags)) {
            ImGui::SetCursorPos(ImVec2(offX, offY));
            ImGui::Image((ImTextureID)(intptr_t)frameTex,
                         ImVec2(drawW, drawH));
        }
        ImGui::End();
        ImGui::PopStyleVar();
    } else if (showScreen) {
        /* Windowed variant: caption + auto-sized to framebuffer.
         * Same style as debugger and on-screen keyboard. */
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
}

void drawStatusBar(const StatusBarState &s)
{
    int cw = 0, ch = 0;
    SDL_GetWindowSize(s.window, &cw, &ch);
    const ImGuiStyle &st = ImGui::GetStyle();
    const float lineH   = ImGui::GetTextLineHeightWithSpacing();
    const float statusH = lineH * 2.0f + st.WindowPadding.y * 2.0f + 2.0f;
    ImGui::SetNextWindowPos(ImVec2(0.0f, (float)ch - statusH));
    ImGui::SetNextWindowSize(ImVec2((float)cw, statusH));
    ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoFocusOnAppearing |
                         ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin("##statusbar", nullptr, f)) {
        ImGui::End();
        return;
    }

    /* Line 1: CPU state | speed/fps | uptime | host-mode + modifier indicators. */
    const bool halted  = s.emu.halted();
    const bool waiting = s.emu.waiting();
    const char *state = halted   ? "HALT" :
                        waiting  ? "WAIT" :
                        s.running ? "RUN" : "PAUSE";
    ImVec4 col = halted   ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f) :
                 waiting  ? ImVec4(0.9f, 0.8f, 0.3f, 1.0f) :
                 s.running ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) :
                            ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
    ImGui::TextColored(col, "%s", state);
    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

    ImGui::Text("%.0f%%/%.0f%%  %.0f fps",
                s.speedDisplay, s.targetSpeed, s.fpsDisplay);
    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

    uint32_t hostS = (SDL_GetTicks() - s.hostMsAtLastReset) / 1000;
    uint32_t emuMs = s.emuFramesSinceReset * 20;  /* 50 Hz frame */
    uint32_t emuS  = emuMs / 1000;
    ImGui::Text("host %02u:%02u:%02u  emu %02u:%02u:%02u",
                hostS / 3600, (hostS / 60) % 60, hostS % 60,
                emuS  / 3600, (emuS  / 60) % 60, emuS  % 60);
    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();

    /* Host mode indicator — keyboard disconnected from emulator. */
    if (s.physKbd.hostMode()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "HOST");
        ImGui::SameLine();
    }

    auto modCol = [](bool on) {
        return on ? ImVec4(1.0f, 1.0f, 0.4f, 1.0f)
                  : ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    };
    bool vrOn  = s.emu.keyHeld(ms0515::Key::ShiftL)
              || s.emu.keyHeld(ms0515::Key::ShiftR);
    bool suOn  = s.emu.keyHeld(ms0515::Key::Ctrl);
    bool kmpOn = s.emu.keyHeld(ms0515::Key::Compose);
    ImGui::TextColored(modCol(vrOn),         "\xd0\x92\xd0\xa0");        /* ВР */
    ImGui::SameLine();
    ImGui::TextColored(modCol(suOn),         "\xd0\xa1\xd0\xa3");        /* СУ */
    ImGui::SameLine();
    ImGui::TextColored(modCol(s.emu.capsOn()),"\xd0\xa4\xd0\x9a\xd0\xa1");/* ФКС */
    ImGui::SameLine();
    ImGui::TextColored(modCol(kmpOn),        "\xd0\x9a\xd0\x9c\xd0\x9f");/* КМП */
    ImGui::SameLine();
    if (s.emu.ruslatOn())
        ImGui::TextColored(modCol(true),     "\xd0\xa0\xd0\xa3\xd0\xa1");/* РУС */
    else
        ImGui::TextColored(modCol(true),     "\xd0\x9b\xd0\x90\xd0\xa2");/* ЛАТ */

    /* Line 2: per-drive mount summary.  One column per physical
     * drive, mode-dependent format:
     *   empty  →  "Disk 0: empty"
     *   DS     →  "Disk 0: <name> (DS)"
     *   SS     →  "Disk 0: <side0> / <side1>"  (either may be (empty)) */
    for (int drive = 0; drive < 2; ++drive) {
        int unit0 = fdcUnitFor(drive, 0);
        int unit1 = fdcUnitFor(drive, 1);
        if (drive > 0) {
            ImGui::SameLine();
            ImGui::TextUnformatted("|");
            ImGui::SameLine();
        }
        if (!s.mountedFd[unit0].empty() ||
            !s.mountedFd[unit1].empty()) {
            if (s.mountedAsDs[drive]) {
                std::string n = std::filesystem::path(
                    s.mountedFd[unit0]).filename().string();
                ImGui::Text("Disk %d: %s (DS)", drive, n.c_str());
            } else {
                auto sideName = [&](int unit) -> std::string {
                    if (s.mountedFd[unit].empty()) return "(empty)";
                    return std::filesystem::path(s.mountedFd[unit])
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

    ImGui::End();
}

void drawTerminalWindow(ms0515::Terminal &term, bool &open, ImFont *monoFont)
{
    if (!ImGui::Begin("Terminal", &open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear"))
        term.clearHistory();
    ImGui::SameLine();
    if (ImGui::Button("Copy"))
        ImGui::SetClipboardText(term.history().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(Copy dumps the whole scrollback)");

    ImGui::Separator();

    static size_t lastBufSize         = 0;
    static size_t lastSeenScreenStart = 0;
    const std::string &hist           = term.history();
    const bool grew                   = hist.size() > lastBufSize;
    lastBufSize                       = hist.size();
    const size_t curScreenStart       = term.lastScreenStart();
    const bool screenRedrawn          = curScreenStart != lastSeenScreenStart;
    lastSeenScreenStart               = curScreenStart;

    if (monoFont) ImGui::PushFont(monoFont);

    /* Single-child rendering: TextUnformatted in a BeginChild we
     * own.  Earlier tried InputTextMultiline for inline drag-select,
     * but it nests its own scrollable child inside our outer child —
     * giving the user two scrollbars and breaking SetScrollY.  Use
     * Copy button for clipboard for now; per-line Selectable could
     * be layered on later if needed. */
    ImGui::BeginChild("##term_scroll", ImVec2(-FLT_MIN, -FLT_MIN), false,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const float lineH   = ImGui::GetTextLineHeight();
    const float windowH = ImGui::GetWindowHeight();

    /* Tight line spacing — terminal output looks wrong with the
     * default ItemSpacing.y between rows. */
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::TextUnformatted(hist.data(), hist.data() + hist.size());
    ImGui::PopStyleVar();

    /* Tail padding for Ctrl+L-style redraw: ImGui clamps SetScrollY
     * to ScrollMaxY (total - viewport).  If the new screen is shorter
     * than the viewport, the desired top-anchor falls past
     * ScrollMaxY and gets clamped, putting the screen at viewport
     * bottom instead of top.  An invisible Dummy fills the gap so
     * ScrollMaxY reaches the screen-start line.  As prints fill in
     * below the screen, the gap shrinks; once content from
     * screen-start exceeds the viewport, padding hits zero and
     * auto-scroll-to-tail naturally takes over (top-of-screen
     * scrolls off, exactly like a real terminal). */
    const auto screenStartLine = static_cast<float>(
        std::count(hist.data(), hist.data() + curScreenStart, '\n'));
    const auto linesFromScreen = static_cast<float>(
        curScreenStart < hist.size()
            ? std::count(hist.data() + curScreenStart,
                         hist.data() + hist.size(), '\n')
              + 1   /* the last (possibly partial) line */
            : 0);
    const float padY = windowH - linesFromScreen * lineH;
    if (padY > 0.0f)
        ImGui::Dummy(ImVec2(0.0f, padY));

    const float anchorY = screenStartLine * lineH;

    if (screenRedrawn) {
        /* The OS just redrew the screen — anchor the viewport top to
         * the new screen's first line.  Old content scrolls into
         * scrollback above; padding fills the still-empty area
         * below.  Subsequent prints replace the padding until the
         * viewport is full. */
        ImGui::SetScrollY(anchorY);
    } else if (grew) {
        /* While the new screen still fits in the viewport, hold the
         * anchor exactly at viewport top — don't let SetScrollHereY's
         * "tail follow" drift it forward by ItemSpacing /
         * WindowPadding (which would push the first line ~1 row off
         * the top).  Once content from the anchor exceeds the
         * viewport, switch to tail-follow so new lines stay visible
         * — but only when the user is still near the tail; if they
         * scrolled up to read history, leave them alone. */
        if (linesFromScreen * lineH <= windowH) {
            ImGui::SetScrollY(anchorY);
        } else if (ImGui::GetScrollY() >=
                   ImGui::GetScrollMaxY() - lineH * 2.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }

    ImGui::EndChild();

    if (monoFont) ImGui::PopFont();

    ImGui::End();
}

} /* namespace ms0515_frontend */
