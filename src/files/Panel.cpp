/*
 * Panel.cpp — the cursor, the marks and the selection over a volume.
 */
#include "Panel.hpp"

#include <algorithm>
#include <cctype>

namespace ms0515::files {

namespace {

std::string extensionOf(const std::string &name)
{
    const auto dot = name.find('.');
    return dot == std::string::npos ? "" : name.substr(dot + 1);
}

bool matchAt(const std::string &name, size_t i, const std::string &pat, size_t j)
{
    while (j < pat.size()) {
        if (pat[j] == '*') {
            for (size_t k = i; k <= name.size(); ++k)
                if (matchAt(name, k, pat, j + 1)) return true;
            return false;
        }
        if (i >= name.size()) return false;
        if (pat[j] != '?' && std::toupper(static_cast<unsigned char>(pat[j])) != std::toupper(static_cast<unsigned char>(name[i]))) return false;
        ++i;
        ++j;
    }
    return i == name.size();
}

} // namespace

bool matchPattern(const std::string &name, const std::string &pattern)
{
    return matchAt(name, 0, pattern, 0);
}

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

/* The listing from the volume, the areas dropped when hidden, in order. */
void Panel::load()
{
    entries_ = location_ ? location_->list() : std::vector<Entry>{};
    if (!showUnused_)
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry &e) { return e.empty; }), entries_.end());
    sortEntries();
}

void Panel::reload()
{
    const auto keep = current();
    load();
    for (auto it = marks_.begin(); it != marks_.end();) {
        const bool there = std::any_of(entries_.begin(), entries_.end(),
                                       [&](const Entry &e) { return !e.empty && e.name == *it; });   /* the area of a deleted file keeps its name: not a mark */
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
    load();
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
    if (!cur->empty && !marks_.erase(cur->name)) marks_.insert(cur->name);   /* an area takes no mark */
    moveCursor(1);
}

void Panel::clearMarks() { marks_.clear(); }

void Panel::markPattern(const std::string &pattern, bool on)
{
    for (const auto &e : entries_) {
        if (e.empty || !matchPattern(e.name, pattern)) continue;
        if (on) marks_.insert(e.name); else marks_.erase(e.name);
    }
}

void Panel::invertMarks()
{
    for (const auto &e : entries_)
        if (!e.empty && !marks_.erase(e.name)) marks_.insert(e.name);
}

uint32_t Panel::markedBlocks() const
{
    uint32_t blocks = 0;
    for (const auto &e : entries_)
        if (marks_.count(e.name)) blocks += e.blocks;
    return blocks;
}

void Panel::setSort(SortOrder order, bool reversed)
{
    const auto keep = current();
    sort_ = order;
    reversed_ = reversed;
    sortEntries();
    placeCursor(keep);
}

void Panel::setShowUnused(bool on)
{
    if (showUnused_ == on) return;
    showUnused_ = on;
    reload();
}

void Panel::sortEntries()
{
    /* a nameless area sorts after every name */
    const auto key = [](const Entry &e) { return e.name.empty() ? std::string("\x7F") : e.name; };
    const auto less = [this, &key](const Entry &a, const Entry &b) {
        switch (sort_) {
        case SortOrder::offset:    return a.offset != b.offset ? a.offset < b.offset : a.ordinal < b.ordinal;
        case SortOrder::extension: { const auto ea = extensionOf(a.name), eb = extensionOf(b.name); return ea != eb ? ea < eb : key(a) < key(b); }
        case SortOrder::size:      return a.blocks != b.blocks ? a.blocks < b.blocks : key(a) < key(b);
        case SortOrder::date:      return a.date != b.date ? a.date < b.date : key(a) < key(b);
        case SortOrder::name:      break;
        }
        return key(a) < key(b);
    };
    std::stable_sort(entries_.begin(), entries_.end(), less);
    if (reversed_) std::reverse(entries_.begin(), entries_.end());
}

std::vector<Entry> Panel::selection() const
{
    std::vector<Entry> out;
    if (!marks_.empty()) {
        for (const auto &e : entries_)
            if (!e.empty && marks_.count(e.name)) out.push_back(e);
        return out;
    }
    if (const auto cur = current(); cur && !cur->empty) out.push_back(*cur);
    return out;
}

void Panel::placeCursor(const std::optional<Entry> &keep)
{
    if (!keep) { moveCursor(0); return; }
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) {
        return keep->name.empty() ? e.empty && e.offset == keep->offset : e.name == keep->name;
    });
    if (it != entries_.end()) cursor_ = static_cast<int>(it - entries_.begin());
    else moveCursor(0);
}

} /* namespace ms0515::files */
