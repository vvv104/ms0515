/*
 * Commander.hpp — the two panels over the mounted devices, the key bar,
 * the dialogs and the viewer, drawn with FTXUI.  All state lives in the
 * model (Panel / Mounts / Ops / Viewer); this draws it and routes keys.
 *
 * It does not own the terminal: whoever does - the standalone
 * ms0515-files in FTXUI's own loop, ms0515-cli over the running machine
 * in its frame loop - asks for the picture of a given size and hands the
 * keys in as FTXUI events.  The host learns of what changed through the
 * hooks: the mounts, so it can apply them to its machine, and the images
 * written, so it can resync a device that keeps a copy.
 */
#ifndef MS0515_FILES_COMMANDER_HPP
#define MS0515_FILES_COMMANDER_HPP

#include "Mounts.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace ms0515::app { class Config; }

namespace ms0515::files {

struct CommanderHooks {
    /* The slots changed (a mount or an unmount): apply mounts() to the machine. */
    std::function<void()> mountsChanged;
    /* Bytes were written into this image. */
    std::function<void(const std::filesystem::path &image)> imageChanged;
    /* F10 asks this before quitting; empty: F10 quits at once. */
    std::string quitQuestion;
    /* Enter on a program: the host types this line into the machine's
     * prompt ("RUN DZ0:GAME", "@DZ0:START") and presses Enter there.
     * Without it Enter views the file. */
    std::function<void(const std::string &line)> runInGuest;
};

class Commander {
public:
    Commander(Mounts mounts, app::Config &config, CommanderHooks hooks = {});
    ~Commander();
    Commander(const Commander &) = delete;
    Commander &operator=(const Commander &) = delete;

    /* The picture for a terminal of `width` x `height`.  `guest`, when
     * given, is drawn between the panels and the key bar - the host's
     * rows of the machine's screen, NC's command line. */
    [[nodiscard]] ftxui::Element render(int width, int height, ftxui::Element guest = nullptr);

    /* A key; true when it was used.  Alt+F1 / Alt+F2 arrive as FTXUI's
     * unnamed Special events with xterm's CSI 1;3P / 1;3Q. */
    bool onEvent(const ftxui::Event &event);

    /* F10 was pressed (and answered, when there is a question); once. */
    [[nodiscard]] bool takeQuitRequest() noexcept;
    /* A dialog, the viewer or the image picker is open: every key is wanted. */
    [[nodiscard]] bool modal() const noexcept;

    [[nodiscard]] const Mounts &mounts() const noexcept;
    /* The panels' key bar alone, for a host that shows the machine's screen. */
    [[nodiscard]] ftxui::Element keyBar() const;

    /* The terminal row the guest's rows start at on a page of `height` -
     * where the host puts the terminal's own cursor. */
    [[nodiscard]] static int guestRowsTop(int height) noexcept;
    /* Re-read the panels' volumes (the host changed something). */
    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/* The standalone program: the commander in FTXUI's own loop until F10.
 * `config` receives the mounts the user made, to be saved by the caller. */
int runCommander(Mounts mounts, app::Config &config, const std::string &quitQuestion);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_COMMANDER_HPP */
