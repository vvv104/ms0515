/*
 * WizardTui.cpp - the native disk wizard's screen.
 */

#include "WizardTui.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

using namespace ftxui;
using namespace ms0515::disk;

namespace ms0515::tools {

namespace {

/* The commander's colours, so the two tools look like one family. */
const Decorator kPanel   = bgcolor(Color::Blue) | color(Color::White);
const Decorator kCursor  = bgcolor(Color::Cyan) | color(Color::Black);
const Decorator kGroup   = color(Color::YellowLight);
const Decorator kAdded   = color(Color::GreenLight);
const Decorator kGrey    = color(Color::GrayDark);
const Decorator kBar     = bgcolor(Color::Cyan) | color(Color::Black);
const Decorator kKeyNum  = bgcolor(Color::Black) | color(Color::White);
const Decorator kBad     = color(Color::RedLight);
const Decorator kEdit    = bgcolor(Color::Black) | color(Color::White);

const std::vector<std::pair<const char *, const char *>> kKeys = {
    {" 2", "Save"}, {" 3", "Open"}, {" 5", "Build"}, {"10", "Quit"},
};

int capacityOf(Media m) { return m == Media::dv ? 1586 : 786; }

std::optional<std::vector<uint8_t>> readHost(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

const int kNoteWidth = 22;
const int kSideWidth = 5 + 2 + kNoteWidth;   /* a group's summary sits where its rows' blocks and notes do */
const int kLineWidth = 30;                   /* a START.COM line's box */
const std::size_t kFieldTitleWidth = 15;     /* "DZ2: volume id", so the boxes line up */
const char *const kNotReady = "choose the diskette and the system first";

/* What a note has room for: a title up to its " - " or " (" - "Pascal",
 * "MACRO V05.04". */
std::string shortTitle(const std::string &title)
{
    auto cut = std::min(title.find(" - "), title.find(" ("));
    return cut == std::string::npos ? title : title.substr(0, cut);
}

std::string mark(const WizardRow &r)
{
    const bool set = r.mark != WizardRow::Mark::off;
    if (r.radio) return set ? "(\xE2\x80\xA2)" : "( )";
    switch (r.mark) {
    case WizardRow::Mark::on:     return "[x]";
    case WizardRow::Mark::added:  return "[+]";
    case WizardRow::Mark::system: return "[#]";
    case WizardRow::Mark::off:    break;
    }
    return "[ ]";
}

}  /* namespace */

WizardTui::WizardTui(const Manifest &manifest, const Repository &repo, std::filesystem::path workDir)
    : manifest_(manifest), repo_(repo), workDir_(std::move(workDir)),
      wizard_(manifest, [this](const ManifestBundle &b) {
                  if (const auto it = blocks_.find(b.key); it != blocks_.end()) return it->second;
                  int n = 0;
                  try {
                      for (const auto &path : bundlePaths(b, repo_))
                          if (const auto bytes = repo_.read(path)) n += static_cast<int>((bytes->size() + 511) / 512);
                  } catch (const std::exception &) {
                  }
                  blocks_[b.key] = n;
                  return n;
              })
{
    changed();
}

void WizardTui::changed()
{
    plan_.reset();
    planProblem_.clear();
    startupLines_.clear();
    if (wizard_.ready()) try {
        const ComposeRecipe recipe = recipeFor(manifest_, wizard_.selection(), repo_);
        plan_ = planDisk(recipe);
        if (!plan_->ok) planProblem_ = plan_->problem;
        startupLines_ = recipe.startup.value_or(std::vector<std::string>{});
    } catch (const std::exception &e) {
        planProblem_ = e.what();
    }
    const auto rows = visibleRows();
    cursor_ = std::clamp(cursor_, 0, std::max(0, static_cast<int>(rows.size()) - 1));
}

std::vector<WizardRow> WizardTui::visibleRows() const
{
    return wizard_.rows();
}

int WizardTui::indexOf(const std::string &key, WizardRow::Kind kind) const
{
    const auto rows = visibleRows();
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].key == key && rows[i].kind == kind) return static_cast<int>(i);
    return -1;
}

void WizardTui::moveCursor(int delta)
{
    const int n = static_cast<int>(visibleRows().size());
    cursor_ = std::clamp(cursor_ + delta, 0, std::max(0, n - 1));
}

