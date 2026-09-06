/*
 * Commander.cpp — the page and the panels, the way Midnight Commander
 * draws them, and the keys that work them.  The dialogs and menus are in
 * CommanderDialogs.cpp, the actions behind the keys in
 * CommanderActions.cpp, the viewer in CommanderViewer.cpp.
 *
 * The keys are mc's: F1 help, F2 the user menu, F3 view, F4 the panel's
 * disk (mc edits; the machine's disks have no editor here), F5 copy, F6
 * rename / move, F7 squeeze (mc makes a directory; a volume has none), F8
 * delete, F9 the menu, F10 quit; Tab the other panel, Insert marks, + - *
 * select by pattern, Ctrl+U swaps the panels, Ctrl+R re-reads, Enter
 * views.  Alt+F1 / Alt+F2 choose the left / right panel's disk: one of
 * the mounted devices, or another image - picked from a listing of the
 * host directory that stands in the panel for the moment, the only time
 * the host's files are on screen; the device then opens where the
 * listing was.
 */
#include "TuiImpl.hpp"

#include "Keys.hpp"

#include "ms0515/app/Config.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <system_error>

namespace ms0515::files::detail {

using namespace ftxui;

namespace {

/* One column of the listing: the header, then a cell per row, the
 * cursor row and the marked rows coloured cell by cell so the bar runs
 * across the rules between the columns, as mc draws it. */
struct Column {
    std::string header;
    int width = 0;                  /* 0: takes what is left */
    std::vector<std::string> cells;
    std::vector<Decorator> looks;   /* per cell */
};

Element renderColumn(const Column &c)
{
    Elements cells = {text(c.header) | kHeader};
    for (size_t i = 0; i < c.cells.size(); ++i) cells.push_back(text(c.cells[i]) | c.looks[i]);
    Element v = vbox(std::move(cells));
    return c.width > 0 ? v | size(WIDTH, EQUAL, c.width) : v | flex;
}

/* The columns side by side with a rule between each two, the whole
 * height of the listing. */
Element renderColumns(const std::vector<Column> &columns)
{
    Elements parts;
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) parts.push_back(separator());
        parts.push_back(renderColumn(columns[i]));
    }
    return hbox(std::move(parts));
}

const std::vector<std::pair<const char *, const char *>> kPanelKeys = {
    {"1", "Help"}, {"2", "Menu"}, {"3", "View"}, {"4", "Disk"}, {"5", "Copy"},
    {"6", "RenMov"}, {"7", "Squeez"}, {"8", "Delete"}, {"9", "PullDn"}, {"10", "Quit"}};

} // namespace

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

