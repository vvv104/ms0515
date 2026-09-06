/*
 * Panel.hpp — one of the two panels: a device's volume, its listing, a
 * cursor and the marks.  Pure state, no drawing: the terminal front-end
 * asks what to show and tells it what key came.  Testable without a
 * terminal.
 */
#ifndef MS0515_FILES_PANEL_HPP
#define MS0515_FILES_PANEL_HPP

#include "Location.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ms0515::files {

class Panel {
public:
    /* An empty panel: no device chosen yet. */
    Panel() = default;
    explicit Panel(Location location);

    [[nodiscard]] bool hasLocation() const noexcept { return location_.has_value(); }
    [[nodiscard]] const Location &location() const { return *location_; }
    [[nodiscard]] Location &location() { return *location_; }
    [[nodiscard]] const std::vector<Entry> &entries() const noexcept { return entries_; }
    [[nodiscard]] int cursor() const noexcept { return cursor_; }
    /* The entry under the cursor; nullopt on an empty listing. */
    [[nodiscard]] std::optional<Entry> current() const;
    [[nodiscard]] bool isMarked(const std::string &name) const { return marks_.count(name) != 0; }
    [[nodiscard]] int markedCount() const noexcept { return static_cast<int>(marks_.size()); }
    /* "DZ0: osa.dsk" or "no disk" for the header. */
    [[nodiscard]] std::string title() const;

    /* Re-read the listing; the cursor stays on the same name when it is
     * still there, marks on names that vanished are dropped. */
    void reload();
    /* Show another device's volume (the cursor at the top, marks cleared). */
    void show(Location location);
    void clear();

    /* Cursor motion; `rows` is the panel's visible height for paging. */
    void moveCursor(int delta);
    void pageUp(int rows);
    void pageDown(int rows);
    void home();
    void end();
    /* The scroll offset the front-end draws from, for `rows` visible lines:
     * it follows the cursor only when the cursor leaves the visible rows, so
     * the listing does not jump about (mc's way). */
    [[nodiscard]] int scrollTop(int rows) const;

    /* Insert toggles the mark under the cursor and moves down. */
    void toggleMark();
    void clearMarks();
    /* What an operation acts on: the marked entries, else the one under
     * the cursor. */
    [[nodiscard]] std::vector<Entry> selection() const;

private:
    std::optional<Location> location_;
    std::vector<Entry>      entries_;
    int                     cursor_ = 0;
    mutable int             top_ = 0;
    std::set<std::string>   marks_;

    void placeCursor(const std::string &name);
};

} /* namespace ms0515::files */

#endif /* MS0515_FILES_PANEL_HPP */
