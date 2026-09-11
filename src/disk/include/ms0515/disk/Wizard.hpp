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
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

/* One line of the wizard's list. */
struct WizardRow {
    enum class Kind : uint8_t {
        group,      /* a heading: "Development / Pascal" */
        radio,      /* the heading of alternatives: one of them, or none */
        bundle,     /* a checkbox, or a radio button under a radio heading */
    };
    enum class Mark : uint8_t {
        off,        /* [ ]  / ( ) */
        on,         /* [x]  / (o) - chosen by the user */
        added,      /* [+]  / (o) - brought by what requires it */
        system,     /* [#]  - a part of the system, not the user's to take away */
    };
    Kind        kind = Kind::bundle;
    int         depth = 0;
    std::string key;           /* the bundle's key; the provided name of a radio heading */
    std::string title;
    Mark        mark = Mark::off;
    bool        radio = false; /* a button of the radio heading above it */
    bool        available = true;
    std::string why;           /* when not available: the reason */
    std::string requiredBy;    /* for Mark::added: the title of what needs it */
    int         blocks = 0;
};

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
     * the rows show 0. */
    DiskWizard(const Manifest &manifest, std::string system, Media media,
               std::function<int(const ManifestBundle &)> blocksOf = {});

    [[nodiscard]] const Manifest &manifest() const noexcept { return m_; }

    /* The system and the media.  A choice that no longer fits is dropped,
     * and notices() says what went. */
    void setSystem(const std::string &key);
    void setMedia(Media media);
    [[nodiscard]] const std::string &system() const noexcept { return sel_.system; }
    [[nodiscard]] Media media() const noexcept { return sel_.media; }
    [[nodiscard]] std::vector<Media> mediaOffered() const;

    /* Space on a row: a checkbox turns on or off, a radio button is picked
     * (or, picked already and needed by nothing, cleared).  Returns "" when
     * the action was taken, else why not - and nothing changed. */
    std::string toggle(const std::string &bundleKey);

    void setStartup(std::vector<std::string> lines);
    void setVolumeId(std::optional<std::string> id);

    [[nodiscard]] const Selection &selection() const noexcept { return sel_; }
    [[nodiscard]] const Resolution &resolution() const noexcept { return res_; }
    [[nodiscard]] std::vector<WizardRow> rows() const;

    /* What the last change of system or media took away. */
    [[nodiscard]] const std::vector<std::string> &notices() const noexcept { return notices_; }

    /* A saved choice, over this manifest: names it no longer knows are
     * skipped, and a collection version other than this manifest's is
     * reported - both in notices(). */
    void load(const SavedSelection &saved);
    [[nodiscard]] SavedSelection saved() const;

private:
    void resolve();
    void dropWhatDoesNotFit();
    [[nodiscard]] std::vector<std::string> alternativesOf(const ManifestBundle &b) const;
    [[nodiscard]] bool isSystemPart(const std::string &key) const;

    const Manifest &m_;
    std::function<int(const ManifestBundle &)> blocksOf_;
    Selection sel_;
    Resolution res_;
    std::vector<std::string> notices_;
};

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_WIZARD_HPP */
