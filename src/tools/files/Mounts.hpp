/*
 * Mounts.hpp — which image is behind which device.
 *
 * The same slots the emulator has: drive A and drive B, each either a
 * two-sided image (both sides) or a single-sided image per side, and the
 * paravirtual HD.  They come from the same two places the emulator reads
 * them from - the command line (--disk0, --disk0-side1, --disk1, --hd,
 * libapp's parser, so every flag of ms0515.exe works here too) and
 * ms0515.yaml next to the binaries - so ms0515-files starts on the disks
 * the emulator had last, and a mount made here is what the emulator
 * mounts next.  That is the whole convenience: no separate mount state
 * to keep in sync.
 *
 * What a slot presents follows from the image's content (detectVolumes):
 * a two-sided DZ image gives two devices, DZn: and DZn+2:; an image whose
 * content is one DV: / MZ: whole-diskette volume gives one, DV0: / MZ0:;
 * any other 512-multiple in the HD slot is HD0:, addressed linearly.
 */
#ifndef MS0515_FILES_MOUNTS_HPP
#define MS0515_FILES_MOUNTS_HPP

#include "Location.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ms0515::app { struct CliArgs; class Config; }

namespace ms0515::files {

enum class Slot { driveA, driveB, hd };

struct Mount {
    Slot                  slot;
    std::filesystem::path image;
    std::vector<disk::VolumeSpec> volumes;   /* what the image parsed as */
};

class Mounts {
public:
    Mounts() = default;
    /* The emulator's mounts: the command line over the config. */
    static Mounts fromEmulator(const app::CliArgs &cli, const app::Config &config);

    /* Mount `image` in `slot` - `side` 0/1 for a single-sided image in a
     * floppy slot (a two-sided image or a DV/MZ volume takes the whole
     * drive; the HD slot takes any 512-multiple).  Returns "" or why not:
     * unreadable, a size no slot takes, no RT-11 volume inside (mount it
     * anyway with `force` to initialise it). */
    std::string mount(Slot slot, const std::filesystem::path &image, int side = 0, bool force = false);
    void unmount(Slot slot);
    [[nodiscard]] std::optional<Mount> mounted(Slot slot) const;

    /* The devices the panels can show, in guest order: DZ0: DZ2: DZ1: DZ3:
     * (or DV0: / MZ0: in their place), HD0:. */
    [[nodiscard]] std::vector<Device> devices() const;
    [[nodiscard]] std::optional<Device> device(const std::string &name) const;

    /* Write the mounts into `config` (the emulator's slots) so they persist. */
    void store(app::Config &config) const;

    /* What the size of an image file allows: which slots and sides. */
    [[nodiscard]] static std::string describe(const std::filesystem::path &image);

private:
    struct FloppySlot {
        std::filesystem::path ds;          /* a two-sided image, or a DV/MZ volume */
        std::filesystem::path side[2];     /* single-sided images per side */
        std::vector<disk::VolumeSpec> dsVolumes;
    };
    FloppySlot            fd_[2];
    std::filesystem::path hd_;

    [[nodiscard]] std::vector<Device> floppyDevices(int drive) const;
};

/* A host path typed at a prompt, helped along: the completions of `prefix`
 * (directories with a trailing separator, image files), for Tab. */
[[nodiscard]] std::vector<std::string> completePath(const std::string &prefix);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_MOUNTS_HPP */
