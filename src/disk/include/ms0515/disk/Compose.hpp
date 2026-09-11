/*
 * Compose.hpp - a whole bootable diskette, made from scratch.
 *
 * The level under the disk wizards: it knows volumes, not manifests.  A
 * blank of the media is formatted; the exemplar system image gives only
 * what exists nowhere else - its SWAP.SYS, its monitor and the blocks it
 * protects; the groups bring everything else, the system's own handlers and
 * utilities first; then the startup command file and the bootstrap for the
 * media.  composeDisk() makes the image and planDisk() says, without
 * throwing, where every group would go and what is left - the same answer,
 * because the plan is made by putting the files for real on a scratch copy.
 *
 *   ss  400 KB single-sided: one DZ volume
 *   dz  800 KB double-sided: two DZ volumes, side 0 boots
 *   dv  800 KB double-sided: one DV whole-disk volume
 *
 * The disk boots when the groups brought the boot device's handler - DZ.SYS
 * for a floppy volume, DV.SYS for a DV one; without it the recipe is refused.
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

/* A block copied from the exemplar as it is - a copy protection kept in
 * sectors the file system counts as free - that no file may take. */
struct ReservedBlock {
    int side = 0;
    int lbn  = 0;
};

struct ComposeRecipe {
    std::vector<uint8_t> system;       /* the exemplar image, whole */
    Media                media = Media::ss;
    std::vector<ComposeGroup> groups;  /* placed in this order */
    /* The startup command file's lines; nullopt copies the exemplar's. */
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
