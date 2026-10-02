/*
 * ms0515-run - run one RT-11 program on the MS 0515.
 *
 *     ms0515-run [switches] PROGRAM[.SAV] [the program's command line]
 *
 * What stands before the program is the tool's - its switches, each
 * beginning with a dash:
 *
 *     --em    switch on EM, the emulator of the instructions the
 *             processor lacks (MUL, DIV, ASH, ASHC, the FIS four), for a
 *             program built for a PDP-11 that has them; it takes about
 *             1.3 KB of the program's memory
 *
 * What follows the program on the line is the program's, as after RUN at
 * the monitor's prompt.  DK: holds the program and the files that line
 * speaks of, taken from the program's folder; a file the program asks
 * for by name comes from there too.  What the program writes lands in
 * the folder.  What it prints goes to stdout - as text alone when stdout
 * is a file or a pipe - and what is typed on stdin is typed on the
 * machine's keyboard.  Ctrl-C is the machine's (two of them stop a
 * program, as on RT-11); Ctrl-] leaves at once.
 *
 * A program that makes its picture itself - a game - gets a window the
 * moment it starts drawing: the screen is shown there, the keys come
 * from there, and closing it ends the run.  The speaker sounds in either
 * case.
 *
 * A program that does its work and ends runs as fast as the host does;
 * one a person sits at runs at the machine's own pace (Machine.hpp says
 * how the two are told apart).
 *
 * The exit status is 0 when the program ended well by the monitor's
 * account, 1 when it did not, 2 when it could not be started.
 *
 * There are no settings and no files of the tool's own: everything the
 * machine needs is compiled in (Embedded.hpp).
 */

#include "ConsoleText.hpp"
#include "Display.hpp"
#include "HostKeys.hpp"
#include "Machine.hpp"

#include <ms0515/Typist.hpp>

#include <Platform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

/* The machine's frame: 50 a second. */
constexpr std::chrono::milliseconds kFrame{20};

/* Stdin has ended and the program still asks for a key: after this many
 * frames it gets the two Ctrl-C that end a program on RT-11. */
constexpr int kFramesToInterrupt = 25;

void print(const std::string &text)
{
    if (text.empty()) return;
    ms0515::cli::writeStdout(text.data(), text.size());
    ms0515::cli::flushStdout();
}

/* The terminal as the machine's keyboard for the length of the run. */
struct Keyboard {
    Keyboard()
    {
        ms0515::cli::installInterruptHandler();
        ms0515::cli::setTerminalRawMode();
    }
    ~Keyboard() { ms0515::cli::restoreTerminal(); }
    Keyboard(const Keyboard &)            = delete;
    Keyboard &operator=(const Keyboard &) = delete;

    /* Before each frame: what was typed since the last one, a key at a
     * time.  False when the host asked to leave. */
    bool pump(ms0515::run::Machine &machine)
    {
        std::array<uint8_t, 256> bytes{};
        const std::size_t n =
            ms0515::cli::readStdinNonBlocking(bytes.data(), bytes.size());
        if (keys.feed({bytes.data(), n}, typist))
            return false;

        const bool starved = ms0515::cli::isStdinEof() && typist.pending() == 0 &&
                             machine.waitingForKey();
        starvedFrames = starved ? starvedFrames + 1 : 0;
        if (starvedFrames == kFramesToInterrupt) {
            typist.type(uint8_t{3});
            typist.type(uint8_t{3});
        }
        if (machine.takesKeys())
            typist.pump(machine.emulator());
        return !ms0515::cli::shouldQuit();
    }

    ms0515::run::HostKeys keys;
    ms0515::Typist        typist;
    int                   starvedFrames = 0;
};

int run(const std::string &program, const std::vector<std::string> &arguments,
        ms0515::run::Machine::Options options)
{
    using ms0515::run::ConsoleText;

    ms0515::run::Machine machine;
    if (auto r = machine.start(program, arguments, options); !r) {
        std::fprintf(stderr, "ms0515-run: %s\n", r.error().c_str());
        return 2;
    }

    ms0515::cli::enableUtf8Output();
    ConsoleText text(ms0515::cli::stdoutIsTerminal()
                         ? ConsoleText::Reader::terminal
                         : ConsoleText::Reader::plain);
    ms0515::run::Display display;
    machine.setSpeaker([&display](uint32_t cycle, int level) {
        display.speaker(cycle, level);
    });
    std::optional<Keyboard> keyboard{std::in_place};

    auto next = Clock::now();
    while (true) {
        /* The terminal is the keyboard until there is a window. */
        if (keyboard && !keyboard->pump(machine))
            return 1;                   /* left by Ctrl-] */
        if (!machine.step())
            break;

        if (machine.graphics() && !display.isOpen()) {
            /* The program draws: from here on its screen is a picture. */
            keyboard.reset();
            if (!display.open(std::filesystem::path{program}.stem().string())) {
                std::fprintf(stderr, "ms0515-run: %s\n", display.error().c_str());
                return 2;
            }
            ms0515::cli::releaseOwnConsole();
        }
        const std::string printed = machine.takeOutput();
        if (!display.isOpen())
            print(text.convert(printed));
        if (!display.frame(machine.emulator()))
            return 0;                   /* the window was closed */

        if (!machine.interactive()) {
            next = Clock::now();
            continue;
        }
        /* The machine's pace; a frame that came late is not made up for. */
        next = std::max(next + kFrame, Clock::now());
        std::this_thread::sleep_until(next);
    }
    if (!display.isOpen())
        print(text.convert(machine.drainOutput()));
    return machine.failed() ? 1 : 0;
}

} /* namespace */

constexpr const char *kUsage =
    "usage: ms0515-run [switches] PROGRAM[.SAV] [the program's command line]\n"
    "  --em    switch on the emulator of the EIS/FIS instructions\n";

int main(int argc, char **argv)
{
    /* The tool's switches come before the program; from the program on,
     * the line is the program's. */
    ms0515::run::Machine::Options options;
    int at = 1;
    for (; at < argc && argv[at][0] == '-'; ++at) {
        const std::string flag = argv[at];
        if (flag == "--em") {
            options.instructionEmulator = true;
        } else {
            std::fprintf(stderr, "ms0515-run: no such switch: %s\n%s",
                         flag.c_str(), kUsage);
            return 2;
        }
    }
    if (at >= argc) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    try {
        return run(argv[at], {argv + at + 1, argv + argc}, options);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ms0515-run: %s\n", e.what());
        return 2;
    }
}
