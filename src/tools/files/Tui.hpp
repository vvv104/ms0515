/*
 * Tui.hpp — the terminal front-end of ms0515-files: two panels over the
 * mounted devices, the key bar, the dialogs and the viewer, drawn with
 * FTXUI and sized to the terminal.  All state lives in the core
 * (Panel / Mounts / Ops / Viewer); this only draws it and routes keys.
 */
#ifndef MS0515_FILES_TUI_HPP
#define MS0515_FILES_TUI_HPP

#include "Mounts.hpp"

namespace ms0515::app { class Config; }

namespace ms0515::files {

/* Run the manager until F10 / Esc.  `config` receives the mounts the user
 * made, to be saved by the caller. */
int runTui(Mounts mounts, app::Config &config);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_TUI_HPP */
