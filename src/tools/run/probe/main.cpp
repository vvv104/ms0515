/*
 * ms0515-run-probe - what happens to a program under ms0515-run, told
 * without a terminal or a window: a tool for going through a library of
 * programs, not a shipped one.
 *
 *     ms0515-run-probe [--em] [--frames N] [--type TEXT]... [--png FILE]
 *                      PROGRAM[.SAV] [the program's command line]
 *
 * The program is run as ms0515-run runs it, for N frames at most (1500,
 * half a minute of the machine's time).  Each --type is typed when the
 * program takes keys, in order; `\r` in it is the Return key.  The report
 * says how it ended or what it was doing when the frames ran out, what
 * kind of program it was taken for, which files it asked the monitor
 * for, and what it printed; --png keeps the screen.
 */

#include "ConsoleText.hpp"
#include "Machine.hpp"

#include <ms0515/Typist.hpp>
#include <ms0515/app/Screen.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace {

struct Options {
    int                      frames = 1500;
    std::vector<std::string> typed;
    std::string              png;
    std::string              program;
    std::vector<std::string> arguments;
    ms0515::run::Machine::Options machine;
};

/* `\r` and `\n` written out become the characters. */
std::string unescape(const std::string &text)
{
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size() &&
            (text[i + 1] == 'r' || text[i + 1] == 'n')) {
            out += '\r';
            ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool parse(int argc, char **argv, Options &o)
{
    int i = 1;
    for (; i + 1 < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag == "--em") {       /* ms0515-run's own switch: no value */
            o.machine.instructionEmulator = true;
            --i;
            continue;
        }
        if (flag == "--frames")     o.frames = std::atoi(argv[i + 1]);
        else if (flag == "--type")  o.typed.push_back(unescape(argv[i + 1]));
        else if (flag == "--png")   o.png = argv[i + 1];
        else break;
    }
    if (i >= argc || o.frames <= 0)
        return false;
    o.program = argv[i];
    o.arguments.assign(argv + i + 1, argv + argc);
    return true;
}

int probe(const Options &o)
{
    using ms0515::run::ConsoleText;

    ms0515::run::Machine machine;
    std::string asked;
    machine.setAsked([&asked](const std::string &device, const std::string &name,
                              bool given) {
        asked += "  " + (device.empty() ? std::string{"(none)"} : device) + ":" +
                 name + (given ? "  <- given from the folder" : "") + "\n";
    });
    if (auto r = machine.start(o.program, o.arguments, o.machine); !r) {
        std::printf("could not start: %s\n", r.error().c_str());
        return 2;
    }

    ConsoleText text(ConsoleText::Reader::plain);
    ms0515::Typist typist;
    std::size_t next = 0;
    std::string printed;
    int frame = 0;
    int interactiveAt = -1, graphicsAt = -1;
    for (; frame < o.frames; ++frame) {
        if (machine.takesKeys()) {
            if (typist.pending() == 0 && next < o.typed.size())
                for (char c : o.typed[next++]) typist.type(static_cast<uint8_t>(c));
            typist.pump(machine.emulator());
        }
        if (!machine.step()) break;
        printed += text.convert(machine.takeOutput());
        if (machine.interactive() && interactiveAt < 0) interactiveAt = frame;
        if (machine.graphics() && graphicsAt < 0) graphicsAt = frame;
    }
    if (machine.ended())
        printed += text.convert(machine.drainOutput());

    std::printf("frames:       %d of %d\n", frame, o.frames);
    std::printf("ended:        %s\n", !machine.ended()  ? "no, still running"
                                    : machine.failed() ? "yes, FAILED" : "yes, well");
    std::printf("interactive:  %s\n", interactiveAt < 0 ? "no"
                : ("from frame " + std::to_string(interactiveAt)).c_str());
    std::printf("graphics:     %s\n", graphicsAt < 0 ? "no"
                : ("from frame " + std::to_string(graphicsAt)).c_str());
    if (!machine.ended())
        std::printf("waiting key:  %s\n", machine.waitingForKey() ? "yes" : "no");
    std::printf("typed:        %zu of %zu\n", next, o.typed.size());
    std::printf("files asked:\n%s", asked.empty() ? "  none\n" : asked.c_str());
    std::printf("printed (%zu bytes):\n%s\n", printed.size(), printed.c_str());

    if (!o.png.empty()) {
        ms0515::app::Screen screen;
        screen.render(machine.emulator(), 0);
        if (ms0515::app::saveScreenshot(screen, o.png).empty())
            std::printf("could not write %s\n", o.png.c_str());
    }
    return 0;
}

} /* namespace */

int main(int argc, char **argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        std::fprintf(stderr, "usage: ms0515-run-probe [--frames N] [--type TEXT]... "
                             "[--png FILE] PROGRAM[.SAV] [command line]\n");
        return 2;
    }
    try {
        return probe(options);
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ms0515-run-probe: %s\n", e.what());
        return 2;
    }
}