std::string utf8(const std::filesystem::path &p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string join(const std::vector<std::string> &v, const char *sep)
{
    std::string out;
    for (const auto &s : v) { if (!out.empty()) out += sep; out += s; }
    return out;
}

std::string upperCase(std::string s)
{
    for (auto &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

size_t lastUtf8Start(const std::string &s)
{
    size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    return i > 0 ? i - 1 : 0;
}

Tui::Tui(Mounts mounts, app::Config &config, CommanderHooks hooks)
    : mounts_(std::move(mounts)), config_(config), hooks_(std::move(hooks))
{
    buildMenus();
    const auto devices = mounts_.devices();
    if (!devices.empty()) showDevice(devices[0]);
    if (devices.size() > 1) { active_ = 1; showDevice(devices[1]); active_ = 0; }
    std::error_code ec;
    lastDir_ = devices.empty() ? std::filesystem::current_path(ec) : devices[0].image.parent_path();
    newFileDate_ = today();
    status_ = devices.empty() ? "Hint: Alt-F1 / Alt-F2 mount an image into the left / right panel."
                              : "Hint: Tab changes your current panel.";
}

/* The guest's rows go between the panels and the hint line: mc's
 * command line, here the machine's own prompt. */
Element Tui::render(Element guest)
{
    if (view_) return renderViewer();
    guestRows_ = guest ? kGuestRows : 0;   /* before the panels: their height depends on it */
    const int left = width_ / 2;           /* strictly halves, whatever the panels hold */
    Element panels = hbox({renderPanel(0) | size(WIDTH, EQUAL, left), renderPanel(1) | size(WIDTH, EQUAL, width_ - left)});
    Elements rows = {renderMenuBar(), panels | flex};
    if (guest) rows.push_back(guest);
    rows.push_back(text(" " + status_));
    rows.push_back(renderKeyBar(kPanelKeys));
    Element page = vbox(rows);
    if (menu_ >= 0 && menuDown_) page = dbox({page, renderMenu()});
    if (dialog_) page = dbox({page, renderDialog() | center});
    return page;
}

Element Tui::renderMenuBar() const
{
    Elements items = {text("  ")};
    for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
        Element name = text(" " + menus_[static_cast<size_t>(i)].name + " ");
        items.push_back(i == menu_ ? name | kBarOpen : name);
        items.push_back(text("    "));
    }
    return hbox({hbox(items), filler()}) | kBar;
}

/* mc's frame: the title on the top border, the summary on the bottom one;
 * inside, the columns, a rule (carrying the marks summary when there is
 * one) and the line of the current entry.  No bold on the title: bold
 * black is grey on most terminals. */
Element Tui::frame(bool isActive, const std::string &title, Element columns, Element rule, Element info, const std::string &foot)
{
    Element head = text(" " + title + " ");
    if (isActive) head = head | kCursor;
    Element body = vbox({std::move(columns) | flex, std::move(rule), std::move(info)});
    Element panel = window(head | hcenter, body) | kPanel;
    /* only the text is painted: a colour on the whole overlay would
     * repaint every cell of the panel, cursor and headers included */
    Element bottom = vbox({filler(), text(" " + foot + " ") | kPanel | hcenter});
    return dbox({panel, bottom});
}

Element Tui::renderPanel(int index)
{
    if (browse_[static_cast<size_t>(index)]) return renderHost(index);
    Panel &p = panels_[index];
    const bool isActive = index == active_;
    const int rows = panelRows();
    std::vector<Column> cols = {{" Name", 0, {}, {}}, {"  Blk", 5, {}, {}}, {"   Date", 10, {}, {}}, {"P", 1, {}, {}}};
    const auto &entries = p.entries();
    const int top = p.scrollTop(rows);
    for (int i = top; i < top + rows; ++i) {
        const bool have = i < static_cast<int>(entries.size());
        const Entry e = have ? entries[static_cast<size_t>(i)] : Entry{};
        Decorator look = nothing;
        if (have && p.isMarked(e.name)) look = kMarked;
        if (have && i == p.cursor() && isActive) look = look | kCursor;   /* the other panel shows no cursor, as mc does */
        const std::vector<std::string> cells = {have ? " " + e.name : "", have ? fmt::format("{:>5}", e.blocks) : "",
                                                have ? e.date : "", have && e.protectedFlag ? "P" : ""};
        for (size_t c = 0; c < cols.size(); ++c) { cols[c].cells.push_back(cells[c]); cols[c].looks.push_back(look); }
    }
    Element rule = separator();
    if (p.markedCount())
        rule = dbox({separator(), text(fmt::format(" {} blocks in {} file{} ", p.markedBlocks(), p.markedCount(), p.markedCount() == 1 ? "" : "s")) | hcenter});
    std::string current;
    if (const auto cur = p.current()) current = fmt::format(" {:<10} {:>5} blocks  {}", cur->name, cur->blocks, cur->date);
    std::string foot = p.hasLocation() ? p.location().summary() : (index == 0 ? "Alt-F1 picks a disk" : "Alt-F2 picks a disk");
    if (p.hasLocation() && !p.location().volumeId().empty()) foot += " - " + p.location().volumeId();
    return frame(isActive, p.title(), renderColumns(cols), rule, text(current), foot);
}

Element Tui::renderHost(int index)
{
    const HostBrowse &b = *browse_[static_cast<size_t>(index)];
    const bool isActive = index == active_;
    const int rows = panelRows();
    std::vector<Column> cols = {{" Name", 0, {}, {}}, {"  Size", 9, {}, {}}};
    for (int i = b.top; i < b.top + rows; ++i) {
        const bool have = i < static_cast<int>(b.items.size());
        const HostEntry h = have ? b.items[static_cast<size_t>(i)] : HostEntry{};
        Decorator look = have && h.directory ? bold : nothing;
        if (have && i == b.cursor && isActive) look = look | kCursor;
        cols[0].cells.push_back(have ? " " + h.name : "");
        cols[1].cells.push_back(!have ? "" : h.directory ? "    <DIR>" : fmt::format("{:>5} blk", h.bytes / 512));
        for (auto &c : cols) c.looks.push_back(look);
    }
    std::string current;
    if (!b.items.empty()) current = " " + b.items[static_cast<size_t>(b.cursor)].name;
    return frame(isActive, "host: " + utf8(b.dir), renderColumns(cols), separator(), text(current), "Enter mounts / enters, Esc back");
}

Element Tui::renderKeyBar(const std::vector<std::pair<const char *, const char *>> &keys) const
{
    Elements items;
    for (const auto &[num, name] : keys)
        items.push_back(hbox({text(num) | kKeyNum, text(name) | kBar | flex}) | flex);
    return hbox(items);
}

bool Tui::onEvent(const Event &e)
{
    if (dialog_) return onDialogKey(e);
    if (menu_ >= 0) return onMenuKey(e);
    if (view_) return onViewerKey(e);
    if (const auto fk = parseFunctionKey(e.input()); fk && fk->alt && (fk->number == 1 || fk->number == 2)) {
        openDevices(fk->number - 1);
        return true;
    }
    if (browse_[static_cast<size_t>(active_)]) return onHostKey(e);
    return onBrowseKey(e);
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
    if (e == Event::F1)  { help(); return true; }
    if (e == Event::F2)  { userMenu(); return true; }
    if (e == Event::F3)  { doView(); return true; }
    if (e == Event::F4)  { openDevices(active_); return true; }
    if (e == Event::F5)  { doCopy(false); return true; }
    if (e == Event::F6)  { doCopy(true); return true; }
    if (e == Event::F7)  { doSqueeze(); return true; }
    if (e == Event::F8)  { doDelete(); return true; }
    if (e == Event::F9)  { openMenu(0); return true; }
    if (e == Event::F10) {
        if (hooks_.quitQuestion.empty()) { quit_ = true; return true; }
        ask("Quit", {hooks_.quitQuestion}, {"Yes", "No"}, [this](int c) { if (c == 0) quit_ = true; });
        return true;
    }
    if (e == Event::Character("+")) { doSelect(true); return true; }
    if (e == Event::Character("-")) { doSelect(false); return true; }
    if (e == Event::Character("*")) { p.invertMarks(); return true; }
    if (e == Event::Special(std::string("\x15"))) { swapPanels(); return true; }     /* Ctrl+U */
    if (e == Event::Special(std::string("\x12"))) { refreshPanels(); return true; }  /* Ctrl+R */
    if (e == Event::Special(std::string("\x14"))) { p.toggleMark(); return true; }   /* Ctrl+T */
    return false;
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

void Tui::startHostBrowse(int panelIndex, const std::filesystem::path &dir)
{
    HostBrowse b;
    std::error_code ec;
    b.dir = std::filesystem::canonical(dir, ec);
    if (ec) b.dir = dir;
    b.items = listImages(b.dir);
    browse_[static_cast<size_t>(panelIndex)] = std::move(b);
    active_ = panelIndex;
    status_ = "Pick the image to mount - Enter; Esc keeps the panel as it was.";
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
            ask("Mount", {"Which side of the drive?"}, {"Side 0", "Side 1", "Cancel"},
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
        ask("Mount", {why, "Mount it anyway, to initialise it?"}, {"Yes", "No"},
            [this, slot, path, side](int c) { if (c == 0) doMount(slot, path, side, true); });
        return;
    }
    message("Mount", {why});
}

/* A volume was written: re-read every panel on that image, and tell the
 * host, whose machine may keep a copy of it. */
void Tui::changedImage(const std::filesystem::path &image)
{
    for (Panel &q : panels_)
        if (q.hasLocation() && q.location().device().image == image) {
            if (!q.location().reload()) q.clear(); else q.reload();
        }
    if (hooks_.imageChanged) hooks_.imageChanged(image);
}

void Tui::changed(Panel &p)
{
    if (p.hasLocation()) changedImage(p.location().device().image);
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

} /* namespace ms0515::files::detail */

namespace ms0515::files {

struct Commander::Impl {
    detail::Tui tui;
};

Commander::Commander(Mounts mounts, app::Config &config, CommanderHooks hooks)
    : impl_(std::make_unique<Impl>(Impl{detail::Tui(std::move(mounts), config, std::move(hooks))}))
{
}

Commander::~Commander() = default;

ftxui::Element Commander::render(int width, int height, ftxui::Element guest)
{
    impl_->tui.setSize(width, height);
    return impl_->tui.render(std::move(guest));
}

bool Commander::onEvent(const ftxui::Event &event)
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
    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto component = ftxui::Renderer([&] { return commander.render(screen.dimx(), screen.dimy()); })
                   | ftxui::CatchEvent([&](const ftxui::Event &e) {
                         const bool used = commander.onEvent(e);
                         if (commander.takeQuitRequest()) screen.ExitLoopClosure()();
                         return used;
                     });
    screen.TrackMouse(false);   /* keyboard only - and a terminal left in mouse-tracking mode after a crash is a mess */
    screen.Loop(component);
    return 0;
}

} /* namespace ms0515::files */
