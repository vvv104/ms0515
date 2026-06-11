/*
 * FolderVolume.hpp — a host folder presented as an RT-11 block device.
 *
 * Pairs a folder with its `.rtfs` descriptor (see Rtfs.hpp and
 * docs/folder-device.md) and serves 512-byte blocks: the home block and
 * directory segments are generated from the descriptor on the fly, data
 * blocks map straight onto host files (reads see external edits
 * immediately; writes go to the host file).  Blocks not backed by
 * anything (free space) live in a session-only scratch store so the
 * guest can stage data there before committing a directory entry.
 *
 * The descriptor file is auto-filled on open when it lists no files, and
 * saved back whenever the folder scan discovers changes.
 */

#ifndef MS0515_DISK_FOLDERVOLUME_HPP
#define MS0515_DISK_FOLDERVOLUME_HPP

#include "ms0515/disk/Rtfs.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ms0515::disk {

class FolderVolume {
public:
    /*
     * open — Load `descriptorPath` (a `.rtfs` file inside the device
     * folder), scan the folder, auto-fill an empty descriptor (saving it
     * back).  Returns nullptr on a missing/malformed descriptor; `error`
     * (when non-null) receives the reason.
     */
    static std::unique_ptr<FolderVolume>
    open(const std::string &descriptorPath, std::string *error = nullptr);

    [[nodiscard]] int blocks() const noexcept { return desc_.blocks; }
    [[nodiscard]] RtfsDescriptor::Device deviceType() const noexcept
    { return desc_.device; }
    [[nodiscard]] const RtfsDescriptor &descriptor() const noexcept
    { return desc_; }

    /*
     * readBlock / writeBlock — 512-byte linear block access.  Reading a
     * directory block first rescans the folder, so changes show up at the
     * natural RT-11 rhythm: new host files enter the descriptor, entries
     * whose host file vanished are dropped (anything can happen outside —
     * the folder is simply accepted as it is, a renamed file re-enters as
     * a new one).  Writing a data block lands in the backing host file
     * (extended to whole blocks when needed).
     */
    void readBlock(int lbn, uint8_t *out);
    void writeBlock(int lbn, const uint8_t *in);

    /*
     * writeRange — `count` consecutive blocks in one transfer (one guest
     * DMA).  Directory edits are diffed against the descriptor only after
     * the whole range lands, so a segment rewritten by PIP is seen
     * atomically: new entries materialize host files (fed from scratch
     * blocks), removed entries become `deleted`, renames follow the
     * entry's start block.
     */
    void writeRange(int lbn, int count, const uint8_t *in);

private:
    FolderVolume() = default;

    struct Extent {
        std::size_t fileIndex = 0;  /* into desc_.files                     */
        int  start  = 0;            /* first LBN                            */
        int  blocks = 0;
    };

    void rescan();                  /* folder -> descriptor + extents       */
    void saveDescriptor();
    void generateDirectory();
    /* Manual `.rtfs` edits: a guest directory read stats the descriptor
     * (no polling — piggybacked on guest activity, the earliest moment a
     * change could become visible inside anyway) and reloads it when the
     * stamp moved.  Geometry (device/blocks) is fixed at mount time — a
     * geometry edit is ignored until a remount.  Malformed text keeps the
     * current state. */
    void noteDescriptorStamp();
    void maybeReloadDescriptor();
    void reparseDirectory();        /* guest dir edit -> descriptor diff    */
    [[nodiscard]] std::string materializeHostName(const std::string &rt11) const;
    [[nodiscard]] const Extent *extentAt(int lbn) const;
    [[nodiscard]] std::string hostPath(const std::string &name) const;

    std::string descriptorPath_;
    std::string folder_;
    std::string descriptorName_;    /* descriptor's own file name           */
    RtfsDescriptor desc_;
    std::vector<Extent>  extents_;
    std::vector<uint8_t> dirImage_; /* generated segments, kDirLbn..        */
    std::map<int, std::vector<uint8_t>> scratch_;   /* unbacked blocks      */
    std::filesystem::file_time_type descStamp_{};   /* descriptor as loaded */
    std::uintmax_t descSize_ = 0;
};

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_FOLDERVOLUME_HPP */
