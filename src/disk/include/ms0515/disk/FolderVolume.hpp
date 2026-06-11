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
     * directory block first rescans the folder (new/changed/missing host
     * files show up at the natural RT-11 rhythm); a missing host file's
     * entry is presented as NAME.BAD.  Writing a data block lands in the
     * backing host file (extended to whole blocks when needed).
     */
    void readBlock(int lbn, uint8_t *out);
    void writeBlock(int lbn, const uint8_t *in);

private:
    FolderVolume() = default;

    struct Extent {
        std::size_t fileIndex = 0;  /* into desc_.files                     */
        int  start  = 0;            /* first LBN                            */
        int  blocks = 0;
        bool missing = false;       /* host file gone -> NAME.BAD, reads 0  */
    };

    void rescan();                  /* folder -> descriptor + extents       */
    void saveDescriptor() const;
    void generateDirectory();
    [[nodiscard]] const Extent *extentAt(int lbn) const;
    [[nodiscard]] std::string hostPath(const std::string &name) const;

    std::string descriptorPath_;
    std::string folder_;
    std::string descriptorName_;    /* descriptor's own file name           */
    RtfsDescriptor desc_;
    std::vector<Extent>  extents_;
    std::vector<uint8_t> dirImage_; /* generated segments, kDirLbn..        */
    std::map<int, std::vector<uint8_t>> scratch_;   /* unbacked blocks      */
};

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_FOLDERVOLUME_HPP */
