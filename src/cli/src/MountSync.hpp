/*
 * MountSync.hpp — the commander's slots applied to the running machine.
 *
 * The commander keeps what is mounted where as `files::Mounts`; the
 * machine keeps it as four floppy units and the HD.  applyMounts brings
 * the machine in line: a unit whose image changed is ejected and the new
 * one inserted, a unit that lost its image is ejected, the HD follows
 * its slot and its controller comes on with the first image.
 */
#ifndef MS0515_CLI_MOUNT_SYNC_HPP
#define MS0515_CLI_MOUNT_SYNC_HPP

#include "Mounts.hpp"

#include <ms0515/Emulator.hpp>

#include <string>
#include <vector>

namespace ms0515::cli {

/* Returns what could not be mounted, one line each; empty when all is well. */
[[nodiscard]] std::vector<std::string> applyMounts(Emulator &emu, const files::Mounts &mounts);

} /* namespace ms0515::cli */

#endif /* MS0515_CLI_MOUNT_SYNC_HPP */
