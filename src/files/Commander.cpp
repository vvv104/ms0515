/*
 * Commander.cpp — two panels, a key bar, dialogs and the viewer, drawn
 * with FTXUI in the blue of the classic commanders.  The keys are the web
 * commander's: F1 from the host, F2 to the host, F3 view, F5 copy, F6
 * rename / move, F7 squeeze, F8 delete, F9 init, F10 quit (asked first);
 * Tab switches the panel, Insert marks, Enter views.
 *
 * Alt+F1 / Alt+F2 (F4 for the panel in use) choose the left / right
 * panel's disk: one of the mounted devices, or another image - picked
 * from a listing of the host directory that stands in the panel for the
 * moment, the only time the host's files are on screen; the device then
 * opens where the listing was.
 */
#include "Commander.hpp"

#include "Keys.hpp"
#include "Ops.hpp"
#include "Panel.hpp"
#include "Viewer.hpp"

#include "ms0515/app/Config.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace ms0515::files {

namespace {

using namespace ftxui;

std::string today()
{
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    /* RT-11 dates run to 2099 but V5.04 shows anything past 2003 as -BAD-:
     * keep the year within what the machine reads back. */
    const int year = std::min(tm.tm_year + 1900, 2003);
    return fmt::format("{:04}-{:02}-{:02}", year, tm.tm_mon + 1, tm.tm_mday);
}

size_t lastUtf8Start(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    return i > 0 ? i - 1 : 0;
}

std::string join(const std::vector<std::string> &v, const char *sep)
{
    std::string out;
    for (const auto &s : v) { if (!out.empty()) out += sep; out += s; }
    return out;
}

/* A dialog: a message with buttons, a text prompt, or a list to pick from. */
struct Dialog {
    enum class Kind { message, prompt, list };
    Kind kind = Kind::message;
    std::string title;
    std::vector<std::string> lines;
    std::vector<std::string> buttons;   /* message: "Yes" "No" ...; the index goes to onDone */
    std::string input;                  /* prompt */
    std::vector<std::string> items;     /* list */
    int selected = 0;
    std::vector<std::string> completions;
    std::function<void(int choice, const std::string &text)> onDone;   /* choice < 0: cancelled */
};

struct ViewState {
    std::string name;
    std::vector<uint8_t> bytes;
    ViewOptions opts;
    std::vector<std::string> lines;
    int top = 0;
    std::string lastSearch;
};

/* A panel picking an image to mount: the host directory it lists. */
struct HostBrowse {
    std::filesystem::path dir;
    std::vector<HostEntry> items;
    int cursor = 0;
    int top = 0;
};

/* How many of the guest's rows go under the panels when the host gives them. */
constexpr int kGuestRows = 2;

/* The palette: blue panels, a cyan cursor and title, grey dialogs. */
const Decorator kPanel   = bgcolor(Color::Blue) | color(Color::White);
const Decorator kCursor  = bgcolor(Color::Cyan) | color(Color::Black);
const Decorator kKeyName = bgcolor(Color::Cyan) | color(Color::Black);
const Decorator kDialog  = bgcolor(Color::GrayLight) | color(Color::Black);

std::string utf8(const std::filesystem::path &p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

class Tui {
public:
    Tui(Mounts mounts, app::Config &config, CommanderHooks hooks);

    Element render(Element guest);
    bool onEvent(const Event &e);
    [[nodiscard]] bool takeQuitRequest() noexcept { const bool q = quit_; quit_ = false; return q; }
    [[nodiscard]] bool modal() const noexcept { return dialog_ || view_ || browse_[0] || browse_[1]; }
    [[nodiscard]] const Mounts &mounts() const noexcept { return mounts_; }
    void setSize(int width, int height) noexcept { width_ = width; height_ = height; }
    void refreshPanels();

private:
    Mounts mounts_;
    app::Config &config_;
    CommanderHooks hooks_;
    int width_ = 80;
    int height_ = 25;
    int guestRows_ = 0;                  /* rows of the host's screen under the panels */
    bool quit_ = false;
    Panel panels_[2];
    int active_ = 0;
    std::string status_;
    std::optional<Dialog> dialog_;
    std::optional<ViewState> view_;
    std::optional<HostBrowse> browse_[2];
    std::filesystem::path lastDir_;      /* where the picker starts */

    [[nodiscard]] Panel &panel() { return panels_[active_]; }
    [[nodiscard]] Panel &other() { return panels_[1 - active_]; }
    [[nodiscard]] int panelRows() const { return std::max(3, height_ - 6 - guestRows_); }

    /* drawing */
    Element renderPanel(int index);
    Element renderHost(int index);
    Element renderKeyBar() const;
    Element renderDialog() const;
    Element renderViewer() const;

    /* keys */
    bool onBrowseKey(const Event &e);
    bool onHostKey(const Event &e);
    bool onDialogKey(const Event &e);
    bool onViewerKey(const Event &e);
    void completeInput();

    /* the actions behind the keys */
    void openDevices(int panelIndex);
    void showDevice(const Device &device);
    void changed(Panel &p);
    void startHostBrowse(int panelIndex, const std::filesystem::path &dir);
    void mountPicked(int panelIndex, const std::filesystem::path &image);
    void doMount(Slot slot, const std::string &path, int side, bool force);
    void doImport();
    void doExport();
    void doView();
    void doCopy(bool move);
    void doRename();
    void doSqueeze();
    void doDelete();
    void doInit();
    void protectedGuard(const std::vector<Entry> &entries, const char *verb, const std::function<void(Policy)> &go);
    void report(const OpResult &r, const char *verb);
    void message(const std::string &title, const std::vector<std::string> &lines);
    void ask(const std::string &title, const std::vector<std::string> &lines, std::vector<std::string> buttons,
             std::function<void(int)> onDone);
    void prompt(const std::string &title, const std::string &line, const std::string &initial,
                std::function<void(const std::string &)> onDone);
    void pick(const std::string &title, std::vector<std::string> items, std::function<void(int)> onDone);
};

Tui::Tui(Mounts mounts, app::Config &config, CommanderHooks hooks)
    : mounts_(std::move(mounts)), config_(config), hooks_(std::move(hooks))
{
    const auto devices = mounts_.devices();
    if (!devices.empty()) showDevice(devices[0]);
    if (devices.size() > 1) { active_ = 1; showDevice(devices[1]); active_ = 0; }
    std::error_code ec;
    lastDir_ = devices.empty() ? std::filesystem::current_path(ec) : devices[0].image.parent_path();
    status_ = devices.empty() ? "no disks mounted - Alt+F1 / Alt+F2 mount an image into the left / right panel"
                              : "Alt+F1 / Alt+F2 the left / right panel's disk, Tab the other panel, F10 leaves";
}

/* The guest's rows go between the panels and the status line: NC's
 * command line, here the machine's own prompt. */
Element Tui::render(Element guest)
{
    if (view_) return renderViewer();
    guestRows_ = 0;
    const int left = width_ / 2;   /* strictly halves, whatever the panels hold */
    Element panels = hbox({renderPanel(0) | size(WIDTH, EQUAL, left), renderPanel(1) | size(WIDTH, EQUAL, width_ - left)});
    Elements rows = {panels | flex};
    if (guest) { rows.push_back(guest); guestRows_ = kGuestRows; }
    rows.push_back(text(" " + status_));
    rows.push_back(renderKeyBar());
    Element page = vbox(rows);
    if (dialog_) return dbox({page, renderDialog() | center});
    return page;
}

Element Tui::renderPanel(int index)
{
    if (browse_[static_cast<size_t>(index)]) return renderHost(index);
    Panel &p = panels_[index];
    const bool isActive = index == active_;
    const int rows = panelRows();
    Elements lines;
    const auto &entries = p.entries();
    const int top = p.scrollTop(rows);
    for (int i = top; i < top + rows; ++i) {
        if (i >= static_cast<int>(entries.size())) { lines.push_back(text("")); continue; }
        const Entry &e = entries[static_cast<size_t>(i)];
        std::string line = fmt::format(" {:<10} {:>5} {:>10} {} ", e.name, e.blocks, e.date, e.protectedFlag ? "P" : " ");
        Element el = text(line);
        if (p.isMarked(e.name)) el = el | color(Color::Yellow) | bold;
        if (i == p.cursor() && isActive) el = el | kCursor;   /* the other panel shows no cursor, as mc does */
        lines.push_back(el);
    }
    Element title = text(" " + p.title() + " ") | bold;
    if (isActive) title = title | kCursor;
    std::string foot = p.hasLocation() ? p.location().summary() : (index == 0 ? "Alt+F1 picks a disk" : "Alt+F2 picks a disk");
    if (p.hasLocation() && !p.location().volumeId().empty()) foot += " - " + p.location().volumeId();
    if (p.markedCount()) foot += fmt::format(" - {} marked", p.markedCount());
    return vbox({title | hcenter, separator(), vbox(lines) | flex, separator(), text(" " + foot)}) | border | kPanel;
}

Element Tui::renderHost(int index)
{
    const HostBrowse &b = *browse_[static_cast<size_t>(index)];
    const bool isActive = index == active_;
    const int rows = panelRows();
    Elements lines;
    for (int i = b.top; i < b.top + rows; ++i) {
        if (i >= static_cast<int>(b.items.size())) { lines.push_back(text("")); continue; }
        const HostEntry &h = b.items[static_cast<size_t>(i)];
        Element el = h.directory ? text(fmt::format(" {:<24.24} <DIR>", h.name)) | bold
                                 : text(fmt::format(" {:<24.24} {:>6} blk", h.name, h.bytes / 512));
        if (i == b.cursor && isActive) el = el | kCursor;
        lines.push_back(el);
    }
    Element title = text(" host: " + utf8(b.dir) + " ") | bold;
    if (isActive) title = title | kCursor;
    const char *foot = " Enter mounts the image / enters the directory, Esc back";
    return vbox({title | hcenter, separator(), vbox(lines) | flex, separator(), text(foot)}) | border | kPanel;
}

Element Tui::renderKeyBar() const
{
    const std::vector<std::pair<const char *, const char *>> keys = {
        {"1", "From"}, {"2", "To"}, {"3", "View"}, {"4", "Disk"}, {"5", "Copy"},
        {"6", "RenMov"}, {"7", "Squeez"}, {"8", "Delete"}, {"9", "Init"}, {"10", "Quit"}};
    Elements items;
    for (const auto &[num, name] : keys)
        items.push_back(hbox({text(num), text(name) | kKeyName | flex}) | flex);
    return hbox(items);
}

Element Tui::renderDialog() const
{
    const Dialog &d = *dialog_;
    Elements body;
    for (const auto &l : d.lines) body.push_back(text(l));
    if (d.kind == Dialog::Kind::prompt) {
        body.push_back(text("> " + d.input + "_") | inverted);
        for (const auto &c : d.completions) body.push_back(text("  " + c) | dim);
    }
    if (d.kind == Dialog::Kind::list) {
        for (int i = 0; i < static_cast<int>(d.items.size()); ++i) {
            Element el = text(" " + d.items[static_cast<size_t>(i)] + " ");
            body.push_back(i == d.selected ? el | inverted : el);
        }
    }
    if (d.kind == Dialog::Kind::message) {
        Elements buttons;
        for (int i = 0; i < static_cast<int>(d.buttons.size()); ++i) {
            Element b = text("[ " + d.buttons[static_cast<size_t>(i)] + " ]");
            buttons.push_back(i == d.selected ? b | inverted : b);
            buttons.push_back(text("  "));
        }
        body.push_back(text(""));
        body.push_back(hbox(buttons) | hcenter);
    }
    return window(text(" " + d.title + " ") | bold, vbox(body)) | kDialog;
}

Element Tui::renderViewer() const
{
    const ViewState &v = *view_;
    const int rows = std::max(1, height_ - 2);
    Elements lines;
    for (int i = v.top; i < v.top + rows; ++i)
        lines.push_back(text(i < static_cast<int>(v.lines.size()) ? v.lines[static_cast<size_t>(i)] : ""));
    const std::string head = fmt::format(" {}  {} bytes  {}  {}  line {}/{}", v.name, v.bytes.size(),
                                         viewName(v.opts.view), encodingName(v.opts.encoding),
                                         v.top + 1, v.lines.size());
    const std::string keys = " 1 text/octal/hex  2 wrap  4 encoding  5 go to  7 search  3/10 back ";
    return vbox({text(head) | kCursor, vbox(lines) | flex | kPanel, text(keys) | kCursor});
}

bool Tui::onEvent(const Event &e)
{
    if (dialog_) return onDialogKey(e);
    if (view_) return onViewerKey(e);
    if (const auto fk = parseFunctionKey(e.input()); fk && fk->alt && (fk->number == 1 || fk->number == 2)) {
        openDevices(fk->number - 1);
        return true;
    }
    if (browse_[static_cast<size_t>(active_)]) return onHostKey(e);
    return onBrowseKey(e);
}

bool Tui::onHostKey(const Event &e)
{
    HostBrowse &b = *browse_[static_cast<size_t>(active_)];
    const int n = static_cast<int>(b.items.size());
    const int rows = panelRows();
    const auto moveTo = [&](int cursor) {
        b.cursor = std::clamp(cursor, 0, std::max(0, n - 1));
        if (b.cursor < b.top) b.top = b.cursor;
        if (b.cursor >= b.top + rows) b.top = b.cursor - rows + 1;
    };
    if (e == Event::Escape || e == Event::F10) { browse_[static_cast<size_t>(active_)].reset(); status_ = "nothing mounted"; return true; }
    if (e == Event::Tab || e == Event::TabReverse) { active_ = 1 - active_; return true; }
    if (e == Event::ArrowUp)   { moveTo(b.cursor - 1); return true; }
    if (e == Event::ArrowDown) { moveTo(b.cursor + 1); return true; }
    if (e == Event::PageUp)    { moveTo(b.cursor - (rows - 1)); return true; }
    if (e == Event::PageDown)  { moveTo(b.cursor + (rows - 1)); return true; }
    if (e == Event::Home)      { moveTo(0); return true; }
    if (e == Event::End)       { moveTo(n - 1); return true; }
    if (e == Event::Backspace) { startHostBrowse(active_, b.dir.parent_path()); return true; }
    if (e == Event::Return && n > 0) {
        const HostEntry h = b.items[static_cast<size_t>(b.cursor)];
        if (h.directory) startHostBrowse(active_, h.path);
        else mountPicked(active_, h.path);
        return true;
    }
    return true;
}

bool Tui::onBrowseKey(const Event &e)
{
    Panel &p = panel();
    if (e == Event::Tab || e == Event::TabReverse) { active_ = 1 - active_; return true; }
    if (e == Event::ArrowUp)   { p.moveCursor(-1); return true; }
    if (e == Event::ArrowDown) { p.moveCursor(1); return true; }
    if (e == Event::PageUp)    { p.pageUp(panelRows() - 1); return true; }
    if (e == Event::PageDown)  { p.pageDown(panelRows() - 1); return true; }
    if (e == Event::Home)      { p.home(); return true; }
    if (e == Event::End)       { p.end(); return true; }
    if (e == Event::Insert)    { p.toggleMark(); return true; }
    if (e == Event::Return)    { doView(); return true; }
    if (e == Event::F1)  { doImport(); return true; }
    if (e == Event::F2)  { doExport(); return true; }
    if (e == Event::F3)  { doView(); return true; }
    if (e == Event::F4)  { openDevices(active_); return true; }
    if (e == Event::F5)  { doCopy(false); return true; }
    if (e == Event::F6)  { doRename(); return true; }
    if (e == Event::F7)  { doSqueeze(); return true; }
    if (e == Event::F8)  { doDelete(); return true; }
    if (e == Event::F9)  { doInit(); return true; }
    if (e == Event::F10) {
        if (hooks_.quitQuestion.empty()) { quit_ = true; return true; }
        ask("Quit", {hooks_.quitQuestion}, {"Yes", "No"}, [this](int c) { if (c == 0) quit_ = true; });
        return true;
    }
    if (e == Event::Character('r') || e == Event::Character('R')) { refreshPanels(); return true; }
    return false;
}

bool Tui::onDialogKey(const Event &e)
{
    Dialog &d = *dialog_;
    auto finish = [this](int choice, const std::string &text) {
        auto done = std::move(dialog_->onDone);
        dialog_.reset();
        if (done) done(choice, text);
    };
    if (e == Event::Escape) { finish(-1, ""); return true; }
    switch (d.kind) {
    case Dialog::Kind::message:
        if (e == Event::ArrowLeft || e == Event::TabReverse) d.selected = std::max(0, d.selected - 1);
        else if (e == Event::ArrowRight || e == Event::Tab) d.selected = std::min(static_cast<int>(d.buttons.size()) - 1, d.selected + 1);
        else if (e == Event::Return) finish(d.selected, "");
        else if (e.is_character()) {   /* the first letter of a button answers too */
            for (int i = 0; i < static_cast<int>(d.buttons.size()); ++i)
                if (std::tolower(d.buttons[static_cast<size_t>(i)][0]) == std::tolower(e.character()[0])) { finish(i, ""); break; }
        }
        return true;
    case Dialog::Kind::prompt:
        if (e == Event::Return) finish(0, d.input);
        else if (e == Event::Backspace) { if (!d.input.empty()) d.input.erase(lastUtf8Start(d.input)); d.completions.clear(); }
        else if (e == Event::Tab) completeInput();
        else if (e.is_character()) { d.input += e.character(); d.completions.clear(); }
        return true;
    case Dialog::Kind::list:
        if (e == Event::ArrowUp) d.selected = std::max(0, d.selected - 1);
        else if (e == Event::ArrowDown) d.selected = std::min(static_cast<int>(d.items.size()) - 1, d.selected + 1);
        else if (e == Event::Return) finish(d.selected, "");
        return true;
    }
    return true;
}

void Tui::completeInput()
{
    Dialog &d = *dialog_;
    auto cands = completePath(d.input);
    std::sort(cands.begin(), cands.end());
    if (cands.empty()) { d.completions = {"(nothing matches)"}; return; }
    std::string common = cands[0];
    for (const auto &c : cands)
        common.resize(std::mismatch(common.begin(), common.end(), c.begin(), c.end()).first - common.begin());
    if (common.size() > d.input.size()) d.input = common;
    d.completions = cands.size() == 1 ? std::vector<std::string>{} : cands;
    if (d.completions.size() > 12) { d.completions.resize(12); d.completions.push_back("..."); }
}

bool Tui::onViewerKey(const Event &e)
{
    ViewState &v = *view_;
    const int rows = std::max(1, height_ - 2);
    const int maxTop = std::max(0, static_cast<int>(v.lines.size()) - rows);
    auto rerender = [&] { v.lines = renderLines(v.bytes, v.opts); v.top = std::min(v.top, std::max(0, static_cast<int>(v.lines.size()) - rows)); };
    if (e == Event::Escape || e == Event::F3 || e == Event::F10) { view_.reset(); return true; }
    if (e == Event::ArrowUp)   v.top = std::max(0, v.top - 1);
    else if (e == Event::ArrowDown) v.top = std::min(maxTop, v.top + 1);
    else if (e == Event::PageUp)    v.top = std::max(0, v.top - rows);
    else if (e == Event::PageDown)  v.top = std::min(maxTop, v.top + rows);
    else if (e == Event::Home)      v.top = 0;
    else if (e == Event::End)       v.top = maxTop;
    else if (e == Event::F1) { v.opts.view = nextView(v.opts.view); rerender(); }
    else if (e == Event::F2) { v.opts.wrap = !v.opts.wrap; rerender(); }
    else if (e == Event::F4) { v.opts.encoding = nextEncoding(v.opts.encoding); rerender(); }
    else if (e == Event::F5) {
        prompt("Go to line", fmt::format("1..{}", v.lines.size()), "", [this, rows](const std::string &s) {
            try { view_->top = std::clamp(std::stoi(s) - 1, 0, std::max(0, static_cast<int>(view_->lines.size()) - rows)); } catch (...) {}
        });
    } else if (e == Event::F7) {
        prompt("Search", "a string, in the file's encoding (" + std::string(encodingName(v.opts.encoding)) + ")", v.lastSearch,
               [this, rows](const std::string &s) {
            ViewState &vs = *view_;
            vs.lastSearch = s;
            const auto needle = encodeString(s, vs.opts.encoding);
            if (!needle) { status_ = "not in this encoding"; return; }
            const size_t from = vs.opts.view == View::text ? 0 : static_cast<size_t>(vs.top) * 16;
            const auto at = findBytes(vs.bytes, *needle, from);
            if (!at) { status_ = "not found"; return; }
            if (vs.opts.view != View::text) vs.top = static_cast<int>(*at / 16);
            else {
                /* the line the offset falls in: count line breaks before it */
                int line = 0, col = 0;
                for (size_t i = 0; i < *at; ++i) { if (vs.bytes[i] == '\n') { ++line; col = 0; } else if (++col >= 80 && vs.opts.wrap) { ++line; col = 0; } }
                vs.top = line;
            }
            vs.top = std::clamp(vs.top, 0, std::max(0, static_cast<int>(vs.lines.size()) - rows));
        });
    }
    return true;
}

void Tui::openDevices(int panelIndex)
{
    active_ = panelIndex;
    const auto devices = mounts_.devices();
    std::vector<std::string> items;
    for (const auto &d : devices) items.push_back(d.label());
    items.push_back("Mount another image...");
    const auto forSlot = [&](Slot s, const char *name) {
        if (const auto m = mounts_.mounted(s))
            items.push_back(std::string("Unmount ") + name + " (" + m->image.filename().string() + ")");
    };
    forSlot(Slot::driveA, "drive A");
    forSlot(Slot::driveB, "drive B");
    forSlot(Slot::hd, "HD");
    pick(panelIndex == 0 ? "Left panel" : "Right panel", items, [this, panelIndex, devices, items](int i) {
        if (i < 0) return;
        if (i < static_cast<int>(devices.size())) {
            browse_[static_cast<size_t>(panelIndex)].reset();
            showDevice(devices[static_cast<size_t>(i)]);
            return;
        }
        const std::string &item = items[static_cast<size_t>(i)];
        if (item.rfind("Mount", 0) == 0) { startHostBrowse(panelIndex, lastDir_); return; }
        const Slot slot = item.find("drive A") != std::string::npos ? Slot::driveA
                        : item.find("drive B") != std::string::npos ? Slot::driveB : Slot::hd;
        mounts_.unmount(slot);
        mounts_.store(config_);
        if (hooks_.mountsChanged) hooks_.mountsChanged();
        refreshPanels();
        status_ = item;
    });
}

void Tui::showDevice(const Device &device)
{
    auto loc = Location::open(device);
    if (!loc) { status_ = device.label() + ": cannot open"; return; }
    panel().show(*loc);
    status_ = device.label();
}

/* A volume was written: re-read it here, and tell the host, whose
 * machine may keep a copy of the image. */
void Tui::changed(Panel &p)
{
    p.reload();
    if (p.hasLocation() && hooks_.imageChanged) hooks_.imageChanged(p.location().device().image);
    for (Panel &q : panels_)
        if (&q != &p && q.hasLocation() && p.hasLocation() && q.location().device().image == p.location().device().image) q.reload();
}

void Tui::refreshPanels()
{
    const auto devices = mounts_.devices();
    for (Panel &p : panels_) {
        if (!p.hasLocation()) continue;
        const std::string name = p.location().device().name;
        const auto d = std::find_if(devices.begin(), devices.end(), [&](const Device &x) { return x.name == name; });
        if (d == devices.end() || d->image != p.location().device().image) { p.clear(); continue; }
        if (!p.location().reload()) p.clear(); else p.reload();
    }
}

void Tui::startHostBrowse(int panelIndex, const std::filesystem::path &dir)
{
    HostBrowse b;
    std::error_code ec;
    b.dir = std::filesystem::canonical(dir, ec);
    if (ec) b.dir = dir;
    b.items = listImages(b.dir);
    browse_[static_cast<size_t>(panelIndex)] = std::move(b);
    active_ = panelIndex;
    status_ = "pick the image to mount - Enter; Esc keeps the panel as it was";
}

/* An image chosen in the listing: which slot (and side) it goes into,
 * then the mount, and the device opens in the panel that listed it. */
void Tui::mountPicked(int panelIndex, const std::filesystem::path &image)
{
    lastDir_ = image.parent_path();
    std::error_code ec;
    const auto size = std::filesystem::file_size(image, ec);
    if (ec) { message("Mount", {image.string() + ": cannot read"}); return; }
    const bool floppy = size == 409600 || size == 819200;
    const std::vector<std::string> buttons = floppy ? std::vector<std::string>{"Drive A", "Drive B", "Cancel"}
                                                    : std::vector<std::string>{"HD", "Cancel"};
    const std::string path = image.string();
    ask("Mount", {image.filename().string(), Mounts::describe(image), "where?"}, buttons,
        [this, panelIndex, path, size, floppy](int c) {
        const int cancel = floppy ? 2 : 1;
        if (c < 0 || c == cancel) return;
        const Slot slot = !floppy ? Slot::hd : c == 0 ? Slot::driveA : Slot::driveB;
        auto go = [this, panelIndex, slot, path](int side) {
            browse_[static_cast<size_t>(panelIndex)].reset();
            active_ = panelIndex;
            doMount(slot, path, side, false);
        };
        if (size == 409600) {
            ask("Mount", {"which side of the drive?"}, {"side 0", "side 1", "Cancel"},
                [go](int s) { if (s == 0 || s == 1) go(s); });
            return;
        }
        go(0);
    });
}

void Tui::doMount(Slot slot, const std::string &path, int side, bool force)
{
    const auto why = mounts_.mount(slot, path, side, force);
    if (why.empty()) {
        mounts_.store(config_);
        if (hooks_.mountsChanged) hooks_.mountsChanged();
        refreshPanels();
        const auto devices = mounts_.devices();
        for (const auto &d : devices)
            if (d.image == std::filesystem::path(path)) { showDevice(d); break; }
        return;
    }
    if (!force && why.find("no RT-11 volume") != std::string::npos) {
        ask("Mount", {why, "Mount it anyway, to initialise it with F9?"}, {"Yes", "No"},
            [this, slot, path, side](int c) { if (c == 0) doMount(slot, path, side, true); });
        return;
    }
    message("Mount", {why});
}

void Tui::doImport()
{
    if (!panel().hasLocation()) { status_ = "no disk in this panel"; return; }
    prompt("From the host", "a host file to put on " + panel().location().device().name + " (Tab completes)", "",
           [this](const std::string &path) {
        if (path.empty()) return;
        Policy policy;
        policy.date = today();
        Location &to = panel().location();
        const std::string name = Location::toVolumeName(std::filesystem::path(path).filename().string());
        auto go = [this, path, policy, &to]() mutable {
            report(importFiles({path}, to, policy), "brought in");
            changed(panel());
        };
        if (to.find(name)) {
            ask("From the host", {name + " is already on " + to.device().name}, {"Overwrite", "Cancel"},
                [go, policy](int c) mutable { if (c == 0) { policy.overwrite = true; policy.touchProtected = true; go(); } });
            return;
        }
        go();
    });
}

void Tui::doExport()
{
    if (!panel().hasLocation() || panel().selection().empty()) { status_ = "nothing to put out"; return; }
    const auto sel = panel().selection();
    prompt("To the host", fmt::format("a host directory for {} file(s) (Tab completes)", sel.size()), "",
           [this, sel](const std::string &dir) {
        if (dir.empty()) return;
        Policy policy;
        policy.overwrite = true;
        report(exportFiles(panel().location(), sel, dir, policy), "put out");
    });
}

void Tui::doView()
{
    if (!panel().hasLocation()) return;
    const auto cur = panel().current();
    if (!cur) return;
    const auto bytes = panel().location().read(cur->name);
    if (!bytes) { status_ = cur->name + ": cannot read"; return; }
    ViewState v;
    v.name = panel().location().device().name + cur->name;
    v.bytes = *bytes;
    v.lines = renderLines(v.bytes, v.opts);
    view_ = std::move(v);
}

void Tui::doCopy(bool move)
{
    if (!panel().hasLocation() || !other().hasLocation()) { status_ = "both panels need a disk"; return; }
    const auto sel = panel().selection();
    if (sel.empty()) return;
    Location &from = panel().location();
    Location &to = other().location();
    if (from.device().image == to.device().image && from.device().spec.side == to.device().spec.side && from.device().spec.vol == to.device().spec.vol) {
        status_ = "the same volume on both sides";
        return;
    }
    const char *verb = move ? "Move" : "Copy";
    const auto clash = clashes(sel, to);
    auto go = [this, sel, move, &from, &to](Policy policy) {
        report(move ? moveFiles(from, sel, to, policy) : copyFiles(from, sel, to, policy), move ? "moved" : "copied");
        panel().clearMarks();
        changed(panel());
        changed(other());
    };
    ask(verb, {fmt::format("{} {} file(s) to {}?", verb, sel.size(), to.device().name)}, {"Yes", "No"}, [this, clash, go, verb, move, sel](int c) {
        if (c != 0) return;
        auto after = [this, go, move, sel](Policy policy) {
            if (move) protectedGuard(sel, "move", go); else go(policy);
        };
        if (clash.empty()) { after(Policy{}); return; }
        ask(verb, {fmt::format("{} already there: {}", clash.size(), join(clash, ", "))}, {"Overwrite", "Skip", "Cancel"},
            [after](int cc) { if (cc == 2) return; Policy p; p.overwrite = (cc == 0); p.touchProtected = (cc == 0); after(p); });
    });
}

void Tui::doRename()
{
    if (!panel().hasLocation()) return;
    const auto sel = panel().selection();
    if (sel.empty()) return;
    if (sel.size() > 1 || panel().markedCount() > 0) { doCopy(true); return; }   /* several: a move to the other panel */
    const Entry entry = sel[0];
    prompt("Rename", entry.name + " to (NAME.EXT)", entry.name, [this, entry](const std::string &name) {
        if (name.empty() || name == entry.name) return;
        std::string upper = name;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        protectedGuard({entry}, "rename", [this, entry, upper](Policy policy) {
            report(renameFile(panel().location(), entry, upper, policy), "renamed");
            changed(panel());
        });
    });
}

void Tui::doSqueeze()
{
    if (!panel().hasLocation()) return;
    ask("Squeeze", {"Squeeze " + panel().location().device().name + " (gather the free space)?"}, {"Yes", "No"}, [this](int c) {
        if (c != 0) return;
        const auto why = panel().location().squeeze();
        status_ = why.empty() ? "squeezed" : why;
        changed(panel());
    });
}

void Tui::doDelete()
{
    if (!panel().hasLocation()) return;
    const auto sel = panel().selection();
    if (sel.empty()) return;
    ask("Delete", {fmt::format("Delete {} file(s) from {}?", sel.size(), panel().location().device().name), join([&] {
            std::vector<std::string> n; for (const auto &e : sel) n.push_back(e.name); return n; }(), " ")},
        {"Yes", "No"}, [this, sel](int c) {
        if (c != 0) return;
        protectedGuard(sel, "delete", [this, sel](Policy policy) {
            report(deleteFiles(panel().location(), sel, policy), "deleted");
            panel().clearMarks();
            changed(panel());
        });
    });
}

void Tui::doInit()
{
    if (!panel().hasLocation()) return;
    const std::string dev = panel().location().device().name;
    ask("Init", {"Initialise " + dev + " - every file on it is lost!"}, {"Yes", "No"}, [this, dev](int c) {
        if (c != 0) return;
        prompt("Init", "the volume ID (up to 12 characters)", "RT11A", [this](const std::string &id) {
            disk::InitOptions opts;
            if (!id.empty()) opts.volumeId = id.substr(0, 12);
            const auto why = panel().location().init(opts);
            status_ = why.empty() ? "initialised" : why;
            changed(panel());
        });
    });
}

void Tui::protectedGuard(const std::vector<Entry> &entries, const char *verb, const std::function<void(Policy)> &go)
{
    const auto prot = protectedOnes(entries);
    if (prot.empty()) { go(Policy{}); return; }
    ask("Protected", {fmt::format("{} protected: {}", prot.size(), join(prot, ", ")), std::string(verb) + " them anyway?"},
        {"Yes", "No"}, [go](int c) { Policy p; p.touchProtected = (c == 0); go(p); });
}

void Tui::report(const OpResult &r, const char *verb)
{
    status_ = fmt::format("{} {}", r.done, verb);
    if (!r.errors.empty()) {
        status_ += fmt::format(", {} failed", r.errors.size());
        message("Not done", r.errors);
    }
}

void Tui::message(const std::string &title, const std::vector<std::string> &lines)
{
    Dialog d;
    d.kind = Dialog::Kind::message;
    d.title = title;
    d.lines = lines;
    d.buttons = {"OK"};
    dialog_ = std::move(d);
}

void Tui::ask(const std::string &title, const std::vector<std::string> &lines, std::vector<std::string> buttons,
              std::function<void(int)> onDone)
{
    Dialog d;
    d.kind = Dialog::Kind::message;
    d.title = title;
    d.lines = lines;
    d.buttons = std::move(buttons);
    d.onDone = [onDone = std::move(onDone)](int c, const std::string &) { onDone(c); };
    dialog_ = std::move(d);
}

void Tui::prompt(const std::string &title, const std::string &line, const std::string &initial,
                 std::function<void(const std::string &)> onDone)
{
    Dialog d;
    d.kind = Dialog::Kind::prompt;
    d.title = title;
    d.lines = {line};
    d.input = initial;
    d.onDone = [onDone = std::move(onDone)](int c, const std::string &s) { if (c >= 0) onDone(s); };
    dialog_ = std::move(d);
}

void Tui::pick(const std::string &title, std::vector<std::string> items, std::function<void(int)> onDone)
{
    Dialog d;
    d.kind = Dialog::Kind::list;
    d.title = title;
    d.items = std::move(items);
    d.onDone = [onDone = std::move(onDone)](int c, const std::string &) { onDone(c); };
    dialog_ = std::move(d);
}

} // namespace

struct Commander::Impl {
    Tui tui;
};

Commander::Commander(Mounts mounts, app::Config &config, CommanderHooks hooks)
    : impl_(std::make_unique<Impl>(Impl{Tui(std::move(mounts), config, std::move(hooks))}))
{
}

Commander::~Commander() = default;

Element Commander::render(int width, int height, Element guest)
{
    impl_->tui.setSize(width, height);
    return impl_->tui.render(std::move(guest));
}

bool Commander::onEvent(const Event &event)
{
    return impl_->tui.onEvent(event);
}

bool Commander::takeQuitRequest() noexcept
{
    return impl_->tui.takeQuitRequest();
}

bool Commander::modal() const noexcept
{
    return impl_->tui.modal();
}

const Mounts &Commander::mounts() const noexcept
{
    return impl_->tui.mounts();
}

void Commander::refresh()
{
    impl_->tui.refreshPanels();
}

int runCommander(Mounts mounts, app::Config &config, const std::string &quitQuestion)
{
    CommanderHooks hooks;
    hooks.quitQuestion = quitQuestion;
    Commander commander(std::move(mounts), config, std::move(hooks));
    auto screen = ScreenInteractive::Fullscreen();
    auto component = Renderer([&] { return commander.render(screen.dimx(), screen.dimy()); })
                   | CatchEvent([&](const Event &e) {
                         const bool used = commander.onEvent(e);
                         if (commander.takeQuitRequest()) screen.ExitLoopClosure()();
                         return used;
                     });
    screen.TrackMouse(false);   /* keyboard only - and a terminal left in mouse-tracking mode after a crash is a mess */
    screen.Loop(component);
    return 0;
}

} /* namespace ms0515::files */
