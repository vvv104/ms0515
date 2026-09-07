/*
 * CommanderActions.cpp — what the keys and menus do: the menus
 * themselves, copy and move with mc's "File exists" question per file,
 * delete, rename, squeeze, init, protect, dates, the host ends,
 * selection by pattern, the sort order, swapping the panels, help.
 */
#include "TuiImpl.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <utility>

namespace ms0515::files::detail {

using namespace ftxui;

void Tui::buildMenus()
{
    const auto side = [this](int idx, const char *diskKey) {
        Menu m;
        m.name = idx == 0 ? "Left" : "Right";
        m.items = {{"Disk...", diskKey, [this, idx] { openDevices(idx); }},
                   {"Sort order...", "", [this, idx] { active_ = idx; doSort(); }},
                   {"Reread", "C-r", [this] { refreshPanels(); }}};
        return m;
    };
    Menu file;
    file.name = "File";
    file.items = {{"View", "F3", [this] { doView(); }},
                  {"Copy", "F5", [this] { doCopy(false); }},
                  {"Rename/Move", "F6", [this] { doCopy(true); }},
                  {"Squeeze", "F7", [this] { doSqueeze(); }},
                  {"Delete", "F8", [this] { doDelete(); }},
                  {},
                  {"Protect", "", [this] { doProtect(true); }},
                  {"Unprotect", "", [this] { doProtect(false); }},
                  {"Set date...", "", [this] { doSetDate(); }},
                  {"Undelete...", "", [this] { doUndelete(); }},
                  {},
                  {"From host...", "", [this] { doImport(); }},
                  {"To host...", "", [this] { doExport(); }},
                  {},
                  {"Init volume...", "", [this] { doInit(); }}};
    Menu command;
    command.name = "Command";
    command.items = {{"User menu", "F2", [this] { userMenu(); }},
                     {"Swap panels", "C-u", [this] { swapPanels(); }},
                     {"Switch panels on/off", "C-o", nullptr},
                     {},
                     {"Select group", "+", [this] { doSelect(true); }},
                     {"Unselect group", "-", [this] { doSelect(false); }},
                     {"Invert selection", "*", [this] { panel().invertMarks(); }}};
    Menu options;
    options.name = "Options";
    options.items = {{"Viewer encoding...", "", [this] {
                          Dialog d;
                          d.title = "Viewer encoding";
                          d.radio = {"ASCII", "KOI-8R", "KOI-7", "KOI-7 ^N/^O", "CP866"};
                          d.radioAt = static_cast<int>(viewEncoding_);
                          d.buttons = {"OK", "Cancel"};
                          d.focus = Dialog::Focus::radio;
                          d.onDone = [this](const Dialog &dd, int b) { if (b == 0) viewEncoding_ = static_cast<Encoding>(dd.radioAt); };
                          dialog_ = std::move(d);
                      }},
                     {"Date for new files...", "", [this] {
                          inputDialog("Date for new files", {"The date a host file gets on a volume (YYYY-MM-DD, empty for none):"},
                                      "", newFileDate_, false, [this](const std::string &s) {
                              if (!s.empty() && Location::dateWord(s) == 0) { message("Date", {s + ": not a date"}); return; }
                              newFileDate_ = s;
                          });
                      }},
                     {"Show unused areas", "", [this] { setShowUnused(!showUnused_); }}};
    menus_ = {side(0, "M-F1"), std::move(file), std::move(command), std::move(options), side(1, "M-F2")};
}

void Tui::userMenu()
{
    pick("User menu", {"From host...", "To host...", "Protect", "Unprotect", "Set date...", "Squeeze", "Init volume...", "Undelete..."},
         [this](int i) {
        switch (i) {
        case 0: doImport(); break;
        case 1: doExport(); break;
        case 2: doProtect(true); break;
        case 3: doProtect(false); break;
        case 4: doSetDate(); break;
        case 5: doSqueeze(); break;
        case 6: doInit(); break;
        case 7: doUndelete(); break;
        default: break;
        }
    });
}

void Tui::help()
{
    message("Help", {
        "The machine's disks in two panels, the Midnight Commander way.",
        "",
        "Tab       the other panel        Insert    mark, and down",
        "Enter/F3  view the file          + - *     select / unselect / invert",
        "F1        this help              F2        the user menu",
        "F4        the panel's disk       Alt-F1/F2 the left / right panel's disk",
        "F5        copy to a device       F6        rename / move",
        "F7        squeeze the volume     F8        delete",
        "F9        the menu               F10       quit",
        "Ctrl-U    swap the panels        Ctrl-R    re-read",
        "",
        "Typed text goes to the machine's prompt under the panels; Enter",
        "after typing goes there too.  Ctrl-O shows the machine's screen.",
    });
}

void Tui::doView()
{
    if (!panel().hasLocation()) return;
    const auto cur = panel().current();
    if (!cur) return;
    const auto bytes = panel().location().readArea(*cur);   /* a file's blocks, or an unused area's */
    if (!bytes) { status_ = cur->name + ": cannot read"; return; }
    ViewState v;
    v.name = panel().location().device().name + (cur->empty ? fmt::format("<unused at {}>", cur->offset) : cur->name);
    v.bytes = *bytes;
    /* a text opens as text, in the encoding its bytes point to; a binary
     * file as a dump, in the encoding set under Options */
    const bool isText = isTextLike(v.bytes);
    v.opts.view = isText ? View::text : View::hex;
    v.opts.encoding = isText ? detectEncoding(v.bytes) : viewEncoding_;
    v.lines = renderLines(v.bytes, v.opts);
    view_ = std::move(v);
}

/* The command that runs a program on the machine: RUN for a .SAV, @ for
 * a .COM (an indirect command file); "" for anything else. */
std::string Tui::runCommand(const std::string &device, const std::string &name)
{
    const auto dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    const std::string stem = name.substr(0, dot), ext = name.substr(dot + 1);
    if (ext == "SAV") return "RUN " + device + stem;
    if (ext == "COM") return "@" + device + stem;
    return "";
}

/* Enter with nothing typed: a program runs, through the host's hands at
 * the machine's prompt; anything else opens in the viewer. */
void Tui::doEnter()
{
    if (!panel().hasLocation()) return;
    const auto cur = panel().current();
    if (!cur || cur->empty || !hooks_.runInGuest) { doView(); return; }
    const std::string line = runCommand(panel().location().device().name, cur->name);
    if (line.empty()) { doView(); return; }
    hooks_.runInGuest(line);
    status_ = line;
}

/* mc's dialog: "Copy file "X" to:" with the other panel's device in the
 * line.  A move with a bare name in the line is a rename. */
void Tui::doCopy(bool move)
{
    if (!panel().hasLocation()) { status_ = "no disk in this panel"; return; }
    const auto sel = panel().selection();
    if (sel.empty()) return;
    const std::string verb = move ? "Move" : "Copy";
    const std::string what = sel.size() == 1 ? fmt::format("{} file \"{}\" to:", verb, sel[0].name)
                                             : fmt::format("{} {} files to:", verb, sel.size());
    const std::string target = other().hasLocation() ? other().location().device().name : "";
    inputDialog(verb, {what}, "", target, false, [this, sel, move, verb](const std::string &to) {
        const std::string dest = upperCase(to);
        if (dest.empty()) return;
        if (move && sel.size() == 1 && dest.find(':') == std::string::npos) { doRename(sel[0], dest); return; }
        const auto dev = mounts_.device(dest);
        if (!dev) { message(verb, {dest + ": not a mounted device (DZ0: DZ2: DZ1: DZ3: HD0:)"}); return; }
        startTransfer(move, sel, *dev);
    });
}

void Tui::startTransfer(bool move, const std::vector<Entry> &sel, const Device &target)
{
    Transfer t;
    t.move = move;
    t.queue = sel;
    t.from = &panel().location();
    if (t.from->device().name == target.name) { message(move ? "Move" : "Copy", {"The same volume on both ends."}); return; }
    if (other().hasLocation() && other().location().device().name == target.name) t.to = &other().location();
    else {
        t.ownTo = Location::open(target);
        if (!t.ownTo) { message(move ? "Move" : "Copy", {target.label() + ": cannot open"}); return; }
    }
    transfer_ = std::move(t);
    if (transfer_->ownTo) transfer_->to = &*transfer_->ownTo;
    if (move) protectedGuard(sel, "move", [this](Policy p) { transfer_->policy = p; continueTransfer(); });
    else continueTransfer();
}

/* The files one by one; a name that exists on the target stops for the
 * question unless the user has answered for all. */
void Tui::continueTransfer()
{
    Transfer &t = *transfer_;
    while (t.at < t.queue.size()) {
        const Entry e = t.queue[t.at];
        const auto existing = t.to->find(e.name);
        if (existing && !t.overwriteAll && !t.overwriteNext) {
            if (t.skipAll) { ++t.at; continue; }
            askExists(e, *existing);
            return;
        }
        Policy p = t.policy;
        if (existing) { p.overwrite = true; p.touchProtected = true; }
        t.overwriteNext = false;
        const OpResult r = t.move ? moveFiles(*t.from, {e}, *t.to, p) : copyFiles(*t.from, {e}, *t.to, p);
        t.result.done += r.done;
        t.result.errors.insert(t.result.errors.end(), r.errors.begin(), r.errors.end());
        ++t.at;
    }
    finishTransfer();
}

void Tui::askExists(const Entry &entry, const Entry &existing)
{
    const Transfer &t = *transfer_;
    ask("File exists",
        {fmt::format("New     : {}{}   {} blocks  {}", t.from->device().name, entry.name, entry.blocks, entry.date),
         fmt::format("Existing: {}{}   {} blocks  {}", t.to->device().name, existing.name, existing.blocks, existing.date),
         "", "Overwrite this file?"},
        {"Yes", "No", "All", "None", "Abort"}, [this](int c) {
        Transfer &tt = *transfer_;
        switch (c) {
        case 0: tt.overwriteNext = true; break;
        case 1: ++tt.at; break;
        case 2: tt.overwriteAll = true; break;
        case 3: tt.skipAll = true; break;
        default: tt.at = tt.queue.size(); break;
        }
        continueTransfer();
    });
}

void Tui::finishTransfer()
{
    const std::filesystem::path fromImage = transfer_->from->device().image;
    const std::filesystem::path toImage = transfer_->to->device().image;
    const bool move = transfer_->move;
    const OpResult result = transfer_->result;
    transfer_.reset();
    report(result, move ? "moved" : "copied");
    panel().clearMarks();
    changedImage(fromImage);
    if (toImage != fromImage) changedImage(toImage);
}

void Tui::doRename(const Entry &entry, const std::string &newName)
{
    if (newName == entry.name) return;
    protectedGuard({entry}, "rename", [this, entry, newName](Policy policy) {
        report(renameFile(panel().location(), entry, newName, policy), "renamed");
        changed(panel());
    });
}

void Tui::doSqueeze()
{
    if (!panel().hasLocation()) return;
    ask("Squeeze", {"Squeeze " + panel().location().device().name + " (gather the free space)?"}, {"Yes", "No"}, [this](int c) {
        if (c != 0) return;
        const auto why = panel().location().squeeze();
        status_ = why.empty() ? "Squeezed." : why;
        changed(panel());
    });
}

void Tui::doDelete()
{
    if (!panel().hasLocation()) return;
    const auto sel = panel().selection();
    if (sel.empty()) return;
    const std::string what = sel.size() == 1 ? fmt::format("Delete file \"{}\"?", sel[0].name)
                                             : fmt::format("Delete {} files?", sel.size());
    ask("Delete", {what}, {"Yes", "No"}, [this, sel](int c) {
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
    ask("Init", {"Initialise " + dev + " - every file on it is lost!"}, {"Yes", "No"}, [this](int c) {
        if (c != 0) return;
        inputDialog("Init", {"The volume ID (up to 12 characters):"}, "", "RT11A", false, [this](const std::string &id) {
            disk::InitOptions opts;
            if (!id.empty()) opts.volumeId = id.substr(0, 12);
            const auto why = panel().location().init(opts);
            status_ = why.empty() ? "Initialised." : why;
            changed(panel());
        });
    });
}

void Tui::doProtect(bool on)
{
    if (!panel().hasLocation()) return;
    const auto sel = panel().selection();
    if (sel.empty()) return;
    report(protectFiles(panel().location(), sel, on), on ? "protected" : "unprotected");
    changed(panel());
}

/* An unused area back as a file: under the name it kept, or one typed. */
void Tui::doUndelete()
{
    if (!panel().hasLocation()) return;
    const auto cur = panel().current();
    if (!cur || !cur->empty) { status_ = "Undelete works on an unused area (the dim entries)."; return; }
    const Entry area = *cur;
    inputDialog("Undelete", {fmt::format("Bring back {} blocks at {} as:", area.blocks, area.offset)}, "", area.name, false,
                [this, area](const std::string &name) {
        const auto why = panel().location().undelete(area, upperCase(name));
        status_ = why.empty() ? "Undeleted." : why;
        changed(panel());
    });
}

void Tui::setShowUnused(bool on)
{
    showUnused_ = on;
    for (Panel &p : panels_) p.setShowUnused(on);
}

void Tui::doSetDate()
{
    if (!panel().hasLocation()) return;
    const auto sel = panel().selection();
    if (sel.empty()) return;
    const std::string initial = sel[0].date.empty() ? today() : sel[0].date;
    inputDialog("Set date", {fmt::format("The date for {} file{} (YYYY-MM-DD, empty for none):", sel.size(), sel.size() == 1 ? "" : "s")},
                "", initial, false, [this, sel](const std::string &date) {
        OpResult r;
        for (const auto &e : sel) {
            const auto why = panel().location().setDate(e.name, date);
            if (why.empty()) ++r.done; else r.errors.push_back(why);
        }
        report(r, "dated");
        changed(panel());
    });
}

void Tui::doImport()
{
    if (!panel().hasLocation()) { status_ = "no disk in this panel"; return; }
    inputDialog("From host", {"A host file to put on " + panel().location().device().name + " (Tab completes):"}, "", "", true,
                [this](const std::string &path) {
        if (path.empty()) return;
        Policy policy;
        policy.date = newFileDate_;
        Location &to = panel().location();
        const std::string name = Location::toVolumeName(std::filesystem::path(path).filename().string());
        auto go = [this, path, policy, &to]() mutable {
            report(importFiles({path}, to, policy), "brought in");
            changed(panel());
        };
        if (to.find(name)) {
            ask("From host", {name + " is already on " + to.device().name + "."}, {"Overwrite", "Cancel"},
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
    inputDialog("To host", {fmt::format("A host directory for {} file{} (Tab completes):", sel.size(), sel.size() == 1 ? "" : "s")},
                "", utf8(lastDir_), true, [this, sel](const std::string &dir) {
        if (dir.empty()) return;
        Policy policy;
        policy.overwrite = true;
        report(exportFiles(panel().location(), sel, dir, policy), "put out");
    });
}

void Tui::doSelect(bool on)
{
    inputDialog(on ? "Select" : "Unselect", {}, "", "*", false, [this, on](const std::string &pattern) {
        if (!pattern.empty()) panel().markPattern(pattern, on);
    });
}

void Tui::doSort()
{
    Dialog d;
    d.title = "Sort order";
    d.radio = {"Offset", "Name", "Extension", "Size", "Date"};
    d.radioAt = static_cast<int>(panel().sortOrder());
    d.checks = {{"Reverse", panel().reversed()}};
    d.buttons = {"OK", "Cancel"};
    d.focus = Dialog::Focus::radio;
    d.onDone = [this](const Dialog &dd, int b) {
        if (b == 0) panel().setSort(static_cast<SortOrder>(dd.radioAt), dd.checks[0].second);
    };
    dialog_ = std::move(d);
}

void Tui::swapPanels()
{
    std::swap(panels_[0], panels_[1]);
    std::swap(browse_[0], browse_[1]);
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
    status_ = fmt::format("{} {}.", r.done, verb);
    if (!r.errors.empty()) {
        status_ += fmt::format(" {} failed.", r.errors.size());
        message("Not done", r.errors);
    }
}

} /* namespace ms0515::files::detail */
