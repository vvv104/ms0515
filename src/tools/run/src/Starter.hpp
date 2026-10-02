/*
 * Starter.hpp - the program the saved state is caught in, and the way a
 * command gets to the monitor through it.
 *
 * ms0515-run does not load a program itself: KMON's RUN does more than
 * read the file (the load bitmap in the job header, an overlaid program's
 * channel left open on its file, the rest of the command line handed to
 * the program), and all of it stays the monitor's.  What is skipped is the
 * typing.  RT-11 lets a program exit with command lines for KMON in the
 * chain area (the byte count at 510, the lines from 512, SPXIT$ in the
 * JSW, R0 = 0); KMON runs them as it would an indirect file, without
 * echoing them under SET TT QUIET.
 *
 * So the state is saved with a small program running: START.SAV, on the
 * system diskette.  It clears the screen through the console and waits
 * for a word in its own memory to become non-zero.  handCommands() writes
 * the command into the chain area and sets that word; the starter exits,
 * and the monitor does the rest.
 */

#ifndef MS0515_RUN_STARTER_HPP
#define MS0515_RUN_STARTER_HPP

#include <ms0515/Emulator.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ms0515::run {

/* The starter's name on the system diskette. */
inline constexpr const char *kStarterFile = "START.SAV";

/* KMON's command buffer takes a line of this many characters. */
inline constexpr std::size_t kMaxCommand = 80;

/* Where the resident monitor is: the word at 54 points at it. */
inline constexpr uint16_t kRmonPointer = 054;

/* KMONIN's place in the dec monitor (RMON + this): found in the running
 * system and held by the tests - a monitor built otherwise fails them. */
inline constexpr uint16_t kKmoninOffset = 0450;

/* START.SAV: the job header and the code, two blocks. */
[[nodiscard]] std::vector<uint8_t> starterProgram();

/* The machine is in the starter's wait and no command has been handed. */
[[nodiscard]] bool starterWaiting(ms0515::Emulator &emu);

/* Hand `lines` (KMON commands, no line ends) to the waiting starter; the
 * monitor runs them in order, as the lines of an indirect file.  False
 * when the starter is not waiting, a line is longer than KMON takes, or
 * they do not fit the chain area together. */
[[nodiscard]] bool handCommands(ms0515::Emulator &emu,
                                std::span<const std::string> lines);

/* The monitor's own word for who runs: true while KMON has control,
 * false while a program does (KMONIN, RMONSJ.MAC).  It turns true again
 * however the program ends - .EXIT, an error the monitor stops it for,
 * two CTRL/C. */
[[nodiscard]] bool monitorInControl(ms0515::Emulator &emu);

} /* namespace ms0515::run */

#endif /* MS0515_RUN_STARTER_HPP */
