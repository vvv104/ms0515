/*
 * ComposeCommand.hpp - `ms0515-disk compose`: diskettes made from a local
 * copy of the software collection and its disks.toml.
 */

#ifndef MS0515_TOOLS_DISK_COMPOSE_COMMAND_HPP
#define MS0515_TOOLS_DISK_COMPOSE_COMMAND_HPP

namespace ms0515::tools {

/* argv as main() has it, argv[1] == "compose". */
int composeCommand(int argc, char **argv);

} /* namespace ms0515::tools */

#endif
