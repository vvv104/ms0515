/*
 * Programs.hpp - the example programs under test.
 *
 * A .SAV of ../examples' build run the way `ms0515-run NAME` runs it - the
 * machine of ms0515-run, without a terminal or a window - to its end, and
 * what it printed brought back as the host's text.  The folder is the
 * examples' build/sav unless --gcc-sav=<folder> names another; a suite
 * skips itself when the program is not there.
 */
#pragma once

#include <doctest/doctest.h>

#include "ConsoleText.hpp"
#include "Machine.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gcc_tests {

namespace fs = std::filesystem;

inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> opts;
    return opts;
}

inline fs::path savDir()
{
    auto it = options().find("sav");
    return it == options().end() || it->second.empty() ? fs::path(GCC_SAV_DIR) : fs::path(it->second);
}

/* Whether NAME.SAV is there.  A folder named on the command line is
 * expected to hold the programs: a missing one fails the test then (CI),
 * where the default folder's absence only skips it. */
inline bool built(const char *name)
{
    const bool there = fs::exists(savDir() / (std::string(name) + ".SAV"));
    if (!there && options().count("sav")) {
        const std::string missing = std::string(name) + ".SAV is not in " + savDir().string();
        FAIL(missing);
    }
    return there;
}

/* What a program printed, and whether the monitor took its end well. */
struct Run {
    std::string printed;
    bool ended = false;
    bool failed = true;
};

/* NAME.SAV run from a copy of its own folder to its end: at most `frames`
 * frames of the machine (CALC takes some 1500). */
inline Run run(const char *name, int frames = 20000)
{
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() / ("ms0515_gcc_" + std::to_string(counter++));
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string file = std::string(name) + ".SAV";
    fs::copy_file(savDir() / file, dir / file);

    Run result;
    ms0515::run::Machine machine;
    if (!machine.start(dir / file, {}))
        return result;
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    while (frames-- > 0 && machine.step())
        result.printed += text.convert(machine.takeOutput());
    result.ended = machine.ended();
    if (result.ended) {
        result.printed += text.convert(machine.drainOutput());
        result.failed = machine.failed();
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    return result;
}

inline std::vector<std::string> lines(const std::string &text)
{
    std::vector<std::string> out;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            out.push_back(line);
            line.clear();
        } else {
            line += c;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

}  // namespace gcc_tests
