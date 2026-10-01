/*
 * Machine.hpp - the machine ms0515-run runs a program on.
 *
 * Built from what the binary carries (Embedded.hpp): ROM-B, the dec
 * system diskette in drive 0 and the state with the starter waiting.
 * start() mounts the program's folder as the hard disk - DK: in the saved
 * state - and hands KMON `RUN DK:PROGRAM arguments`; step() runs a frame
 * and tells whether the program is still there.  One machine per process:
 * the watch for the program's end is global (MonitorWatch.hpp).
 */

#ifndef MS0515_RUN_MACHINE_HPP
#define MS0515_RUN_MACHINE_HPP

#include <ms0515/Emulator.hpp>
#include <ms0515/Status.hpp>

#include <filesystem>
#include <span>
#include <string>

namespace ms0515::run {

class Machine {
public:
    /* Throws std::runtime_error when the carried data does not make a
     * machine (a build fault, not a user's). */
    Machine();
    ~Machine();

    Machine(const Machine &)            = delete;
    Machine &operator=(const Machine &) = delete;

    [[nodiscard]] ms0515::Emulator &emulator() noexcept { return emu_; }

    /* DK: becomes a volume over the program's folder and the monitor is
     * told to run the program with `arguments` as its command line
     * (joined by spaces, in upper case as RT-11 reads them).  The volume
     * starts with the program and the files the command line speaks of:
     * a name with its files of every extension (PRIMER brings PRIMER.MAC
     * and an earlier PRIMER.OBJ), a wildcard with what it matches.  The
     * rest of the folder is not there - it may be more than a volume
     * takes - but a file the program asks the monitor for by name is
     * given to it, on DK: or on SY:.  Files the program creates appear
     * in the folder. */
    [[nodiscard]] Status start(const std::filesystem::path &program,
                               std::span<const std::string> arguments);

    /* Run one frame.  False once the program is over: the monitor has
     * come back to its prompt (MonitorWatch.hpp), however that came. */
    bool step();

    [[nodiscard]] bool ended() const noexcept;

    /* What the console was given since the last call: the program's
     * output as it printed it (KOI-8 bytes; see ConsoleText.hpp). */
    [[nodiscard]] std::string takeOutput();

    /* The output still on its way when the program ended: the monitor
     * prints from a ring, a character at a time.  Runs the machine until
     * the console has been quiet for a few frames and returns what came. */
    [[nodiscard]] std::string drainOutput();

    /* The program did not end well by the monitor's own account: it
     * reported an error, the monitor stopped it, or RUN refused it.
     * Meaningful once ended(). */
    [[nodiscard]] bool failed() const noexcept;

private:
    void fileAsked(const std::string &device, const std::string &name);

    ms0515::Emulator      emu_;
    std::filesystem::path folder_;      /* the program's */
};

/* The RUN command for a program file and its arguments, or an error
 * saying what RT-11 cannot take.  `rt11Name` is the file's name on DK:. */
[[nodiscard]] Status runCommand(std::string &command,
                                const std::string &rt11Name,
                                std::span<const std::string> arguments);

} /* namespace ms0515::run */

#endif /* MS0515_RUN_MACHINE_HPP */
