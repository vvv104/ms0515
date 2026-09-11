/*
 * Manifest.hpp - the software collection's disks.toml, and a choice made
 * over it turned into a composition recipe.
 *
 * The rules live here once, for both wizards: which media a system boots,
 * which bundles a system and a media allow, how a bundle's paths become
 * files on the disk.  The files themselves come from a Repository - a local
 * copy of the collection for the native tool, the collection's Pages for the
 * web build - so this knows no file system and no network.
 */

#ifndef MS0515_DISK_MANIFEST_HPP
#define MS0515_DISK_MANIFEST_HPP

#include "ms0515/disk/Compose.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

/* A system: its exemplar image gives SWAP.SYS, the monitor and the blocks
 * it protects; everything else on the disk is bundles. */
struct ManifestSystem {
    std::string                key, title, image;
    std::vector<Media>         media;          /* what the monitor boots from */
    std::vector<ReservedBlock> reserved;       /* copied from the image */
    /* The bundles it cannot work without (TOML `requires`), and those it
     * needs on one media only (`requires_by_media`: DV.SYS for dv). */
    std::vector<std::string>   dependsOn;
    std::map<Media, std::vector<std::string>> dependsOnByMedia;
    /* The first lines of the startup command file (TOML `startup`). */
    std::optional<std::vector<std::string>> startup;
};

struct ManifestFile {
    std::string                pattern;        /* a path, or a glob with '*' in its last part */
    std::optional<std::string> as;             /* the name on the disk */
    std::optional<std::string> date;           /* YYYY-MM-DD, overriding the bundle's */
};

struct ManifestBundle {
    std::string               key, title;
    std::vector<ManifestFile> files;
    Place                     place = Place::boot;
    std::vector<Media>        needs;           /* empty: any media */
    std::vector<std::string>  systems;         /* empty: every system */
    std::string               date;            /* YYYY-MM-DD, "" none */
    bool                      protect = false;
    /* Where the wizard lists it: "Development / Pascal", "" at the top. */
    std::string               group;
    /* Names this bundle satisfies besides its own key.  The bundles that
     * provide one name are alternatives: one of them goes on a disk. */
    std::vector<std::string>  provides;
    /* What it needs installed with it: bundle keys or provided names
     * (TOML `requires`). */
    std::vector<std::string>  dependsOn;
    /* Of the alternatives a need has, the ones to take first - the build
     * that sat next to this one on the original disks.  TOML `prefer` is a
     * list, or a table by system ({ mihin = [...], default = [...] }): the
     * list is the default, the table's entries override it per system. */
    std::vector<std::string>  prefer;
    std::map<std::string, std::vector<std::string>> preferBySystem;
    /* Lines it adds to the startup command file (SET SL ON). */
    std::vector<std::string>  startup;
};

struct ManifestPreset {
    std::string                             key, title, system;
    Media                                   media = Media::ss;
    std::vector<std::string>                bundles;
    std::optional<std::vector<std::string>> startup;
    std::optional<std::string>              volumeId;
};

struct Manifest {
    std::optional<std::string>  owner;         /* the owner written on every volume */
    std::vector<ManifestSystem> systems;       /* in the file's order */
    std::vector<ManifestBundle> bundles;
    std::vector<ManifestPreset> presets;

    [[nodiscard]] const ManifestSystem *system(std::string_view key) const;
    [[nodiscard]] const ManifestBundle *bundle(std::string_view key) const;
    [[nodiscard]] const ManifestPreset *preset(std::string_view key) const;
};

/* Parse disks.toml.  Throws std::runtime_error naming what is wrong: a TOML
 * syntax error with its line, an unknown media word, a bundle or preset
 * naming what is not there, a date outside 1972..2003, a need nothing
 * provides, a preference that is no alternative of a need, a cycle. */
[[nodiscard]] Manifest parseManifest(std::string_view text);

/* What the user chose: a preset's choices, or the wizard's. */
struct Selection {
    std::string                             system;
    Media                                   media = Media::ss;
    std::vector<std::string>                bundles;
    /* Lines after the system's and the bundles' (R ROSA3). */
    std::optional<std::vector<std::string>> startup;
    std::optional<std::string>              volumeId;
    /* The user's choice among alternatives: provided name -> bundle key. */
    std::map<std::string, std::string>      picks;
};

[[nodiscard]] Selection selectionOf(const ManifestPreset &preset);

/* The bundles that can satisfy `need` on this system and media, in the
 * file's order: the bundle of that key, or every bundle providing it. */
[[nodiscard]] std::vector<const ManifestBundle *> candidatesFor(const Manifest &m, std::string_view need,
                                                                const std::string &system, Media media);

/* What a choice installs: the system's own bundles first, then the bundles
 * chosen and, through `requires`, what they need - each once, a need before
 * what needs it.  An alternative is
 * the one chosen outright, else the user's pick, else the first the
 * needing bundle prefers, else the first in the file; two bundles
 * providing one name never go together. */
struct Resolution {
    bool                                             ok = false;
    std::string                                      problem;
    std::vector<std::string>                         bundles;
    std::vector<std::pair<std::string, std::string>> addedFor;   /* (added, for whom) */
};

[[nodiscard]] Resolution resolveBundles(const Manifest &m, const std::string &system, Media media,
                                        const std::vector<std::string> &chosen,
                                        const std::map<std::string, std::string> &picks = {});

/* Why a bundle cannot go with this system and media, "" when it can - what
 * greys a checkbox out. */
[[nodiscard]] std::string bundleRefusal(const Manifest &m, const ManifestBundle &b,
                                        const std::string &system, Media media);

/* The collection as far as composing needs it: every file's path, relative
 * and with '/', and a way to read one. */
struct Repository {
    std::vector<std::string>                                            paths;
    std::function<std::optional<std::vector<uint8_t>>(const std::string &)> read;
};

/* A bundle's files in their order, globs expanded in name order to the
 * files whose names are RT-11 names (a README.md next to them is not).
 * Throws when a path is not in the repository or a glob matches nothing. */
[[nodiscard]] std::vector<std::string> bundlePaths(const ManifestBundle &b, const Repository &repo);

/* The recipe for a selection, its needs resolved, the system image and
 * every file read, the startup file made of the system's lines, the chosen
 * bundles' and the selection's, each line once.  Throws std::runtime_error when the selection breaks a
 * rule or a file cannot be read; whether it FITS is the plan's business
 * (planDisk). */
[[nodiscard]] ComposeRecipe recipeFor(const Manifest &m, const Selection &s, const Repository &repo);

[[nodiscard]] std::optional<Media> parseMedia(std::string_view word);
[[nodiscard]] const char *mediaWord(Media m);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_MANIFEST_HPP */
