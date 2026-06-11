/*
 * Rtfs.hpp — the `.rtfs` descriptor of a folder-backed block device.
 *
 * A host folder + a plain-text descriptor = an emulator block device (the
 * paravirtual HD: or a floppy).  The descriptor IS the source of truth:
 * one `file:` line per RT-11 file, line order = block order; start blocks
 * and lengths are never stored, they derive from host file sizes.  See
 * docs/folder-device.md for the full contract.
 *
 * Format:
 *   # comment
 *   device: hd                 # hd | floppy (floppy => exactly 800 blocks)
 *   blocks: 20000
 *   volume-id: MYVOL           # optional, default RT11A
 *   boot: boot.bin             # optional, hidden from RT-11
 *   file: RT11SJ.SYS | rt11sj.sys | date=1994-02-18 protected
 *   file: OLD.TXT    | old-notes.txt | deleted
 *
 * This module is pure data + string handling (parse/serialize/mangle/
 * auto-fill) so it unit-tests without any filesystem; FolderVolume does
 * the actual host I/O on top of it.
 */

#ifndef MS0515_DISK_RTFS_HPP
#define MS0515_DISK_RTFS_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ms0515::disk {

/* One descriptor line: an RT-11 file backed by a host file. */
struct RtfsFile {
    std::string rt11Name;        /* "RT11SJ.SYS" — 6.3, RAD50 charset, upper */
    std::string hostName;        /* host file name inside the folder         */
    uint16_t    date = 0;        /* encoded RT-11 date word, 0 = no date     */
    bool        isProtected = false;
    bool        deleted = false; /* hidden from RT-11; host file kept        */
};

struct RtfsDescriptor {
    enum class Device { Hd, Floppy };

    Device      device  = Device::Hd;
    int         blocks  = 0;          /* device size in 512-byte blocks      */
    std::string volumeId = "RT11A";
    std::string bootHost;             /* boot file host name; "" = none      */
    std::vector<RtfsFile> files;
};

/* The extension that marks a descriptor (lowercase, with the dot). */
inline constexpr const char *kRtfsExtension = ".rtfs";

/* Floppy devices are exactly one single-sided diskette. */
inline constexpr int kRtfsFloppyBlocks = 800;
/* RT-11 volume hard limit. */
inline constexpr int kRtfsMaxBlocks = 65535;

/*
 * parseRtfs — Parse descriptor text.  Returns nullopt on a malformed
 * document (unknown device, missing/invalid blocks, floppy with a size
 * other than 800, malformed file line, duplicate RT-11 name); when `error`
 * is non-null it receives a one-line reason.
 */
[[nodiscard]] std::optional<RtfsDescriptor>
parseRtfs(std::string_view text, std::string *error = nullptr);

/*
 * serializeRtfs — Render a descriptor back to text.  parse(serialize(d))
 * reproduces `d` exactly.
 */
[[nodiscard]] std::string serializeRtfs(const RtfsDescriptor &d);

/*
 * mangleRt11Name — Derive a valid RT-11 6.3 name from a host file name:
 * uppercase, RAD50 charset (A-Z 0-9 $), name truncated to 6 and extension
 * to 3 chars, empty name becomes "FILE".  If the result collides with a
 * name in `taken`, the name's tail is replaced with a number until unique.
 */
[[nodiscard]] std::string
mangleRt11Name(std::string_view hostName,
               const std::vector<std::string> &taken = {});

/*
 * autoFillRtfs — Populate an empty descriptor from a folder listing
 * (hostName + size in bytes, in enumeration order).  `.SYS` files go
 * first — SWAP.SYS, then RT11SJ.SYS, then remaining `.SYS` in listing
 * order — then everything else in listing order.  Files that do not fit
 * on the device are skipped.  No-op unless d.files is empty.
 */
struct RtfsHostFile {
    std::string name;
    uint64_t    size = 0;
    uint16_t    date = 0;        /* optional pre-encoded mtime date          */
};
void autoFillRtfs(RtfsDescriptor &d, const std::vector<RtfsHostFile> &listing);

/*
 * rtfsLayout — Derive the block layout: for every live (non-deleted)
 * entry, its start block and length in blocks, sequential from the first
 * data block (after boot/home/directory).  Entries that no longer fit are
 * reported with start == -1.  `sizeOf(hostName)` supplies current host
 * sizes (missing host file => report length from `missingBlocks`, the
 * NAME.BAD presentation is FolderVolume's concern).
 */
struct RtfsExtent {
    const RtfsFile *file = nullptr;
    int start  = 0;              /* first LBN; -1 = does not fit             */
    int blocks = 0;
};
[[nodiscard]] std::vector<RtfsExtent>
rtfsLayout(const RtfsDescriptor &d,
           const std::vector<uint64_t> &liveSizes,
           int dirSegments = 4);

/* First data block for a given directory size (boot+home+dir precede). */
[[nodiscard]] inline int rtfsDataStart(int dirSegments = 4)
{
    return 6 + 2 * dirSegments;
}

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_RTFS_HPP */
