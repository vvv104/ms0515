/*
 * ConsoleText.hpp - the console's character stream as text for the host.
 *
 * What the machine prints goes through the ROM's output entry a byte at a
 * time: KOI-8 letters, the ROM's pseudographics, CR and LF, and the
 * console's control sequences.  Those are of two kinds.  The VT52's:
 * ESC H and ESC J are seen to home and erase on ROM-B, the rest of the
 * set (ESC A-D, ESC K, ESC Y row column) is taken to follow and is not
 * proven.  And ANSI ones, ESC [ parameters letter: the editors K52 and
 * KED open with ESC [ ? 2 l and the console takes it without printing
 * any of it.  ConsoleText turns the bytes into UTF-8 for one of two
 * readers:
 *
 *   plain     a file or a pipe (`ms0515-run PROG > out.txt`): the text
 *             and its line ends, nothing else - no carriage returns, no
 *             control sequences, no bell.
 *   terminal  a person's terminal: the same text with the cursor
 *             movement kept, the VT52 sequences as their ANSI ones.
 *
 * Line ends are written as "\n" alone; the host's stdout makes of them
 * what the host's text files have.
 */

#ifndef MS0515_RUN_CONSOLETEXT_HPP
#define MS0515_RUN_CONSOLETEXT_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace ms0515::run {

class ConsoleText {
public:
    enum class Reader { plain, terminal };

    explicit ConsoleText(Reader reader) noexcept : reader_(reader) {}

    /* The host's text for the next bytes of the console's stream.  A
     * control sequence cut by the end of `bytes` is finished by the next
     * call. */
    [[nodiscard]] std::string convert(std::string_view bytes);

private:
    enum class State { text, escape, row, column, ansi };

    void control(uint8_t code, std::string &out) const;
    void escape(uint8_t code, std::string &out);

    Reader  reader_;
    State   state_ = State::text;
    uint8_t row_ = 0;
    std::string ansi_;          /* an ANSI sequence's bytes so far */
};

} /* namespace ms0515::run */

#endif /* MS0515_RUN_CONSOLETEXT_HPP */
