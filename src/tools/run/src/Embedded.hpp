/*
 * Embedded.hpp - what ms0515-run carries inside instead of files: the
 * ROM, the dec system diskette and the state saved with the starter
 * waiting.  The diskette is a sparse volume in its serial form
 * (ms0515/disk/SparseVolume.hpp): the blocks its files use, not the
 * medium's image.  The arrays are written by ms0515-run-bake at build
 * time, each packed as ZeroRun.hpp describes; the three were made
 * together and are of one piece (the state checks the ROM, the monitor
 * in it knows where its files lie on the diskette).
 */

#ifndef MS0515_RUN_EMBEDDED_HPP
#define MS0515_RUN_EMBEDDED_HPP

#include <cstdint>
#include <span>

namespace ms0515::run::embedded {

extern const std::span<const std::uint8_t> rom;
extern const std::span<const std::uint8_t> disk;
extern const std::span<const std::uint8_t> state;

} /* namespace ms0515::run::embedded */

#endif /* MS0515_RUN_EMBEDDED_HPP */
