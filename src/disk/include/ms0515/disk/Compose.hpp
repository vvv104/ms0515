/*
 * Compose.hpp - a whole bootable diskette from a system and groups of files.
 *
 * The level under the disk wizards: it knows volumes, not manifests.  A
 * recipe names an exemplar system image, the media to build, the groups of
 * files to add and, optionally, a new startup command file; composeDisk()
 * makes the image and planDisk() says, without throwing, where every group
 * would go and what is left - the same answer, because the plan is made by
 * putting the files for real on a scratch copy.
 *
 *   ss  400 KB single-sided: one DZ volume
 *   dz  800 KB double-sided: two DZ volumes, side 0 boots
 *   dv  800 KB double-sided: one DV whole-disk volume
 *
 * The system's kit is every file of the exemplar's boot volume, in its
 * directory order, with its dates and protection - the exemplars are
 * curated kits, so nothing is picked out of them.
 */

#ifndef MS0515_DISK_COMPOSE_HPP
#define MS0515_DISK_COMPOSE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ms0515::disk {

enum class Media : uint8_t { ss, dz, dv };

/* Where a group may go.  `boot`: the volume the system boots from - a bare
 * program name runs only from DK:.  `any`: the boot volume when the group
 * fits there, else the second volume of a dz disk. */
enum class Place : uint8_t { boot, any };

struct ComposeFile {
    std::string          name;         /* 6.3, as it is to be on the disk */
    std::vector<uint8_t> data;
    uint16_t             date = 0;     /* encodeDate(); 0 = no date */
    bool                 protect = false;
};

/* Files that belong together - a loader and its data - and go onto one
 * volume, all of them or none. */
struct ComposeGroup {
    std::string              title;
    Place                    place = Place::boot;
    std::vector<ComposeFile> files;
};

/* A block that must come out of the composition as it went in: a copy
 * protection kept in sectors the file system counts as free. */
struct ReservedBlock {
    int side = 0;
    int lbn  = 0;
};

struct ComposeRecipe {
    std::vector<uint8_t> system;       /* the exemplar image, whole */
    /* true: a fresh diskette, the exemplar's kit copied onto it.  false: the
     * exemplar itself is the base and the media must be its own. */
    bool                 rebuild = true;
    Media                media = Media::ss;
    std::vector<ComposeGroup> groups;  /* placed in this order */
    /* The startup command file's lines; nullopt keeps the exemplar's. */
    std::optional<std::vector<std::string>> startup;
    /* Home-block labels, 12 characters at most; nullopt leaves what is
     * there - INIT's own on a fresh volume, the exemplar's on a kept one. */
    std::optional<std::string> volumeId, owner;               /* the boot volume */
    std::optional<std::string> secondVolumeId, secondOwner;   /* dz: side 1 */
    std::vector<ReservedBlock> reserved;
};

struct GroupPlacement {
    std::string title;
    int         volume = -1;           /* 0 boot, 1 the second; -1 did not fit */
    int         blocks = 0;
    std::string problem;               /* why it did not fit */
};

struct ComposePlan {
    bool                        ok = false;
    std::string                 problem;     /* the first thing that stops it */
    std::vector<GroupPlacement> groups;
    std::vector<int>            freeBlocks;  /* per volume, after everything placed */
};

/* The media an image is: an 800 KB DV volume, two DZ sides, one DZ side;
 * nullopt when it is none of them. */
[[nodiscard]] std::optional<Media> mediaOf(const std::vector<uint8_t> &image);

/* Where everything would go.  Never throws: a recipe that cannot be built
 * comes back with ok == false and the reason, and the groups that did fit
 * still placed. */
[[nodiscard]] ComposePlan planDisk(const ComposeRecipe &recipe);

/* The diskette.  Throws std::runtime_error with the plan's reason when the
 * recipe cannot be built. */
[[nodiscard]] std::vector<uint8_t> composeDisk(const ComposeRecipe &recipe);

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_COMPOSE_HPP */
