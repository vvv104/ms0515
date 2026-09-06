/*
 * Panel.cpp — the cursor, the marks and the selection over a volume.
 */
#include "Panel.hpp"

#include <algorithm>

namespace ms0515::files {

Panel::Panel(Location location) : location_(std::move(location))
{
    reload();
}

std::optional<Entry> Panel::current() const
{
    if (entries_.empty()) return std::nullopt;
    return entries_[static_cast<size_t>(cursor_)];
}

std::string Panel::title() const
{
    return location_ ? location_->title() : "no disk";
}

void Panel::reload()
{
    const std::string keep = current() ? current()->name : "";
    entries_ = location_ ? location_->list() : std::vector<Entry>{};
    for (auto it = marks_.begin(); it != marks_.end();) {
        const bool there = std::any_of(entries_.begin(), entries_.end(),
                                       [&](const Entry &e) { return e.name == *it; });
        it = there ? std::next(it) : marks_.erase(it);
    }
    placeCursor(keep);
}

void Panel::show(Location location)
{
    location_ = std::move(location);
    marks_.clear();
    cursor_ = 0;
    top_ = 0;
    entries_ = location_->list();
}

void Panel::clear()
{
    location_.reset();
    entries_.clear();
    marks_.clear();
    cursor_ = 0;
    top_ = 0;
}

void Panel::moveCursor(int delta)
{
    if (entries_.empty()) { cursor_ = 0; return; }
    const int last = static_cast<int>(entries_.size()) - 1;
    cursor_ = std::clamp(cursor_ + delta, 0, last);
}

void Panel::pageUp(int rows) { moveCursor(-std::max(1, rows)); }
void Panel::pageDown(int rows) { moveCursor(std::max(1, rows)); }
void Panel::home() { cursor_ = 0; }
void Panel::end() { cursor_ = entries_.empty() ? 0 : static_cast<int>(entries_.size()) - 1; }

int Panel::scrollTop(int rows) const
{
    if (rows <= 0) return 0;
    const int n = static_cast<int>(entries_.size());
    const int maxTop = std::max(0, n - rows);
    if (cursor_ < top_) top_ = cursor_;
    if (cursor_ >= top_ + rows) top_ = cursor_ - rows + 1;
    top_ = std::clamp(top_, 0, maxTop);
    return top_;
}

void Panel::toggleMark()
{
    const auto cur = current();
    if (!cur) return;
    if (!marks_.erase(cur->name)) marks_.insert(cur->name);
    moveCursor(1);
}

void Panel::clearMarks() { marks_.clear(); }

std::vector<Entry> Panel::selection() const
{
    std::vector<Entry> out;
    if (!marks_.empty()) {
        for (const auto &e : entries_)
            if (marks_.count(e.name)) out.push_back(e);
        return out;
    }
    if (const auto cur = current()) out.push_back(*cur);
    return out;
}

void Panel::placeCursor(const std::string &name)
{
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry &e) { return e.name == name; });
    if (it != entries_.end()) cursor_ = static_cast<int>(it - entries_.begin());
    else moveCursor(0);
}

} /* namespace ms0515::files */
