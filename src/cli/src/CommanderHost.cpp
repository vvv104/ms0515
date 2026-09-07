/*
 * CommanderHost.cpp — the panels up and down over the machine, the keys
 * routed, the picture drawn from the frame loop.
 */
#include "CommanderHost.hpp"

#include "StdioBridge.hpp"

#include "Commander.hpp"
#include "GuestScreen.hpp"
#include "HostEvent.hpp"
#include "Commander.hpp"
#include "MountSync.hpp"
#include "Routing.hpp"
#include "Scrollback.hpp"

#include "ms0515/app/Cli.hpp"
#include "ms0515/app/Config.hpp"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>

namespace ms0515::cli {

namespace {

constexpr const char *kAltScreenOn  = "\x1B[?1049h\x1B[?25l";
constexpr const char *kAltScreenOff = "\x1B[?1049l";

} // namespace

struct CommanderHost::Impl {
    Emulator &emu;
    VramMirror &mirror;
    app::CliArgs cli;
    FILE *out;                      /* the terminal, or nullptr for none */
    app::Config scratchConfig;      /* the commander writes its mounts here; stored for real at shutdown */
    files::RouteState state;
    std::optional<files::Commander> commander;
    std::string lastPicture;
    ftxui::Dimensions lastSize{0, 0};
    bool mountsTouched = false;
    Scrollback scrollback;          /* the rows that left the machine's screen, kept all along */
    int scrolledBack = 0;           /* rows leafed back into it with PgUp */
    /* where the terminal's own cursor goes, 0-based; -1: nowhere to put it */
    int cursorRow = -1;
    int cursorCol = -1;
    /* the guest's cursor cell, as last seen.  The mirror reports none
     * while the OS has a letter in that cell (the moment of typing) and
     * between the blinks, so the last sighting is what to go by. */
    int guestRow = -1;
    int guestCol = -1;

    Impl(Emulator &e, VramMirror &m, const app::CliArgs &c, FILE *o) : emu(e), mirror(m), cli(c), out(o) {}
    void write(const char *s) { if (out) { std::fputs(s, out); std::fflush(out); } }

    void makeCommander();
    void enter();
    void leave();
    void draw();
    bool noteGuestCursor();
    [[nodiscard]] ftxui::Element picture(int width, int height);
    [[nodiscard]] VramMirror::Snapshot historyRows(int count) const;
    void leafBack(int rows);
    bool onKey(const files::HostKey &key);
};

void CommanderHost::Impl::makeCommander()
{
    files::CommanderHooks hooks;
    hooks.mountsChanged = [this] {
        mountsTouched = true;
        (void)applyMounts(emu, commander->mounts());
    };
    hooks.imageChanged = [this](const std::filesystem::path &image) {
        /* the HD keeps the image in memory: read it again */
        if (emu.hdMounted() && std::filesystem::path(emu.hdPath()) == image) {
            emu.unmountHd();
            (void)emu.mountHd(image.string());
        }
    };
    hooks.quitQuestion = "Leave the commander?";   /* F10, asked, is the only way down */
    hooks.runInGuest = [](const std::string &line) { bridge::typeToGuest(line + "\r"); };
    commander.emplace(files::Mounts::fromEmulator(cli, scratchConfig), scratchConfig, std::move(hooks));
}

void CommanderHost::Impl::enter()
{
    if (!commander) makeCommander();
    else commander->refresh();
    mirror.setOutput(nullptr);
    write(kAltScreenOn);
    lastPicture.clear();
    state.commanderOn = true;
    state.panelsHidden = false;
    draw();
}

void CommanderHost::Impl::leave()
{
    state.commanderOn = false;
    write(kAltScreenOff);
    /* the machine's screen again, every cell, whatever changed meanwhile */
    mirror.setOutput(out);
    mirror.invalidate();
}

/* The guest's cursor, when the mirror has it in sight; true when it moved. */
bool CommanderHost::Impl::noteGuestCursor()
{
    if (mirror.osCursorRow() < 0 || mirror.osCursorCol() < 0) return false;
    const bool moved = mirror.osCursorRow() != guestRow || mirror.osCursorCol() != guestCol;
    guestRow = mirror.osCursorRow();
    guestCol = mirror.osCursorCol();
    return moved;
}

/* The picture, and with it where the terminal's own cursor goes: on the
 * guest's cursor cell, so the prompt blinks in the shape the terminal is
 * configured with, panels up or down alike.  A dialog takes it away. */
ftxui::Element CommanderHost::Impl::picture(int width, int height)
{
    const auto shadow = mirror.snapshot();
    (void)noteGuestCursor();
    const int row = std::clamp(guestRow >= 0 ? guestRow : mirror.lastWriteRow(), 0, VramMirror::kRows - 1);
    const bool haveCursor = guestRow >= 0 && guestCol >= 0 && !commander->modal();
    cursorRow = -1;
    cursorCol = haveCursor ? guestCol : -1;
    if (state.panelsHidden) {
        /* the guest's screen with its cursor row where it sits under the
         * panels, the rows that left the screen above it, the key bar below */
        const GuestPlacement at = placeGuest(row, height);
        if (haveCursor) cursorRow = at.pad + (row - at.from);
        ftxui::Elements rows;
        rows.push_back(guestRows(historyRows(at.pad), 0, at.pad));
        rows.push_back(guestRows(shadow, at.from, at.to));
        rows.push_back(ftxui::filler());
        rows.push_back(commander->keyBar());
        return ftxui::vbox(std::move(rows));
    }
    /* the rows around the guest's cursor: its prompt, NC's command line */
    const int from = std::clamp(row - 1, 0, VramMirror::kRows - 2);
    if (haveCursor) cursorRow = files::Commander::guestRowsTop(height) + (row - from);
    return commander->render(width, height, guestRows(shadow, from, from + 2));
}

/* The last `count` kept rows, less those leafed back, as a screen to draw. */
VramMirror::Snapshot CommanderHost::Impl::historyRows(int count) const
{
    VramMirror::Snapshot s;
    s.cells.fill(0x20);
    const auto &lines = scrollback.lines();
    const int end = static_cast<int>(lines.size()) - scrolledBack;
    for (int i = 0; i < count && i < VramMirror::kRows; ++i) {
        const int at = end - count + i;
        if (at < 0 || at >= static_cast<int>(lines.size())) continue;
        std::copy(lines[static_cast<size_t>(at)].cells.begin(), lines[static_cast<size_t>(at)].cells.end(),
                  s.cells.begin() + static_cast<long>(i) * VramMirror::kCols);
        std::copy(lines[static_cast<size_t>(at)].inverted.begin(), lines[static_cast<size_t>(at)].inverted.end(),
                  s.inverted.begin() + static_cast<long>(i) * VramMirror::kCols);
    }
    return s;
}

void CommanderHost::Impl::leafBack(int rows)
{
    const int most = static_cast<int>(scrollback.lines().size());
    scrolledBack = std::clamp(scrolledBack + rows, 0, most);
    draw();
}

void CommanderHost::Impl::draw()
{
    if (!out) return;
    const auto size = ftxui::Terminal::Size();
    lastSize = size;
    const int width = std::max(20, size.dimx);
    const int height = std::max(8, size.dimy);
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width), ftxui::Dimension::Fixed(height));
    ftxui::Render(screen, picture(width, height));
    const std::string now = screen.ToString();
    /* the terminal's own cursor on the guest's cell, its shape left alone -
     * no DECSCUSR is ever sent, so the parent terminal keeps its own */
    const std::string cursorAt = cursorRow >= 0 && cursorCol >= 0
        ? "\x1B[" + std::to_string(cursorRow + 1) + ";" + std::to_string(cursorCol + 1) + "H\x1B[?25h"
        : "\x1B[?25l";
    if (now + cursorAt == lastPicture) return;
    lastPicture = now + cursorAt;
    /* row by row at its own position: the terminal is raw, a bare '\n'
     * would not return the carriage */
    std::string painted;
    int row = 1;
    size_t at = 0;
    while (at <= now.size()) {
        size_t nl = now.find('\n', at);
        if (nl == std::string::npos) nl = now.size();
        std::string line = now.substr(at, nl - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        painted += "\x1B[" + std::to_string(row++) + ";1H" + line;
        at = nl + 1;
    }
    write((painted + cursorAt).c_str());
}

