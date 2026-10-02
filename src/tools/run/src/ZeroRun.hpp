/*
 * ZeroRun.hpp - the packing of the data compiled into ms0515-run.
 *
 * The ROM, the system diskette and the saved state are mostly zeros (a
 * diskette with five files, a memory with a monitor at its top and a
 * cleared screen), so only the stretches that hold something are kept:
 *
 *   u32  size of the whole               (little-endian, as all below)
 *   then, until the packed data ends:
 *   u32  offset of a stretch
 *   u32  its length
 *   ...  its bytes
 *
 * Stretches come in rising order and do not overlap; what lies between
 * them is zero.
 */

#ifndef MS0515_RUN_ZERORUN_HPP
#define MS0515_RUN_ZERORUN_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ms0515::run {

[[nodiscard]] std::vector<uint8_t> packZeroRuns(std::span<const uint8_t> data);

/* nullopt when `packed` is not what packZeroRuns writes. */
[[nodiscard]] std::optional<std::vector<uint8_t>>
unpackZeroRuns(std::span<const uint8_t> packed);

} /* namespace ms0515::run */

#endif /* MS0515_RUN_ZERORUN_HPP */
