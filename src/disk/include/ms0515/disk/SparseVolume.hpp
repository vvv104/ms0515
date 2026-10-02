/*
 * SparseVolume.hpp — a block volume that holds only the blocks in use.
 *
 * An image file keeps every block of a medium, written or not: a diskette
 * with five files on it is still 400 KB, most of it the pattern the
 * formatter left.  A sparse volume is a size in blocks and the blocks
 * that hold something; a block it does not hold reads as zeros, and
 * writing one adds it.  So it is as large as what is on it, grows as it
 * is written, and its size in blocks can be raised at any time.
 *
 * Blocks are RT-11's logical blocks (block n of the volume), not a
 * medium's sectors: the same volume can stand behind the floppy
 * controller, the hard disk, or be read by the tools, whatever geometry
 * each gives it.
 *
 * It has a serial form, for keeping it in a file or inside a program:
 *
 *   8 bytes  "MS0515SV"
 *   u16      format version (1)            (all numbers little-endian)
 *   u16      block size (512)
 *   u32      size of the volume, in blocks
 *   u32      number of blocks held
 *   then, for each block held, in rising order:
 *   u32      block number
 *   512 b    the block
 */

#ifndef MS0515_DISK_SPARSEVOLUME_HPP
#define MS0515_DISK_SPARSEVOLUME_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ms0515::disk {

class SparseVolume {
public:
    /* An empty volume of `blocks` blocks. */
    explicit SparseVolume(int blocks = 0);

    /* The serial form back into a volume; nullopt when `bytes` is not
     * one, with the reason in `error` when that is given. */
    [[nodiscard]] static std::optional<SparseVolume>
    parse(std::span<const uint8_t> bytes, std::string *error = nullptr);

    [[nodiscard]] std::vector<uint8_t> serialize() const;

    /* A volume from its blocks laid end to end (block n at n*512), and
     * back.  Blocks of zeros are not held. */
    [[nodiscard]] static SparseVolume fromLinear(std::span<const uint8_t> linear);
    [[nodiscard]] std::vector<uint8_t> toLinear() const;

    [[nodiscard]] int blocks() const noexcept { return blocks_; }

    /* Change the size.  Growing costs nothing; shrinking drops the
     * blocks held past the new end. */
    void resize(int blocks);

    /* The number of blocks held. */
    [[nodiscard]] std::size_t held() const noexcept { return held_.size(); }
    [[nodiscard]] bool holds(int lbn) const { return held_.count(lbn) != 0; }

    /* 512 bytes of block `lbn`: zeros for a block not held or past the
     * volume's end. */
    void readBlock(int lbn, uint8_t *out) const;

    /* Write block `lbn`.  A block of zeros is let go rather than held;
     * a block past the volume's end is not written. */
    void writeBlock(int lbn, const uint8_t *in);

    /* Let block `lbn` go: it reads as zeros again. */
    void clearBlock(int lbn);

private:
    using Block = std::array<uint8_t, 512>;

    int                  blocks_ = 0;
    std::map<int, Block> held_;
};

} /* namespace ms0515::disk */

#endif /* MS0515_DISK_SPARSEVOLUME_HPP */
