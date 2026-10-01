/*
 * Machine.cpp - see Machine.hpp.
 */

#include "Machine.hpp"

#include "Embedded.hpp"
#include "MonitorWatch.hpp"
#include "Starter.hpp"
#include "ZeroRun.hpp"

#include <ms0515/disk/Rtfs.hpp>

#include <cctype>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ms0515::run {

namespace {

std::vector<uint8_t> carried(std::span<const uint8_t> packed, const char *what)
{
    auto data = unpackZeroRuns(packed);
    if (!data)
        throw std::runtime_error(std::string{"the carried "} + what + " is damaged");
    return std::move(*data);
}

/* The folder as RT-11 will see it, every file under the name the volume
 * gives it; `rt11Name` receives the program's. */
disk::RtfsDescriptor describeFolder(const fs::path &folder,
                                    const std::string &programFile,
                                    std::string &rt11Name)
{
    disk::RtfsDescriptor desc;
    desc.device = disk::RtfsDescriptor::Device::Hd;
    desc.blocks = disk::kRtfsMaxBlocks;

    std::vector<disk::RtfsHostFile> listing;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(folder, ec)) {
        if (!de.is_regular_file(ec)) continue;
        if (de.path().extension() == disk::kRtfsExtension) continue;
        listing.push_back({de.path().filename().string(), de.file_size(ec), 0});
    }
    disk::autoFillRtfs(desc, listing);
    for (const auto &f : desc.files)
        if (f.hostName == programFile) rt11Name = f.rt11Name;
    return desc;
}

} /* namespace */

Status runCommand(std::string &command, const std::string &rt11Name,
                  std::span<const std::string> arguments)
{
    command = "RUN DK:" + rt11Name;
    for (const auto &arg : arguments) {
        command += ' ';
        for (char c : arg) {
            const auto u = static_cast<unsigned char>(c);
            if (u < 0x20 || u > 0x7E)
                return Status{"the command line takes ASCII only: " + arg};
            command += static_cast<char>(std::toupper(u));
        }
    }
    if (command.size() > kMaxCommand)
        return Status{"the command line is longer than the monitor takes (" +
                      std::to_string(kMaxCommand) + " characters with the RUN)"};
    return {};
}

Machine::Machine()
{
    emu_.loadRom(carried(embedded::rom, "ROM"));
    /* The state does not hold the hard disk controller: it was on the bus
     * when the monitor loaded HD, with no media in it. */
    emu_.setHdEnabled(true);
    if (auto r = emu_.loadState(carried(embedded::state, "state")); !r)
        throw std::runtime_error("the carried state does not load: " + r.error());
    if (!emu_.mountDiskImage(0, carried(embedded::disk, "system diskette")))
        throw std::runtime_error("the carried system diskette does not mount");
    if (!starterWaiting(emu_))
        throw std::runtime_error("the carried state is not the starter's wait");
}

Status Machine::start(const fs::path &program,
                      std::span<const std::string> arguments)
{
    std::error_code ec;
    if (!fs::is_regular_file(program, ec))
        return Status{"no such program: " + program.string()};
    const fs::path folder = fs::absolute(program, ec).parent_path();

    std::string rt11Name;
    auto desc = describeFolder(folder, program.filename().string(), rt11Name);
    if (rt11Name.empty())
        return Status{"the program does not fit an RT-11 volume: " +
                      program.string()};

    std::string command;
    if (auto r = runCommand(command, rt11Name, arguments); !r)
        return r;
    if (!emu_.mountHdInMemory(folder.string(), std::move(desc)))
        return Status{"cannot serve the folder " + folder.string()};
    installMonitorWatch(emu_);
    if (!handCommand(emu_, command))
        return Status{"the starter does not take the command"};
    return {};
}

bool Machine::step()
{
    if (ended())
        return false;
    (void)emu_.stepFrame();
    return !ended();
}

bool Machine::ended() const noexcept
{
    return monitorPrompted();
}

std::string Machine::takeOutput()
{
    return takeConsoleOutput();
}

std::string Machine::drainOutput()
{
    constexpr int kQuietFrames = 5, kMostFrames = 500;
    std::string rest = takeConsoleOutput();
    int quiet = 0;
    for (int f = 0; f < kMostFrames && quiet < kQuietFrames; ++f) {
        (void)emu_.stepFrame();
        const std::string more = takeConsoleOutput();
        quiet = more.empty() ? quiet + 1 : 0;
        rest += more;
    }
    return rest;
}

bool Machine::failed() const noexcept
{
    return (endSeverity() &
            (kSeverityError | kSeverityFatal | kSeverityUnconditional)) != 0;
}

} /* namespace ms0515::run */
