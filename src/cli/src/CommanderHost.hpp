/*
 * CommanderHost.hpp — the commander over the running machine.
 *
 * Ctrl+\ brings the two panels up over the machine's screen; F10, with
 * its question, takes them down.  While they are up the terminal is the commander's:
 * it draws into the alternate screen from the CLI's frame loop, the
 * mirror of the machine's screen stops writing to the terminal and is
 * drawn from its shadow instead - the rows around the guest's cursor
 * under the panels, the whole screen when Ctrl+O hides the panels.  The
 * keys follow the NC rule (files/Routing.hpp): typed text and Enter go
 * to the machine's prompt, the panel keys to the panels.  A mount made
 * in the panels is applied to the machine at once; an image the panels
 * wrote into, which the machine keeps a copy of (the HD), is re-read.
 */
#ifndef MS0515_CLI_COMMANDER_HOST_HPP
#define MS0515_CLI_COMMANDER_HOST_HPP

#include "HostKey.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/VramMirror.hpp>

#include <cstdio>
#include <memory>
#include <string>

namespace ms0515::app { struct CliArgs; }

namespace ms0515::cli {

/* The line that stands at the bottom of the terminal while the panels are
 * down: the keys that bring them up and leave.  It is written once and
 * never again - the machine's screen keeps to its own 25 rows - and the
 * cursor is put back where the machine had it.  "" when the terminal is
 * too small to hold it clear of those rows. */
[[nodiscard]] std::string hintLine(int width, int height);

class CommanderHost {
public:
    /* `cli` names the disks the machine started with (the flags over the
     * config), so the panels open on them.  `out` is the terminal; nullptr
     * draws nothing (the tests drive the keys alone). */
    CommanderHost(Emulator &emu, VramMirror &mirror, const app::CliArgs &cli, FILE *out);
    ~CommanderHost();
    CommanderHost(const CommanderHost &) = delete;
    CommanderHost &operator=(const CommanderHost &) = delete;

    [[nodiscard]] bool active() const noexcept;

    /* A key from the terminal: true when the commander took it (the
     * toggle included), false when it is the guest's. */
    bool onKey(const files::HostKey &key);

    /* After each emulator frame: draw when up. */
    void frame();

    /* On the way out: the panels down, the terminal back; the mounts the
     * user made stored into ms0515.yaml when `saveConfig`. */
    void shutdown(bool saveConfig);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} /* namespace ms0515::cli */

#endif /* MS0515_CLI_COMMANDER_HOST_HPP */
