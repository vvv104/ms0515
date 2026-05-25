/*
 * Disks.hpp — disk-image discovery, validation, and mount.
 *
 * Shared by ms0515.exe and ms0515-cli.exe: both run the same checks
 * (size matches a single- or double-sided image), the same per-drive
 * mount sequence (DS exclusive with side-N), and the same default-ROM
 * search.  Keeping this in libapp lets both binaries change in lockstep
 * when a new image format or mount policy lands.
 */
#ifndef MS0515_APP_DISKS_HPP
#define MS0515_APP_DISKS_HPP

#include <optional>
#include <string>
#include <vector>

namespace ms0515 { class Emulator; }

namespace ms0515::app {

struct CliArgs;

/* Validate `path` as a single-side disk image (FDC_DISK_SIZE bytes).
 * Returns std::nullopt on success, or an error string suitable for
 * fprintf(stderr, ...) describing what's wrong (file missing, wrong
 * size, double-sided when we wanted single, …). */
[[nodiscard]] std::optional<std::string>
validateSingleSideImage(const std::string &path);

[[nodiscard]] std::optional<std::string>
validateDoubleSidedImage(const std::string &path);

/* Enumerate all *.rom files under <searchRoot>/assets/rom for every
 * search root.  Sorted, deduplicated, normalised paths. */
[[nodiscard]] std::vector<std::string> discoverRoms();

/* Mount the disks described by `cli` onto `emu`.  For each drive the
 * policy is:
 *   - dsPath set → mount as double-sided on units (drive, drive+2).
 *   - dsPath empty → mount each non-empty fdPath as single-side on its
 *     own unit.
 *   - both set → error message + skip the drive (the CLI parser keeps
 *     them mutually exclusive, but a hand-edited YAML can still trip
 *     this).
 *
 * Validation errors are written to stderr; the drive is skipped, but
 * the function still returns true overall.  Returns false only if a
 * mount that PASSED validation still failed at the FDC level — that's
 * a fatal hardware-state problem the caller should surface. */
[[nodiscard]] bool mountDisksFromCli(ms0515::Emulator &emu,
                                     const CliArgs   &cli);

} /* namespace ms0515::app */

#endif /* MS0515_APP_DISKS_HPP */
