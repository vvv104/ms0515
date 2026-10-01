/*
 * ms0515-run - run one RT-11 program on the MS 0515.
 *
 *     ms0515-run PROGRAM.SAV [the program's command line]
 *
 * The program's folder is DK:; what follows the program on the line is
 * the program's, as after RUN at the monitor's prompt.  What it prints
 * goes to stdout - as text alone when stdout is a file or a pipe.  The
 * exit status is 0 when the program ended well by the monitor's account,
 * 1 when it did not, 2 when it could not be started.
 *
 * There are no switches and no settings: everything the machine needs is
 * compiled in (Embedded.hpp).
 */

#include "ConsoleText.hpp"
#include "Machine.hpp"

#include <Platform.hpp>

#include <cstdio>
#include <exception>
#include <string>
#include <vector>

namespace {

void print(const std::string &text)
{
    if (text.empty()) return;
    ms0515::cli::writeStdout(text.data(), text.size());
    ms0515::cli::flushStdout();
}

int run(const std::string &program, const std::vector<std::string> &arguments)
{
    using ms0515::run::ConsoleText;

    ms0515::run::Machine machine;
    if (auto r = machine.start(program, arguments); !r) {
        std::fprintf(stderr, "ms0515-run: %s\n", r.error().c_str());
        return 2;
    }

    ms0515::cli::enableUtf8Output();
    ConsoleText text(ms0515::cli::stdoutIsTerminal()
                         ? ConsoleText::Reader::terminal
                         : ConsoleText::Reader::plain);
    while (machine.step())
        print(text.convert(machine.takeOutput()));
    print(text.convert(machine.drainOutput()));
    return machine.failed() ? 1 : 0;
}

} /* namespace */

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: ms0515-run PROGRAM.SAV [the program's command line]\n");
        return 2;
    }
    try {
        return run(argv[1], {argv + 2, argv + argc});
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ms0515-run: %s\n", e.what());
        return 2;
    }
}
