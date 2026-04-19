/*
 * OnScreenKeyboard.hpp — MS7004 virtual keyboard widget.
 *
 * Owns the visual layout (loaded from ms7004_layout.txt), highlight
 * state, sticky modifier latching, and click dispatch.  Every click
 * goes through the ms7004 microcontroller model via Emulator::keyPress,
 * so virtual clicks produce the exact same scancode sequences a real
 * MS7004 key press would.
 *
 * Physical keyboard highlighting is automatic: the ms7004 model tracks
 * which keys are held, and the OSK queries that state for rendering.
 * No separate host-key tracking is needed.
 */

#ifndef MS0515_FRONTEND_ON_SCREEN_KEYBOARD_HPP
#define MS0515_FRONTEND_ON_SCREEN_KEYBOARD_HPP

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

extern "C" {
#include <ms0515/ms7004.h>
}

namespace ms0515 { class Emulator; }

namespace ms0515_frontend {

class OnScreenKeyboard {
public:
    OnScreenKeyboard();

    /* Load layout.  If `path` is empty, tries a few likely locations. */
    bool loadLayout(const std::string &path = {});

    /* Cap unit size in pixels. */
    [[nodiscard]] float unit() const noexcept { return unit_; }
    void  setUnit(float u) noexcept { unit_ = u; }

    /* Outer ImGui window size. */
    [[nodiscard]] float pixelWidth()  const;
    [[nodiscard]] float pixelHeight() const;

    /* Render the keyboard window.  `open` mirrors the menu toggle. */
    void draw(ms0515::Emulator &emu, bool &open);

    [[nodiscard]] bool loaded() const noexcept { return !rows_.empty(); }

private:
    /* One cap on the keyboard. */
    struct Cap {
        std::string  label;       /* multi-line: top\nbottom */
        float        w;           /* width in units (1.0 = base) */
        bool         drawn;       /* false = cosmetic gap */
        bool         dim;         /* drawn but inert on click */
        bool         gray;        /* gray chassis (fn / edit / arrow cluster) */
        ms7004_key_t ms7004key;   /* physical key binding */
        bool         sticky;      /* modifier cap: latch on click */
        bool         toggle;      /* toggle cap (ФКС / РУС-ЛАТ) */
    };

    std::vector<std::vector<Cap>>    rows_;
    std::unordered_set<int>          stickyKeys_;   /* latched ms7004_key_t */
    float                            unit_ = 40.0f;

    bool parseLine(const std::string &line, Cap &out) const;
    void bindCap(Cap &k, int &shiftCountInRow, bool inFnRow) const;
    bool highlighted(const Cap &c, const ms0515::Emulator &emu) const;
    void drawRow(size_t r, ms0515::Emulator &emu);
    void handleClick(const Cap &c, ms0515::Emulator &emu);
};

} /* namespace ms0515_frontend */

#endif