/* Space on a row: a choice made, the cursor left where it is. */
void WizardTui::activate(const WizardRow &r)
{
    switch (r.kind) {
    case WizardRow::Kind::group:
        if (r.available) wizard_.toggleFold(r.key); else status_ = r.why;
        return;
    case WizardRow::Kind::radio:
    case WizardRow::Kind::line:
        return;
    case WizardRow::Kind::field:
        startEdit(r, r.value);
        return;
    case WizardRow::Kind::media:
        status_ = wizard_.setMedia(*parseMedia(r.key));
        if (status_.empty() && !wizard_.notices().empty()) status_ = wizard_.notices().front();
        changed();
        return;
    case WizardRow::Kind::system:
        status_ = wizard_.setSystem(r.key);
        if (status_.empty() && !wizard_.notices().empty()) status_ = wizard_.notices().front();
        changed();
        return;
    case WizardRow::Kind::bundle:
        status_ = wizard_.toggle(r.key);
        changed();
        return;
    }
}

std::string WizardTui::defaultName(const char *ext) const
{
    return wizard_.ready() ? wizard_.system() + "-" + mediaWord(*wizard_.media()) + ext : std::string("disk") + ext;
}

void WizardTui::startAsk(Ask ask, std::string value)
{
    ask_ = ask;
    input_ = std::move(value);
}

void WizardTui::save(const std::filesystem::path &path)
{
    if (!wizard_.ready()) { status_ = kNotReady; return; }
    std::ofstream f(workDir_ / path, std::ios::binary);
    f << selectionToml(wizard_.saved());
    status_ = f ? "saved " + path.generic_string() : "cannot write " + path.generic_string();
}

void WizardTui::openFile(const std::filesystem::path &path)
{
    const auto bytes = readHost(workDir_ / path);
    if (!bytes) { status_ = "cannot read " + path.generic_string(); return; }
    try {
        open(parseSelection(std::string(bytes->begin(), bytes->end())));
        status_ = wizard_.notices().empty() ? "opened " + path.generic_string() : wizard_.notices().front();
    } catch (const std::exception &e) {
        status_ = e.what();
    }
}

void WizardTui::open(const SavedSelection &saved)
{
    wizard_.load(saved);
    changed();
}

void WizardTui::build(const std::filesystem::path &path)
{
    if (!wizard_.ready()) { status_ = kNotReady; return; }
    try {
        const auto image = composeDisk(recipeFor(manifest_, wizard_.selection(), repo_));
        std::ofstream f(workDir_ / path, std::ios::binary);
        f.write(reinterpret_cast<const char *>(image.data()), static_cast<std::streamsize>(image.size()));
        status_ = f ? "built " + path.generic_string() : "cannot write " + path.generic_string();
    } catch (const std::exception &e) {
        status_ = e.what();
    }
}

