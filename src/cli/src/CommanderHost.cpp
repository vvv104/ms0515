/*
 * CommanderHost.cpp — the panels up and down over the machine, the keys
 * routed, the picture drawn from the frame loop.
 */
#include "CommanderHost.hpp"

#include "StdioBridge.hpp"

#include "Commander.hpp"
#include "GuestScreen.hpp"
#include "HostEvent.hpp"
#include "MountSync.hpp"
#include "Routing.hpp"

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

    Impl(Emulator &e, VramMirror &m, const app::CliArgs &c, FILE *o) : emu(e), mirror(m), cli(c), out(o) {}
    void write(const char *s) { if (out) { std::fputs(s, out); std::fflush(out); } }

    void makeCommander();
    void enter();
    void leave();
    void draw();
    [[nodiscard]] ftxui::Element picture(int width, int height);
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

ftxui::Element CommanderHost::Impl::picture(int width, int height)
{
    const auto shadow = mirror.snapshot();
    /* the guest's cursor: where it draws its '_', else where it last wrote */
    const int cursorCol = mirror.osCursorCol();
    const int cursor = std::clamp(mirror.osCursorRow() >= 0 ? mirror.osCursorRow() : mirror.lastWriteRow(), 0, VramMirror::kRows - 1);
    if (state.panelsHidden) {
        /* the guest's screen with its cursor row where it sits under the
         * panels, the key bar kept below */
        const GuestPlacement at = placeGuest(cursor, height);
        ftxui::Elements rows;
        for (int i = 0; i < at.pad; ++i) rows.push_back(ftxui::text(""));
        rows.push_back(guestRows(shadow, at.from, at.to, cursor, cursorCol));
        rows.push_back(ftxui::filler());
        rows.push_back(commander->keyBar());
        return ftxui::vbox(std::move(rows));
    }
    /* the rows around the guest's cursor: its prompt, NC's command line */
    const int from = std::clamp(cursor - 1, 0, VramMirror::kRows - 2);
    return commander->render(width, height, guestRows(shadow, from, from + 2, cursor, cursorCol));
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
    if (now == lastPicture) return;
    lastPicture = now;
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
    write(painted.c_str());
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
        draw();
        return true;
    case files::Route::commander:
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
    if (!impl_->state.commanderOn || !impl_->out) return;
    /* redraw when the guest wrote to its screen or the terminal changed
     * size; the keys draw on their own */
    const auto size = ftxui::Terminal::Size();
    const bool resized = size.dimx != impl_->lastSize.dimx || size.dimy != impl_->lastSize.dimy;
    if (impl_->mirror.changedThisFlush() || resized) impl_->draw();
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
