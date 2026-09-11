/*
 * Wizard.hpp - the disk wizards' model: a choice over disks.toml made step
 * by step, and the file a choice is saved to.
 *
 * No drawing here.  The native wizard (FTXUI) and the browser's draw the
 * same rows and call the same actions, so the two cannot come to behave
 * differently: what is offered, what is greyed out and why, what came along
 * as a dependency and for whom, which alternatives form a radio group.
 */

#ifndef MS0515_DISK_WIZARD_HPP
#define MS0515_DISK_WIZARD_HPP

#include "ms0515/disk/Manifest.hpp"

#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

/* One line of the wizard's list.  The list is a tree walked in steps: the
 * diskette and its label, then the operating system on it, then the groups
 * of bundles - "Games / Pac-Man" a branch "Pac-Man" under "Games" - with
 * the startup file's lines at the end of the group of the system's parts. */
struct WizardRow {
    enum class Kind : uint8_t {
        group,      /* a branch: "Diskette", "Operating system", "Development", "Pascal" */
        radio,      /* the heading of alternatives: one of them, or none */
        bundle,     /* a checkbox, or a radio button under a radio heading */
        media,      /* a radio button under "Diskette" */
        system,     /* a radio button under "Operating system" */
        field,      /* an edit box: the volume id, one of the user's START.COM lines */
        line,       /* a START.COM line the system or a bundle brings: title the line */
    };
    enum class Mark : uint8_t {
        off,        /* [ ]  / ( ) */
        on,         /* [x]  / (o) - chosen by the user */
        added,      /* [+]  / (o) - brought by what requires it */
        system,     /* [#]  - a part of the system, not the user's to take away */
    };
    Kind        kind = Kind::bundle;
    int         depth = 0;
    std::string key;           /* the bundle's, the system's, the media word; a group's path; a radio heading's name */
    std::string title;
    std::string parent;        /* the key of the group it sits in */
    Mark        mark = Mark::off;
    bool        radio = false; /* drawn as a radio button */
    bool        available = true;
    std::string why;           /* when not available: the reason */
    std::string requiredBy;    /* for Mark::added: the title of what needs it; for a line: whose it is */
    bool        native = false;/* the system's own build among alternatives */
    std::string value;         /* a field: its text */
    int         blocks = 0;
    bool        open = false;  /* a group: its rows follow */
    std::string summary;       /* a group: the diskette or the system chosen, "2 chosen, 1 added"; a field: a hint */
};

/* The keys of the two first steps' groups; a bundle group's key is its path. */
inline constexpr const char *kDisketteGroup = "#diskette";
inline constexpr const char *kLabelGroup = "#label";
inline constexpr const char *kSystemGroup = "#system";
inline constexpr const char *kStartupGroup = "#startup";
/* The fields' keys: the volume id; START.COM's own line N ("#startup:N", N
 * one past the last: a new line). */
inline constexpr const char *kVolumeIdField = "#volume-id";
inline constexpr const char *kOwnerField = "#owner";
inline constexpr const char *kSecondVolumeIdField = "#volume-id-2";     /* dz: side 1 */
inline constexpr const char *kSecondOwnerField = "#owner-2";
inline constexpr const char *kStartupField = "#startup:";

/* "dz - two sides, 800 KB" */
[[nodiscard]] const char *mediaTitle(Media m);

/* A choice saved to its own file, tied to the version of the collection's
 * disks.toml it was made over:
 *   format = 1, collection, system, media, bundles, picks, startup, volume_id
 * Only the person's decisions: the system's parts and the dependencies are
 * derived again, by the rules of the collection that reads it. */
struct SavedSelection {
    std::string collection;    /* disks.toml's version; "" unknown */
    Selection   selection;
};

[[nodiscard]] std::string selectionToml(const SavedSelection &saved);

/* Read such a file.  Throws std::runtime_error on a file that is not one
 * (TOML syntax, format, a media word); whether its names exist, and whether
 * its collection is this one, is the wizard's business (DiskWizard::load). */
[[nodiscard]] SavedSelection parseSelection(std::string_view text);

