/*
 * FileDialog.cpp - the wizard's window for a file to open or to write.
 */

#include "FileDialog.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

using namespace ftxui;
namespace fs = std::filesystem;

namespace ms0515::tools {

namespace {

const Decorator kWindow  = bgcolor(Color::Cyan) | color(Color::Black);
const Decorator kPicked  = bgcolor(Color::Blue) | color(Color::White);
const Decorator kBox     = bgcolor(Color::Black) | color(Color::White);
const Decorator kBad     = color(Color::RedLight);

std::string lower(std::string s)
{
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  /* namespace */

Element framed(Element win)
{
    return vbox({text(""), hbox({text(" "), std::move(win), text(" ")}), text("")}) | kWindow | clear_under | center;
}

FileDialog::FileDialog(std::string title, Mode mode, fs::path dir, std::string extension, std::string name)
    : title_(std::move(title)), mode_(mode), dir_(std::move(dir)), extension_(std::move(extension)), name_(std::move(name))
{
    list();
}

void FileDialog::list()
{
    std::vector<std::string> dirs, files;
    std::error_code ec;
    for (fs::directory_iterator it(dir_, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string n = it->path().filename().string();
        if (n.empty() || n[0] == '.') continue;                /* .git and the like */
        std::error_code kind;
        if (it->is_directory(kind)) dirs.push_back(n + "/");
        else if (lower(it->path().extension().string()) == lower(extension_)) files.push_back(n);
    }
    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());
    entries_.clear();
    if (dir_.has_parent_path() && dir_.parent_path() != dir_) entries_.emplace_back("../");
    entries_.insert(entries_.end(), dirs.begin(), dirs.end());
    entries_.insert(entries_.end(), files.begin(), files.end());
    selected_ = -1;
}

void FileDialog::select(int index)
{
    if (entries_.empty()) return;
    selected_ = std::clamp(index, 0, static_cast<int>(entries_.size()) - 1);
    const std::string &e = entries_[static_cast<std::size_t>(selected_)];
    if (e.back() != '/') name_ = e;                        /* a file: its name to take */
}

FileDialog::Result FileDialog::accept()
{
    if (selected_ >= 0) {
        const std::string &e = entries_[static_cast<std::size_t>(selected_)];
        if (e.back() == '/') {                              /* into the directory */
            dir_ = e == "../" ? dir_.parent_path() : dir_ / e.substr(0, e.size() - 1);
            dir_ = dir_.lexically_normal();
            list();
            return Result::none;
        }
    }
    if (name_.empty()) { problem_ = "type a name, or pick a file"; return Result::none; }
    if (fs::path(name_).extension().empty()) name_ += extension_;
    std::error_code ec;
    const bool there = fs::is_regular_file(path(), ec);
    if (mode_ == Mode::open && !there) { problem_ = "no " + name_ + " here"; return Result::none; }
    if (mode_ == Mode::write && there && !replacing_) {
        replacing_ = true;
        problem_.clear();
        return Result::none;
    }
    return Result::accepted;
}

FileDialog::Result FileDialog::onEvent(const Event &e)
{
    if (replacing_) {
        if (e == Event::Return || e == Event::Character("y") || e == Event::Character("Y")) return Result::accepted;
        if (e == Event::Escape || e == Event::Character("n") || e == Event::Character("N")) replacing_ = false;
        return Result::none;
    }
    if (e == Event::Escape) return Result::cancelled;
    if (e == Event::Return) return accept();
    problem_.clear();
    if (e == Event::ArrowDown) { select(selected_ + 1); return Result::none; }
    if (e == Event::ArrowUp)   { if (selected_ > 0) select(selected_ - 1); else selected_ = -1; return Result::none; }
    if (e == Event::PageDown)  { select(selected_ + 10); return Result::none; }
    if (e == Event::PageUp)    { if (selected_ > 10) select(selected_ - 10); else selected_ = -1; return Result::none; }
    if (e == Event::Backspace) { if (!name_.empty()) name_.pop_back(); selected_ = -1; return Result::none; }
    if (e == Event::Delete)    { name_.clear(); selected_ = -1; return Result::none; }
    if (e.is_character())      { name_ += e.character(); selected_ = -1; return Result::none; }
    return Result::none;
}

Element FileDialog::render(int width, int height) const
{
    const int w = std::clamp(width - 8, 30, 76);
    const int listRows = std::clamp(height - 12, 3, 14);
    int top = std::max(0, selected_ - listRows + 1);
    Elements rows;
    for (int i = top; i < std::min(static_cast<int>(entries_.size()), top + listRows); ++i) {
        Element row = text(" " + entries_[static_cast<std::size_t>(i)]);
        if (i == selected_) row = row | kPicked;
        rows.push_back(row);
    }
    if (entries_.empty()) rows.push_back(text(" (nothing here)"));
    std::string shownName = selected_ < 0 && !replacing_ ? name_ + "_" : name_;
    Element nameLine = hbox({text("Name: "), text(shownName) | kBox | flex});
    Element foot;
    if (replacing_) foot = text(name_ + " is there - replace it?  Enter: yes   Esc: no") | kBad;
    else if (!problem_.empty()) foot = text(problem_) | kBad;
    else foot = text(mode_ == Mode::open ? "Enter: open   Esc: cancel   Up, Down: pick" : "Enter: write   Esc: cancel   Up, Down: pick");
    Element win = window(text(" " + title_ + " "),
                         vbox({text(dir_.string()), separator(), vbox(rows) | size(HEIGHT, EQUAL, listRows), separator(),
                               nameLine, foot}))
                  | size(WIDTH, EQUAL, w);
    return framed(win);
}

} /* namespace ms0515::tools */