void WizardTui::findNext(const std::string &text)
{
    if (text.empty()) return;
    auto lower = [](std::string s) { for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const auto shownRows = visibleRows();
    const auto rows = wizard_.rows(true);                     /* the closed groups' rows too */
    const int n = static_cast<int>(rows.size());
    int from = 0;
    if (!shownRows.empty()) {
        const auto &here = shownRows[static_cast<std::size_t>(cursor_)];
        for (int i = 0; i < n; ++i)
            if (rows[static_cast<std::size_t>(i)].key == here.key && rows[static_cast<std::size_t>(i)].kind == here.kind) from = i;
    }
    const auto needle = lower(text);
    for (int step = 1; step <= n; ++step) {
        const auto &r = rows[static_cast<std::size_t>((from + step) % n)];
        if (lower(r.title).find(needle) == std::string::npos) continue;
        if (!r.parent.empty()) wizard_.reveal(r.parent);
        cursor_ = std::max(0, indexOf(r.key, r.kind));
        return;
    }
    status_ = "no \"" + text + "\"";
}

void WizardTui::finishAsk()
{
    const Ask ask = ask_;
    ask_ = Ask::none;
    switch (ask) {
    case Ask::save:    if (!input_.empty()) save(input_); break;
    case Ask::open:    if (!input_.empty()) openFile(input_); break;
    case Ask::build:   if (!input_.empty()) build(input_); break;
    case Ask::find:    findNext(input_); break;
    case Ask::none:    break;
    }
}

bool WizardTui::onAskEvent(const Event &e)
{
    if (e == Event::Escape) { ask_ = Ask::none; return true; }
    if (e == Event::Return) { finishAsk(); return true; }
    if (e == Event::Backspace) { if (!input_.empty()) input_.pop_back(); return true; }
    if (e.is_character()) { input_ += e.character(); return true; }
    return true;
}

std::string WizardTui::cursorKey() const
{
    const auto rows = visibleRows();
    return rows.empty() ? std::string() : rows[static_cast<std::size_t>(cursor_)].key;
}

/* After Enter's choice: on to the next thing to choose - past the rest of a
 * radio group, over open headings, START.COM's own lines and what cannot be
 * taken.  At the end of the list the cursor stays. */
void WizardTui::advance(const std::string &key, WizardRow::Kind kind)
{
    const auto rows = visibleRows();
    int at = indexOf(key, kind);
    if (at < 0) at = cursor_;
    auto next = static_cast<std::size_t>(at) + 1;
    const auto &r = rows[static_cast<std::size_t>(at)];
    if (r.radio)
        while (next < rows.size() && rows[next].radio && rows[next].kind == r.kind) ++next;
    auto passed = [](const WizardRow &x) {
        return (x.kind == WizardRow::Kind::group && x.open) || x.kind == WizardRow::Kind::radio ||
               x.kind == WizardRow::Kind::line || (x.kind != WizardRow::Kind::group && !x.available);
    };
    while (next < rows.size() && passed(rows[next])) ++next;
    if (next < rows.size()) cursor_ = static_cast<int>(next);
}

void WizardTui::startEdit(const WizardRow &row, std::string text)
{
    editing_ = true;
    editKey_ = row.key;
    edit_ = std::move(text);
}

/* Typing into a field: Enter keeps the text and goes on, an arrow keeps it
 * and moves, Del empties the box, Esc drops the edit. */
bool WizardTui::onEditEvent(const Event &e)
{
    if (e == Event::Escape) { editing_ = false; return true; }
    if (e == Event::Return || e == Event::ArrowUp || e == Event::ArrowDown) {
        editing_ = false;
        const bool blank = edit_.find_first_not_of(' ') == std::string::npos;
        status_ = wizard_.setField(editKey_, edit_);
        changed();
        if (e == Event::Return && status_.empty()) {
            const std::string prefix = kStartupField;
            const int at = indexOf(editKey_, WizardRow::Kind::field);
            if (blank && editKey_.rfind(prefix, 0) == 0 && at >= 0) cursor_ = at;   /* the next line came up */
            else advance(editKey_, WizardRow::Kind::field);
        }
        if (e == Event::ArrowUp) moveCursor(-1);
        if (e == Event::ArrowDown) moveCursor(1);
        return true;
    }
    if (e == Event::Backspace) { if (!edit_.empty()) edit_.pop_back(); return true; }
    if (e == Event::Delete) { edit_.clear(); return true; }
    if (e.is_character()) edit_ += e.character();
    return true;
}

bool WizardTui::onListEvent(const Event &e)
{
    const auto rows = visibleRows();
    if (e == Event::ArrowUp)   { moveCursor(-1); return true; }
    if (e == Event::ArrowDown) { moveCursor(1); return true; }
    if (e == Event::PageUp)    { moveCursor(-10); return true; }
    if (e == Event::PageDown)  { moveCursor(10); return true; }
    if (e == Event::Home)      { cursor_ = 0; return true; }
    if (e == Event::End)       { cursor_ = std::max(0, static_cast<int>(rows.size()) - 1); return true; }
    if (rows.empty()) return false;
    const auto &r = rows[static_cast<std::size_t>(cursor_)];
    if (e == Event::Character(" ")) { activate(r); return true; }
    if (e == Event::Return) {                                  /* choose, and go on */
        if (r.kind == WizardRow::Kind::group) {
            if (!r.available) status_ = r.why;
            else if (!r.open) wizard_.toggleFold(r.key);
        } else if (r.kind != WizardRow::Kind::field) {
            activate(r);
        }
        advance(r.key, r.kind);
        return true;
    }
    if (r.kind == WizardRow::Kind::field && e.is_character()) { startEdit(r, e.character()); return true; }   /* afresh */
    if (r.kind == WizardRow::Kind::field && e == Event::Delete) {   /* the box emptied: a START.COM line goes */
        status_ = wizard_.setField(r.key, "");
        changed();
        return true;
    }
    return false;
}

bool WizardTui::onEvent(const Event &e)
{
    if (ask_ != Ask::none) return onAskEvent(e);
    if (editing_) return onEditEvent(e);
    if (e == Event::F10) { quit_ = true; return true; }
    if (e == Event::F2) {
        if (wizard_.ready()) startAsk(Ask::save, defaultName(".toml")); else status_ = kNotReady;
        return true;
    }
    if (e == Event::F3) { startAsk(Ask::open, wizard_.ready() ? defaultName(".toml") : std::string()); return true; }
    if (e == Event::F5) {
        if (wizard_.ready()) startAsk(Ask::build, defaultName(".dsk")); else status_ = kNotReady;
        return true;
    }
    if (e == Event::Character("/")) { startAsk(Ask::find, ""); return true; }
    return onListEvent(e);
}

/* ---- drawing -------------------------------------------------------------- */

Element WizardTui::renderTop() const
{
    const std::string version = manifest_.version.empty() ? std::string() : "collection " + manifest_.version + " ";
    return hbox({text(" MS-0515 disk composer"), filler(), text(version)}) | kBar;
}

Element WizardTui::renderList(int rows)
{
    const auto list = visibleRows();
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + rows) top_ = cursor_ - rows + 1;
    Elements lines;
    for (int i = top_; i < std::min(static_cast<int>(list.size()), top_ + rows); ++i) {
        const auto &r = list[static_cast<std::size_t>(i)];
        Element line;
        const std::string indent(static_cast<std::size_t>(r.depth * 2), ' ');
        if (r.kind == WizardRow::Kind::group) {
            const std::string arrow = r.open ? "\xE2\x96\xBE " : "\xE2\x96\xB8 ";
            line = hbox({text(indent + arrow + r.title) | flex,
                         text(r.available ? r.summary : r.why) | size(WIDTH, EQUAL, kSideWidth)});
            line = line | (r.available ? kGroup : kGrey);
        } else if (r.kind == WizardRow::Kind::radio) {
            line = text(indent + "one of: " + r.title) | kGroup;
        } else if (r.kind == WizardRow::Kind::field) {
            const bool typing = editing_ && editKey_ == r.key;
            const std::size_t width = r.parent == kLabelGroup ? 12 : kLineWidth;
            std::string shownText = typing ? edit_ + "_" : r.value;
            if (shownText.size() > width) shownText = shownText.substr(shownText.size() - width);
            shownText.resize(width, ' ');
            std::string title = r.title;
            if (!title.empty()) title.resize(std::max<std::size_t>(title.size(), kFieldTitleWidth) + 1, ' ');
            /* The row keeps its cursor bar while the box is typed into: the
             * bar round the box, the box in its own colours. */
            const Decorator around = i == cursor_ ? kCursor : Decorator(nothing);
            line = hbox({text(indent + title + "[") | around, text(shownText) | (typing ? kEdit : around),
                         text("]") | around, filler() | around, text(r.summary) | size(WIDTH, EQUAL, kSideWidth) | around});
        } else if (r.kind == WizardRow::Kind::line) {
            const auto *sys = manifest_.system(wizard_.system());
            const std::string from = sys && r.requiredBy == sys->title ? std::string("the system's") : "from " + shortTitle(r.requiredBy);
            line = hbox({text(indent + r.title) | flex, text(from) | size(WIDTH, EQUAL, kSideWidth)});
        } else {
            std::string note = r.native ? "native"
                             : r.mark == WizardRow::Mark::system ? "system"
                             : r.mark == WizardRow::Mark::added ? "for " + shortTitle(r.requiredBy)
                             : r.available ? std::string() : r.why;
            const bool bundle = r.kind == WizardRow::Kind::bundle;
            Element row = hbox({text(indent + mark(r) + " " + r.title) | flex,
                                hbox({filler(), text(bundle ? std::to_string(r.blocks) : std::string())}) | size(WIDTH, EQUAL, 5),
                                text("  "), text(note) | size(WIDTH, EQUAL, kNoteWidth)});
            if (!r.available) row = row | kGrey;
            else if (r.mark == WizardRow::Mark::added) row = row | kAdded;
            line = row;
        }
        if (i == cursor_ && r.kind != WizardRow::Kind::field) line = line | kCursor;
        lines.push_back(line);
    }
    return vbox(lines);
}