class DiskWizard {
public:
    /* `blocksOf` measures a bundle for the rows (the files' blocks); none:
     * the rows show 0.  Nothing chosen yet: the diskette is the first step. */
    explicit DiskWizard(const Manifest &manifest, std::function<int(const ManifestBundle &)> blocksOf = {});
    /* Both first steps taken: a system and the media (the system's first
     * media when it does not go on this one). */
    DiskWizard(const Manifest &manifest, std::string system, Media media,
               std::function<int(const ManifestBundle &)> blocksOf = {});

    [[nodiscard]] const Manifest &manifest() const noexcept { return m_; }

    /* The diskette: always taken.  A system that does not go on it is dropped,
     * and with the system any bundle that no longer fits - notices() says what
     * went.  Returns "" (the form of the other actions). */
    std::string setMedia(Media media);
    /* The system: "" when taken, else why not (no diskette yet, one it does
     * not go on) - and nothing changed. */
    std::string setSystem(const std::string &key);
    [[nodiscard]] const std::string &system() const noexcept { return sel_.system; }
    [[nodiscard]] std::optional<Media> media() const noexcept { return media_; }
    /* Both chosen: the bundles, a plan and a build are open. */
    [[nodiscard]] bool ready() const noexcept { return media_.has_value() && !sel_.system.empty(); }

    /* Space on a row: a checkbox turns on or off, a radio button is picked
     * (or, picked already and needed by nothing, cleared).  Returns "" when
     * the action was taken, else why not - and nothing changed. */
    std::string toggle(const std::string &bundleKey);

    /* A group opened or closed; reveal() opens the way down to a group, as a
     * search landing inside it needs. */
    void toggleFold(const std::string &groupKey);
    void reveal(const std::string &groupKey);

    void setStartup(std::vector<std::string> lines);
    void setVolumeId(std::optional<std::string> id);
    /* A field's text, as typed: "" when taken, else why not.  A volume id or
     * an owner keeps twelve characters, in capitals, and emptied is unset; a
     * START.COM line emptied goes, one typed into the new line is added. */
    std::string setField(const std::string &key, const std::string &value);

    [[nodiscard]] const Selection &selection() const noexcept { return sel_; }
    [[nodiscard]] const Resolution &resolution() const noexcept { return res_; }
    /* The rows to draw: those of closed groups left out, unless `everything`
     * (what a search looks through).  A locked group has no rows either way. */
    [[nodiscard]] std::vector<WizardRow> rows(bool everything = false) const;

    /* What the last change of system or media took away. */
    [[nodiscard]] const std::vector<std::string> &notices() const noexcept { return notices_; }

    /* A saved choice, over this manifest: names it no longer knows are
     * skipped, and a collection version other than this manifest's is
     * reported - both in notices().  The branches holding what it chose
     * open; the rest stay folded. */
    void load(const SavedSelection &saved);
    [[nodiscard]] SavedSelection saved() const;

private:
    struct Branch;

    void resolve();
    void dropWhatDoesNotFit();
    [[nodiscard]] std::vector<std::string> alternativesOf(const ManifestBundle &b) const;
    [[nodiscard]] bool isSystemPart(const std::string &key) const;
    [[nodiscard]] std::string systemRefusal(const ManifestSystem &s) const;
    [[nodiscard]] WizardRow::Mark markOf(const ManifestBundle &b) const;
    [[nodiscard]] WizardRow bundleRow(const ManifestBundle &b, int depth, bool radio) const;
    [[nodiscard]] Branch tree() const;
    void stepRows(std::vector<WizardRow> &out, bool everything) const;
    void labelRows(std::vector<WizardRow> &out, bool everything) const;
    void openWhatIsChosen();
    [[nodiscard]] std::string startupHome() const;
    void startupRows(std::vector<WizardRow> &out, int depth, const std::string &parent, bool everything) const;
    void branchRows(std::vector<WizardRow> &out, const Branch &branch, int depth, bool everything) const;
    void leafRows(std::vector<WizardRow> &out, const Branch &branch, int depth) const;

    const Manifest &m_;
    std::function<int(const ManifestBundle &)> blocksOf_;
    std::optional<Media> media_;
    Selection sel_;
    Resolution res_;
    Resolution own_;                                 /* the system's parts alone */
    std::vector<std::string> notices_;
    std::set<std::string> open_;
};

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_WIZARD_HPP */
