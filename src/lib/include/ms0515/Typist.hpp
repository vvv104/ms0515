/*
 * Typist.hpp — types characters on the machine's keyboard.
 *
 * A host that has characters rather than keys - a terminal's input, a
 * line of text to enter - hands them here as KOI-8R bytes; the Typist
 * works out the MS-7004 key for each, with Shift or СУ (Ctrl) held and
 * the РУС/ЛАТ key pressed first where the letter needs the other
 * alphabet, and taps them into the Emulator a key at a time, one call of
 * pump() per frame.  The characters go the whole way the hardware has:
 * the keyboard, its interrupt, the system's terminal service, which
 * echoes them as it would a person's typing.
 */

#ifndef MS0515_TYPIST_HPP
#define MS0515_TYPIST_HPP

#include <ms0515/Emulator.hpp>

#include <cstddef>
#include <cstdint>
#include <deque>

namespace ms0515 {

class Typist {
public:
    /* Queue a character: a KOI-8R byte.  LF is taken as CR (the host's
     * line end for RT-11's); 001..032 other than BS, TAB and CR are
     * СУ/letter; a character no key of the MS-7004 gives is dropped. */
    void type(uint8_t koi8);

    /* Queue a key that has no character: an arrow, a function key. */
    void type(Key key);

    /* Once per frame: press the next key, hold it, let it go. */
    void pump(Emulator &emu);

    /* Keys queued and not yet let go. */
    [[nodiscard]] std::size_t pending() const noexcept;

private:
    struct Tap {
        Key  key;
        bool shift;
        bool ctrl = false;
    };
    enum class Phase { idle, holding, cooldown };

    std::deque<Tap> queue_;

    /* The system's РУС/ЛАТ state as the Typist has left it.  ЛАТ at the
     * start, as the systems come up; flipped with every РУС/ЛАТ tap.  A
     * switch made by other means is not seen. */
    bool rus_ = false;

    Phase phase_ = Phase::idle;
    int   frames_ = 0;
    Tap   held_{Key::None, false};
};

} /* namespace ms0515 */

#endif /* MS0515_TYPIST_HPP */
