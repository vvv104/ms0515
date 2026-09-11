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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

struct ManifestSystem {
    std::string                key, title, image;
    std::vector<Media>         media;          /* what the monitor boots from */
    bool                       rebuild = true;
    std::vector<ReservedBlock> reserved;
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
 * naming what is not there, a date outside 1972..2003. */
[[nodiscard]] Manifest parseManifest(std::string_view text);

/* What the user chose: a preset's choices, or the wizard's. */
struct Selection {
    std::string                             system;
    Media                                   media = Media::ss;
    std::vector<std::string>                bundles;
    std::optional<std::vector<std::string>> startup;
    std::optional<std::string>              volumeId;
};

[[nodiscard]] Selection selectionOf(const ManifestPreset &preset);

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

/* The recipe for a selection, the system image and every file read.
 * Throws std::runtime_error when the selection breaks a rule or a file
 * cannot be read; whether it FITS is the plan's business (planDisk). */
[[nodiscard]] ComposeRecipe recipeFor(const Manifest &m, const Selection &s, const Repository &repo);

[[nodiscard]] std::optional<Media> parseMedia(std::string_view word);
[[nodiscard]] const char *mediaWord(Media m);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_MANIFEST_HPP */
