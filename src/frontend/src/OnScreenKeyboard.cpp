/*
 * OnScreenKeyboard.cpp — MS7004 virtual keyboard.
 *
 * This file is the visual side of the on-screen keyboard.  Layout
 * parsing, label → ms0515::Key binding, and the mode-dependent
 * letter/symbol predicates live in `ms0515::KeyboardLayout` so they
 * are reusable across frontends.  Here we own only:
 *
 *   - cap-unit pixel size (UI scale),
 *   - sticky modifier latches (latched on click, released after the
 *     next regular key click in one-shot fashion),
 *   - ImGui rendering of caps as buttons,
 *   - the click → ms7004 keyPress sequence (with the four documented
 *     UX deviations from authentic MS7004 behaviour).
 *
 * Clicks route through Emulator::keyPress; no raw scancode injection.
 */

#include "OnScreenKeyboard.hpp"
#include "Config.hpp"          /* Paths::searchRoots */

#include <ms0515/Emulator.hpp>

#include <imgui.h>

#include <filesystem>
#include <format>

namespace ms0515_frontend {

using Cap = ms0515::KeyboardLayout::Cap;

/* ── OnScreenKeyboard ─────────────────────────────────────────────────── */

OnScreenKeyboard::OnScreenKeyboard() = default;

bool OnScreenKeyboard::loadLayout(const std::string &path)
{
    layout_.clear();
    stickyKeys_.clear();

    std::vector<std::string> candidates;
    if (!path.empty()) {
        candidates.push_back(path);
    } else {
        const char *rels[] = {
            "assets/keyboard/ms7004_layout.txt",
        };
        for (const auto &root : Paths::searchRoots())
            for (const char *rel : rels)
                candidates.push_back((root / rel).string());
    }
    for (const auto &p : candidates) {
        if (std::filesystem::exists(p) && layout_.loadFromFile(p))
            return true;
    }
    return false;
}

/* ── Geometry ─────────────────────────────────────────────────────────── */

float OnScreenKeyboard::pixelWidth() const
{
    float maxW = 0;
    for (const auto &row : layout_.rows()) {
        float w = 0;
        for (const auto &k : row) w += k.widthUnits * unit_;
        if (w > maxW) maxW = w;
    }
    return maxW + 24.0f;
}

float OnScreenKeyboard::pixelHeight() const
{
    return (float)layout_.rows().size() * (unit_ * 0.95f + 4.0f) + 28.0f;
}

/* ── Click dispatch ───────────────────────────────────────────────────── */

void OnScreenKeyboard::handleClick(const Cap &c, ms0515::Emulator &emu)
{
    if (c.dim || c.key == ms0515::Key::None) return;

    /* Ъ and _ share scancode 0o361.  The ROM renders it as Ъ in РУС
     * and _ in ЛАТ.  Suppress Ъ in ЛАТ (it has no Latin equivalent).
     * _ in РУС is handled by RUSLAT-immunity below (temporarily
     * switches to ЛАТ so the ROM outputs _). */
    if (c.key == ms0515::Key::HardSign && !emu.ruslatOn())
        return;

    /* Sticky modifier cap: toggle latch. */
    if (c.sticky) {
        int k = (int)c.key;
        if (stickyKeys_.count(k)) {
            emu.keyPress(c.key, false);
            stickyKeys_.erase(k);
        } else {
            emu.keyPress(c.key, true);
            stickyKeys_.insert(k);
        }
        return;
    }

    /* Toggle caps (ФКС, РУС-ЛАТ): press + release.  The ms7004 model
     * flips the internal toggle flag on press; release is a no-op. */
    if (c.toggle) {
        emu.keyPress(c.key, true);
        emu.keyPress(c.key, false);
        return;
    }

    /* ── UX convenience layer ──────────────────────────────────────
     *
     * Four deviations from authentic MS7004 / ROM behaviour, applied
     * only to OSK clicks (physical keyboard goes through the model
     * unmodified).  See comments marked [DEVIATE] in ms7004.c.
     *
     * 1. ВР (Shift) does not change symbol-on-letter keys (Ш/[ Щ/]
     *    Э/\ Ч/¬) in ЛАТ mode.  On real hardware, Shift + [ → {.
     *    Here, the OSK releases Shift before emitting the key.  In
     *    РУС mode these keys are letters and respond to Shift
     *    normally.
     *
     * 2. ФКС (CapsLock) only affects letter keys.  Digits, symbols,
     *    and function keys are immune.  Which keys count as "letters"
     *    is mode-dependent: in РУС mode, Ш/Щ/Э/Ч/Ю/Ъ are letters.
     *    Implemented by temporarily toggling CAPS off around emission.
     *
     * 3. ВР inverts ФКС on letter keys.  On real MS7004, CAPS +
     *    Shift still produces uppercase.  Here, CAPS + Shift + letter
     *    produces lowercase (modern CapsLock + Shift cancellation).
     *
     * 4. РУС/ЛАТ does not change non-letter keys.  On real hardware,
     *    the ROM maps some symbol scancodes to Cyrillic in РУС mode
     *    (e.g. { → Ш, } → Щ).  Here, non-letter keys temporarily
     *    switch to ЛАТ so their symbol output is preserved.
     * ──────────────────────────────────────────────────────────────── */

    const bool rusMode      = emu.ruslatOn();
    const bool shiftLatched = stickyKeys_.count((int)ms0515::Key::ShiftL)
                           || stickyKeys_.count((int)ms0515::Key::ShiftR);
    const bool capsOn       = emu.capsOn();
    const bool letter       = ms0515::isLetterKey(c.key, rusMode);
    const bool shiftImmune  = ms0515::isShiftImmuneSymbol(c.key, rusMode);

    const bool dropShift     = shiftLatched
                            && (shiftImmune                     /* fix 1 */
                             || (letter && capsOn));            /* fix 3 */
    const bool toggleCapsOff = capsOn
                            && (!letter                         /* fix 2 */
                             || shiftLatched);                  /* fix 3 */
    const bool toggleRusOff  = rusMode && !letter;              /* fix 4 */

    if (dropShift || toggleCapsOff || toggleRusOff) {
        if (dropShift) {
            for (auto it = stickyKeys_.begin(); it != stickyKeys_.end(); ) {
                auto mk = static_cast<ms0515::Key>(*it);
                if (mk == ms0515::Key::ShiftL || mk == ms0515::Key::ShiftR) {
                    emu.keyPress(mk, false);
                    it = stickyKeys_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        if (toggleRusOff) {
            emu.keyPress(ms0515::Key::RusLat, true);
            emu.keyPress(ms0515::Key::RusLat, false);
        }

        if (toggleCapsOff) {
            emu.keyPress(ms0515::Key::Caps, true);
            emu.keyPress(ms0515::Key::Caps, false);
        }

        emu.keyPress(c.key, true);
        emu.keyPress(c.key, false);

        if (toggleCapsOff) {
            emu.keyPress(ms0515::Key::Caps, true);
            emu.keyPress(ms0515::Key::Caps, false);
        }

        if (toggleRusOff) {
            emu.keyPress(ms0515::Key::RusLat, true);
            emu.keyPress(ms0515::Key::RusLat, false);
        }

        if (!stickyKeys_.empty()) {
            for (int k : stickyKeys_)
                emu.keyPress(static_cast<ms0515::Key>(k), false);
            stickyKeys_.clear();
        }
        return;
    }

    /* Regular key: press, release, then release any sticky mods
     * (one-shot behaviour). */
    emu.keyPress(c.key, true);
    emu.keyPress(c.key, false);

    if (!stickyKeys_.empty()) {
        for (int k : stickyKeys_)
            emu.keyPress(static_cast<ms0515::Key>(k), false);
        stickyKeys_.clear();
    }
}

/* ── Rendering ────────────────────────────────────────────────────────── */

bool OnScreenKeyboard::highlighted(const Cap &c,
                                   const ms0515::Emulator &emu) const
{
    if (c.key == ms0515::Key::None) return false;

    /* Toggle caps: lit when the toggle is on. */
    if (c.toggle) {
        if (c.key == ms0515::Key::Caps)   return emu.capsOn();
        if (c.key == ms0515::Key::RusLat) return emu.ruslatOn();
    }

    /* Sticky modifiers: lit when latched. */
    if (c.sticky) {
        if (stickyKeys_.count((int)c.key)) return true;
    }

    /* All keys: lit when physically held in the ms7004 model. */
    return emu.keyHeld(c.key);
}

void OnScreenKeyboard::drawRow(size_t rowIdx, ms0515::Emulator &emu)
{
    const auto &keys    = layout_.rows()[rowIdx];
    const float spacing = 3.0f;
    const float capH    = unit_ * 0.95f;

    for (size_t i = 0; i < keys.size(); ++i) {
        const Cap &k = keys[i];
        float btnW = unit_ * k.widthUnits - spacing;
        if (btnW < 1) btnW = 1;
        ImVec2 sz(btnW, capH);

        if (!k.drawn) {
            auto id = std::format("##gap{}_{}", rowIdx, i);
            ImGui::InvisibleButton(id.c_str(), sz);
            ImGui::SameLine(0, spacing);
            continue;
        }

        int colorsPushed = 0;
        if (highlighted(k, emu)) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.95f, 0.65f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
            colorsPushed += 3;
        } else if (k.gray) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.62f, 0.64f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.70f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.55f, 0.57f, 1.0f));
            colorsPushed += 3;
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.94f, 0.94f, 0.94f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 1.00f, 1.00f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            colorsPushed += 3;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        ++colorsPushed;

        ImGui::PushID((int)(rowIdx * 1024 + i));
        if (ImGui::Button(k.label.empty() ? " " : k.label.c_str(), sz))
            handleClick(k, emu);
        ImGui::PopID();

        ImGui::PopStyleColor(colorsPushed);
        ImGui::SameLine(0, spacing);
    }
    ImGui::NewLine();
}

void OnScreenKeyboard::draw(ms0515::Emulator &emu, bool &open)
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.23f, 1.0f));
    if (!ImGui::Begin("Keyboard (MS7004)", &open,
                      ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    if (!layout_.loaded()) {
        ImGui::TextUnformatted("ms7004_layout.txt not found.");
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    for (size_t r = 0; r < layout_.rows().size(); ++r) {
        drawRow(r, emu);
        if (r == 0) ImGui::Dummy(ImVec2(1, 4));
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

} /* namespace ms0515_frontend */