Element WizardTui::renderDetails() const
{
    const auto list = visibleRows();
    if (list.empty()) return text("");
    const auto &r = list[static_cast<std::size_t>(cursor_)];
    const auto *b = manifest_.bundle(r.key);
    if (r.kind != WizardRow::Kind::bundle || !b) {
        const std::string title = r.kind == WizardRow::Kind::radio ? "one of: " + r.title
                                : r.kind == WizardRow::Kind::field && r.title.empty() ? std::string("a line of START.COM")
                                : r.title;
        Elements out = {paragraph(title), text("")};
        if (!r.summary.empty()) out.push_back(paragraph(r.summary));
        if (r.kind == WizardRow::Kind::line) out.push_back(paragraph("from " + r.requiredBy));
        if (r.kind == WizardRow::Kind::field) out.push_back(paragraph("Type to change it; Enter keeps, Esc drops."));
        if (!r.available) out.push_back(paragraph(r.why) | kBad);
        return vbox(out);
    }
    Elements out = {paragraph(b->title), text("")};
    auto joined = [](const std::vector<std::string> &v) { std::string s; for (const auto &x : v) s += (s.empty() ? "" : " ") + x; return s; };
    if (!b->provides.empty()) out.push_back(paragraph("provides " + joined(b->provides)));
    if (!b->dependsOn.empty()) out.push_back(paragraph("requires " + joined(b->dependsOn)));
    if (!b->startup.empty()) out.push_back(paragraph("startup  " + joined(b->startup)));
    try {
        std::string files;
        for (const auto &p : bundlePaths(*b, repo_)) files += (files.empty() ? "" : " ") + p.substr(p.rfind('/') + 1);
        out.push_back(paragraph("files    " + files));
    } catch (const std::exception &e) {
        out.push_back(paragraph(e.what()) | kBad);
    }
    out.push_back(text(std::to_string(r.blocks) + " blocks"));
    if (!r.available) out.push_back(paragraph(r.why) | kBad);
    if (r.mark == WizardRow::Mark::added) out.push_back(paragraph("required by " + r.requiredBy));
    return vbox(out);
}

