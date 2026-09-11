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

const std::vector<std::pair<const char *, const char *>> kKeys = {
    {" 2", "Save"}, {" 3", "Open"}, {" 5", "Build"}, {" 7", "Startup"}, {" 8", "Label"}, {"10", "Quit"},
};

int capacityOf(Media m) { return m == Media::dv ? 1586 : 786; }

std::optional<std::vector<uint8_t>> readHost(const std::filesystem::path &p)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::vector<std::string> splitLines(const std::string &s)
{
    std::vector<std::string> out;
    std::stringstream in(s);
    for (std::string item; std::getline(in, item, ';');) {
        const auto b = item.find_first_not_of(' '), e = item.find_last_not_of(' ');
        if (b != std::string::npos) out.push_back(item.substr(b, e - b + 1));
    }
    return out;
}

const int kNoteWidth = 22;

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
      wizard_(manifest, manifest.systems.empty() ? std::string() : manifest.systems.front().key,
              manifest.systems.empty() ? Media::ss : manifest.systems.front().media.front(),
              [this](const ManifestBundle &b) {
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
    try {
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
    std::vector<WizardRow> out;
    bool hidden = false;
    for (auto &r : wizard_.rows()) {
        if (r.kind == WizardRow::Kind::group) {
            hidden = folded_.count(r.key) != 0;
            out.push_back(std::move(r));
        } else if (!hidden) {
            out.push_back(std::move(r));
        }
    }
    return out;
}

void WizardTui::moveCursor(int delta)
{
    const int n = static_cast<int>(visibleRows().size());
    cursor_ = std::clamp(cursor_ + delta, 0, std::max(0, n - 1));
}

void WizardTui::toggleFold(const std::string &group)
{
    if (!folded_.erase(group)) folded_.insert(group);
}

void WizardTui::stepSystem(int delta)
{
    const auto &systems = manifest_.systems;
    auto it = std::find_if(systems.begin(), systems.end(), [&](const ManifestSystem &s) { return s.key == wizard_.system(); });
    int at = it == systems.end() ? 0 : static_cast<int>(it - systems.begin());
    at = (at + delta + static_cast<int>(systems.size())) % static_cast<int>(systems.size());
    wizard_.setSystem(systems[static_cast<std::size_t>(at)].key);
    status_ = wizard_.notices().empty() ? std::string() : wizard_.notices().front();
    changed();
}

void WizardTui::stepMedia(int delta)
{
    const auto offered = wizard_.mediaOffered();
    if (offered.empty()) return;
    auto it = std::find(offered.begin(), offered.end(), wizard_.media());
    int at = it == offered.end() ? 0 : static_cast<int>(it - offered.begin());
    at = (at + delta + static_cast<int>(offered.size())) % static_cast<int>(offered.size());
    wizard_.setMedia(offered[static_cast<std::size_t>(at)]);
    status_ = wizard_.notices().empty() ? std::string() : wizard_.notices().front();
    changed();
}

std::string WizardTui::defaultName(const char *ext) const
{
    return wizard_.system() + "-" + mediaWord(wizard_.media()) + ext;
}

void WizardTui::startAsk(Ask ask, std::string value)
{
    ask_ = ask;
    input_ = std::move(value);
}

void WizardTui::save(const std::filesystem::path &path)
{
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
    folded_.clear();
    const auto rows = visibleRows();
    const auto needle = lower(text);
    const int n = static_cast<int>(rows.size());
    for (int step = 1; step <= n; ++step) {
        const int i = (cursor_ + step) % n;
        if (lower(rows[static_cast<std::size_t>(i)].title).find(needle) != std::string::npos) { cursor_ = i; return; }
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
    case Ask::startup: wizard_.setStartup(splitLines(input_)); changed(); break;
    case Ask::label:   wizard_.setVolumeId(input_.empty() ? std::nullopt : std::optional<std::string>(input_.substr(0, 12))); changed(); break;
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
    if (e == Event::Return && r.kind == WizardRow::Kind::group) { toggleFold(r.key); return true; }
    if ((e == Event::Character(" ") || e == Event::Return) && r.kind == WizardRow::Kind::bundle) {
        status_ = wizard_.toggle(r.key);
        changed();
        return true;
    }
    return false;
}

bool WizardTui::onEvent(const Event &e)
{
    if (ask_ != Ask::none) return onAskEvent(e);
    if (e == Event::F10) { quit_ = true; return true; }
    if (e == Event::F2) { startAsk(Ask::save, defaultName(".toml")); return true; }
    if (e == Event::F3) { startAsk(Ask::open, defaultName(".toml")); return true; }
    if (e == Event::F5) { startAsk(Ask::build, defaultName(".dsk")); return true; }
    if (e == Event::F7) {
        const auto &s = wizard_.selection().startup;
        std::string v;
        if (s) for (const auto &l : *s) v += (v.empty() ? "" : "; ") + l;
        startAsk(Ask::startup, v);
        return true;
    }
    if (e == Event::F8) { startAsk(Ask::label, wizard_.selection().volumeId.value_or("")); return true; }
    if (e == Event::Character("/")) { startAsk(Ask::find, ""); return true; }
    if (e == Event::Tab) { focus_ = focus_ == Focus::system ? Focus::media : focus_ == Focus::media ? Focus::list : Focus::system; return true; }
    if (e == Event::TabReverse) { focus_ = focus_ == Focus::list ? Focus::media : focus_ == Focus::media ? Focus::system : Focus::list; return true; }
    if (focus_ != Focus::list) {
        if (e == Event::ArrowLeft || e == Event::ArrowRight) {
            const int d = e == Event::ArrowLeft ? -1 : 1;
            if (focus_ == Focus::system) stepSystem(d); else stepMedia(d);
            return true;
        }
        if (e == Event::ArrowDown || e == Event::Return) { focus_ = Focus::list; return true; }
        return false;
    }
    return onListEvent(e);
}

/* ---- drawing -------------------------------------------------------------- */

Element WizardTui::renderTop() const
{
    const auto *sys = manifest_.system(wizard_.system());
    Element system = text("< " + std::string(sys ? sys->title : "?") + " >");
    Element media = text(std::string("< ") + mediaWord(wizard_.media()) + " >");
    if (focus_ == Focus::system) system = system | kCursor;
    if (focus_ == Focus::media) media = media | kCursor;
    return hbox({text(" MS-0515 disk composer   System: "), system, text("   Media: "), media, filler()}) | kBar;
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
        if (r.kind == WizardRow::Kind::group) {
            line = text((folded_.count(r.key) ? "\xE2\x96\xB8 " : "\xE2\x96\xBE ") + r.title) | kGroup;
        } else if (r.kind == WizardRow::Kind::radio) {
            line = text("  one of: " + r.title) | kGroup;
        } else {
            std::string note = r.mark == WizardRow::Mark::system ? "system"
                             : r.mark == WizardRow::Mark::added ? "for " + shortTitle(r.requiredBy)
                             : r.available ? std::string() : r.why;
            Element row = hbox({text(std::string(static_cast<std::size_t>(r.depth * 2), ' ') + mark(r) + " " + r.title) | flex,
                                hbox({filler(), text(std::to_string(r.blocks))}) | size(WIDTH, EQUAL, 5),
                                text("  "), text(note) | size(WIDTH, EQUAL, kNoteWidth)});
            if (!r.available) row = row | kGrey;
            else if (r.mark == WizardRow::Mark::added) row = row | kAdded;
            line = row;
        }
        if (i == cursor_ && focus_ == Focus::list) line = line | kCursor;
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
    if (r.kind != WizardRow::Kind::bundle || !b) return paragraph(r.title);
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
    Elements lines;
    if (plan_) {
        const int capacity = capacityOf(wizard_.media());
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
        {Ask::startup, "START.COM lines after the system's (; between): "}, {Ask::label, "Volume id: "}, {Ask::find, "Find: "}};
    if (ask_ != Ask::none) line = hbox({text(prompts.at(ask_)), text(input_) | kCursor, filler()});
    else line = hbox({text(status_.empty() ? "Space toggle  Enter fold  Tab system/media  / find" : status_), filler()});
    Elements keys;
    for (const auto &[num, name] : kKeys) keys.push_back(hbox({text(num) | kKeyNum, text(name) | kBar | flex}) | flex);
    return vbox({line, hbox(keys)});
}

Element WizardTui::render(int width, int height)
{
    const int planLines = 2 + (plan_ ? static_cast<int>(plan_->freeBlocks.size()) : 0) + (planProblem_.empty() ? 0 : 1);
    const int listRows = std::max(3, height - 1 - 2 - planLines - 2 - 2);
    const int detailsWidth = std::clamp(width / 3, 24, 44);
    Element body = hbox({window(text(" Bundles "), renderList(listRows)) | flex,
                         window(text(" Details "), renderDetails()) | size(WIDTH, EQUAL, detailsWidth)});
    return vbox({renderTop(), body | flex, window(text(" Plan "), renderPlan(width)), renderBottom()}) | kPanel;
}

} /* namespace ms0515::tools */
