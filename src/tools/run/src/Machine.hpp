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

#include <cstdint>
#include <filesystem>
#include <functional>
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

    /* What of the system is switched on for the program. */
    struct Options {
        /* EM, the emulator of the instructions the processor lacks (MUL,
         * DIV, ASH, ASHC, the FIS four), for a program built for a PDP-11
         * that has them.  It is on the system diskette; resident it takes
         * about 1.3 KB of the program's memory, so it is not on unasked. */
        bool instructionEmulator = false;
    };

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
                               std::span<const std::string> arguments,
                               Options options = {});

    /* Run one frame.  False once the program is over: the monitor has
     * come back to its prompt (MonitorWatch.hpp), however that came. */
    bool step();

    [[nodiscard]] bool ended() const noexcept;

    /* The program is one a person sits at, and wants the machine's own
     * pace: it has waited for a key - asked the monitor for one, called
     * the ROM's key input itself, or taken the keyboard's interrupt - or
     * drawn on the screen itself, or left the console's video mode, or
     * sounded.  Until then it is taken
     * for one that does its work and ends - an assembler, a linker - and
     * may run as fast as the host does.  Once true, it stays. */
    [[nodiscard]] bool interactive() const noexcept { return interactive_; }

    /* The program makes its picture itself: it has put bytes into video
     * memory past the console, or left the console's video mode.  Its
     * screen is to be shown as a picture from now on, not printed as
     * text.  Once true, it stays. */
    [[nodiscard]] bool graphics() const noexcept { return graphics_; }

    /* Where the speaker's level changes go: the cycle of the frame and
     * the new level.  Empty: nowhere. */
    using Speaker = std::function<void(uint32_t cycle, int level)>;
    void setSpeaker(Speaker speaker) { speaker_ = std::move(speaker); }

    /* The program is asking for a key and has been for some frames. */
    [[nodiscard]] bool waitingForKey() const noexcept
    { return keyFrames_ >= kKeyFrames; }

    /* The moment to type: the program asks the monitor for keys and has
     * done so lately, or it is one that never asks and reads the keyboard
     * its own way (a game).  A program at its work between two questions
     * is not typed at - what is typed ahead waits, so that its echo comes
     * after the question it answers; one not yet interactive is not typed
     * at either. */
    [[nodiscard]] bool takesKeys() const noexcept
    { return interactive_ && (!everAsked_ || sinceAsked_ < kTypeAheadFrames); }

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

    /* Told of every file a program asks the monitor for by name: the
     * device and the name as asked, and whether the file was given from
     * the folder just then (false also when it was there already).  For
     * finding out what a program needs beside it. */
    using Asked = std::function<void(const std::string &device,
                                     const std::string &name, bool given)>;
    void setAsked(Asked asked) { asked_ = std::move(asked); }

private:
    void fileAsked(const std::string &device, const std::string &name);
    [[nodiscard]] bool giveFile(const std::string &device, const std::string &name);

    /* A line handed over by RUN is read in a frame or two; a program
     * that asks for a key for this many frames on end is waiting. */
    static constexpr int kKeyFrames = 3;
    /* A program that polls for keys does not ask every frame: it still
     * takes them this many frames after it last asked. */
    static constexpr int kTypeAheadFrames = 25;

    ms0515::Emulator      emu_;
    std::filesystem::path folder_;      /* the program's */
    uint16_t              rmon_ = 0;    /* where the resident monitor starts */
    int                   keyFrames_ = 0;   /* frames on end with a key asked */
    int                   sinceAsked_ = 0;  /* frames since one last was     */
    bool                  everAsked_ = false;
    bool                  drewItself_ = false;
    bool                  sounded_ = false;
    bool                  interactive_ = false;
    bool                  graphics_ = false;
    Speaker               speaker_;
    Asked                 asked_;
};

/* The RUN command for a program file and its arguments, or an error
 * saying what RT-11 cannot take.  `rt11Name` is the file's name on DK:. */
[[nodiscard]] Status runCommand(std::string &command,
                                const std::string &rt11Name,
                                std::span<const std::string> arguments);

} /* namespace ms0515::run */

#endif /* MS0515_RUN_MACHINE_HPP */