Element WizardTui::renderPlan(int width) const
{
    if (!wizard_.ready()) return text("Choose the diskette, then the operating system.");
    Elements lines;
    if (plan_) {
        const int capacity = capacityOf(*wizard_.media());
        for (std::size_t v = 0; v < plan_->freeBlocks.size(); ++v) {
            const int used = capacity - plan_->freeBlocks[v];
            const std::string name = wizard_.media() == Media::dv ? "DV0:" : v == 0 ? "DZ0:" : "DZ2:";
            const float fill = std::clamp(static_cast<float>(used) / static_cast<float>(capacity), 0.0f, 1.0f);
            lines.push_back(hbox({text(name + " " + std::to_string(used) + " / " + std::to_string(capacity) + " "),
                                  gauge(fill) | size(WIDTH, EQUAL, std::max(10, width / 2)),
                                  text("  " + std::to_string(plan_->freeBlocks[v]) + " free")}));
        }
    }
    std::string startup;
    for (const auto &l : startupLines_) startup += (startup.empty() ? "" : " \xC2\xB7 ") + l;
    lines.push_back(text("START.COM  " + startup));
    if (!planProblem_.empty()) lines.push_back(paragraph(planProblem_) | kBad);
    return vbox(lines);
}

Element WizardTui::renderBottom() const
{
    Element line;
    static const std::map<Ask, const char *> prompts = {
        {Ask::save, "Save the choice to: "}, {Ask::open, "Open a choice: "}, {Ask::build, "Build the disk to: "},
        {Ask::find, "Find: "}};
    if (ask_ != Ask::none) line = hbox({text(prompts.at(ask_)), text(input_) | kCursor, filler()});
    else line = hbox({text(status_), filler()});
    const Element hint = hbox({text(editing_ ? "Enter: keep    Esc: drop    Del: empty    Up, Down: keep and move"
                                             : "Space: choose    Enter: choose and go on    a field: type to edit, Del empties    / find"), filler()});
    Elements keys;
    for (const auto &[num, name] : kKeys) keys.push_back(hbox({text(num) | kKeyNum, text(name) | kBar | flex}) | flex);
    return vbox({line, hint, hbox(keys)});
}

Element WizardTui::render(int width, int height)
{
    const int planLines = wizard_.ready() ? 2 + (plan_ ? static_cast<int>(plan_->freeBlocks.size()) : 0) + (planProblem_.empty() ? 0 : 1) : 1;
    const int listRows = std::max(3, height - 1 - 2 - planLines - 2 - 3);
    const int detailsWidth = std::clamp(width / 3, 24, 44);
    Element body = hbox({window(text(" Bundles "), renderList(listRows)) | flex,
                         window(text(" Details "), renderDetails()) | size(WIDTH, EQUAL, detailsWidth)});
    return vbox({renderTop(), body | flex, window(text(" Plan "), renderPlan(width)), renderBottom()}) | kPanel;
}

} /* namespace ms0515::tools */
