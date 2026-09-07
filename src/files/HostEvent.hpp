/*
 * HostEvent.hpp — a key of the host's terminal as the FTXUI event the
 * commander reads, for a host that parses its own input.
 */
#ifndef MS0515_FILES_HOSTEVENT_HPP
#define MS0515_FILES_HOSTEVENT_HPP

#include "HostKey.hpp"

#include <ftxui/component/event.hpp>

namespace ms0515::files {

/* Enter, Tab, Esc, Backspace, the arrows, the editing keys and the
 * F-keys as FTXUI names them; printable bytes as characters (KOI-8
 * letters in UTF-8); other control bytes as FTXUI's unnamed specials;
 * Alt+F<n> as xterm's modified sequence, which Commander decodes. */
[[nodiscard]] ftxui::Event toEvent(const HostKey &key);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_HOSTEVENT_HPP */
