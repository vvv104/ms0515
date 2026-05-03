/*
 * OnScreenKeyboard.hpp — MS7004 virtual keyboard widget (frontend side).
 *
 * Owns visual state only: cap-unit pixel size, sticky modifier latch
 * set, and the click → ms7004 key sequence dispatcher.  The layout
 * data (rows of caps, label → Key bindings, mode-dependent letter
 * predicates) lives in `ms0515::KeyboardLayout` so that any frontend
 * — SDL+ImGui today, Web tomorrow — gets the same authoritative
 * description.  This class binds that data to ImGui buttons and
 * clicks; it does not parse or know layout-file syntax.
 */

#ifndef MS0515_FRONTEND_ON_SCREEN_KEYBOARD_HPP
#define MS0515_FRONTEND_ON_SCREEN_KEYBOARD_HPP

#include <ms0515/Emulator.hpp>       /* ms0515::Key */
#include <ms0515/KeyboardLayout.hpp>

#include <cstdint>
#include <string>
#include <unordered_set>

namespace ms0515 { class Emulator; }

namespace ms0515_frontend {

class OnScreenKeyboard {
public:
    OnScreenKeyboard();

    /* Load the keyboard layout.  When `path` is empty, the frontend's
     * asset-search roots are tried in order.  Returns true on success. */
    bool loadLayout(const std::string &path = {});

    /* Cap unit size in pixels. */
    [[nodiscard]] float unit() const noexcept { return unit_; }
    void  setUnit(float u) noexcept { unit_ = u; }

    /* Outer ImGui window size for the keyboard. */
    [[nodiscard]] float pixelWidth()  const;
    [[nodiscard]] float pixelHeight() const;

    /* Render the keyboard window.  `open` mirrors the menu toggle. */
    void draw(ms0515::Emulator &emu, bool &open);

    [[nodiscard]] bool loaded() const noexcept { return layout_.loaded(); }

private:
    bool highlighted(const ms0515::KeyboardLayout::Cap &c,
                     const ms0515::Emulator &emu) const;
    void drawRow(size_t r, ms0515::Emulator &emu);
    void handleClick(const ms0515::KeyboardLayout::Cap &c,
                     ms0515::Emulator &emu);

    ms0515::KeyboardLayout    layout_;
    std::unordered_set<int>   stickyKeys_;   /* latched ms0515::Key */
    float                     unit_ = 40.0f;
};

} /* namespace ms0515_frontend */

#endif