bool CommanderHost::Impl::onKey(const files::HostKey &key)
{
    state.modal = commander && state.commanderOn && commander->modal();
    const files::Route route = files::routeKey(key, state);
    state = files::afterRouting(state, key, route);
    switch (route) {
    case files::Route::toggle:
        enter();
        return true;
    case files::Route::hidePanels:
        state.panelsHidden = !state.panelsHidden;
        scrolledBack = 0;
        draw();
        return true;
    case files::Route::commander:
        if (state.panelsHidden) {
            /* only PgUp / PgDn come here with the panels hidden */
            const int page = std::max(1, ftxui::Terminal::Size().dimy - 3);
            leafBack(key.special == files::SpecialKey::pageUp ? page : -page);
            return true;
        }
        (void)commander->onEvent(files::toEvent(key));
        if (commander->takeQuitRequest()) leave();
        else draw();
        return true;
    case files::Route::guest:
        return false;
    }
    return false;
}

CommanderHost::CommanderHost(Emulator &emu, VramMirror &mirror, const app::CliArgs &cli, FILE *out)
    : impl_(std::make_unique<Impl>(emu, mirror, cli, out))
{
}

CommanderHost::~CommanderHost()
{
    if (impl_ && impl_->state.commanderOn) impl_->leave();
}

bool CommanderHost::active() const noexcept
{
    return impl_->state.commanderOn;
}

bool CommanderHost::onKey(const files::HostKey &key)
{
    return impl_->onKey(key);
}

void CommanderHost::frame()
{
    /* the rows leaving the screen are kept whether the panels are up or not */
    if (impl_->mirror.changedThisFlush()) impl_->scrollback.frame(impl_->mirror.snapshot());
    if (!impl_->state.commanderOn || !impl_->out) return;
    /* redraw when the guest wrote to its screen, moved its cursor, or the
     * terminal changed size; the keys draw on their own */
    const bool moved = impl_->noteGuestCursor();
    const auto size = ftxui::Terminal::Size();
    const bool resized = size.dimx != impl_->lastSize.dimx || size.dimy != impl_->lastSize.dimy;
    if (impl_->mirror.changedThisFlush() || moved || resized) impl_->draw();
}

void CommanderHost::shutdown(bool saveConfig)
{
    if (impl_->state.commanderOn) impl_->leave();
    if (saveConfig && impl_->mountsTouched && impl_->commander) {
        app::Config config = app::Config::load();
        impl_->commander->mounts().store(config);
        config.save();
    }
}

} /* namespace ms0515::cli */
