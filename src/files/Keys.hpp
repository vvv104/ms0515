/*
 * Keys.hpp — the function keys with a modifier, read off what the
 * terminal sends.
 *
 * FTXUI names the plain F1..F12 but passes a modified one (Alt+F1 is
 * xterm's "\x1B[1;3P", Alt+F5 "\x1B[15;3~") through as an unnamed
 * special event, so the raw input is decoded here.  A terminal cannot
 * report a modifier pressed on its own - only the key it modifies - so
 * this is the whole of what a modifier can do in a terminal.
 */
#ifndef MS0515_FILES_KEYS_HPP
#define MS0515_FILES_KEYS_HPP

#include <optional>
#include <string_view>

namespace ms0515::files {

struct FunctionKey {
    int  number = 0;    /* 1..12 */
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

/* The function key behind a terminal input sequence: xterm's SS3 form
 * ("\x1BOP" F1..F4), its CSI forms ("\x1B[15~" F5, "\x1B[1;3P" Alt+F1,
 * "\x1B[15;5~" Ctrl+F5), and the ESC-prefixed Alt some terminals send
 * ("\x1B\x1BOP").  Nullopt for anything else. */
[[nodiscard]] std::optional<FunctionKey> parseFunctionKey(std::string_view raw);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_KEYS_HPP */
