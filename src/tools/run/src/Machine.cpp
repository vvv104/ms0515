/*
 * Machine.cpp - see Machine.hpp.
 */

#include "Machine.hpp"

#include "Embedded.hpp"
#include "MonitorWatch.hpp"
#include "Starter.hpp"
#include "ZeroRun.hpp"

#include <ms0515/disk/Build.hpp>
#include <ms0515/disk/Image.hpp>
#include <ms0515/disk/Rtfs.hpp>

#include <cctype>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ms0515::run {

namespace {

/* BLKEY (RMON + this): the directory segment the monitor holds in memory;
 * 0 is none, and the next lookup reads the directory from the volume. */
constexpr uint16_t kDirectoryInMemory = 0256;

std::vector<uint8_t> carried(std::span<const uint8_t> packed, const char *what)
{
    auto data = unpackZeroRuns(packed);
    if (!data)
        throw std::runtime_error(std::string{"the carried "} + what + " is damaged");
    return std::move(*data);
}

std::string lower(std::string_view s)
{
    std::string out{s};
    for (auto &c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

/* A file specification of a command line: name and extension in lower
 * case, the device dropped; no extension given is an empty one. */
struct Spoken {
    std::string name, extension;
};

/* The file specifications a command line holds: the line is cut at the
 * characters RT-11's command syntax separates them with, and an option
 * (/X, /X:n) or a size ([n]) names no file. */
std::vector<Spoken> spokenOf(std::span<const std::string> arguments)
{
    std::vector<Spoken> spoken;
    for (const auto &argument : arguments) {
        std::size_t at = 0;
        while (at < argument.size()) {
            const std::size_t end = argument.find_first_of("=,<>[/ ", at);
            std::string token = lower(argument.substr(at, end - at));
            if (const auto colon = token.rfind(':'); colon != std::string::npos)
                token.erase(0, colon + 1);
            const auto dot = token.find('.');
            if (!token.empty() && dot != 0)
                spoken.push_back({token.substr(0, dot),
                                  dot == std::string::npos ? std::string{}
                                                           : token.substr(dot + 1)});
            if (end == std::string::npos) break;
            at = end + 1;
            if (argument[end] == '/' || argument[end] == '[')
                at = argument.find_first_of("=,<> ", at);
        }
    }
    return spoken;
}

/* RT-11's wildcards: `*` for any run of characters, `%` for any one. */
bool matches(std::string_view pattern, std::string_view text)
{
    if (pattern.empty()) return text.empty();
    if (pattern.front() == '*')
        return matches(pattern.substr(1), text) ||
               (!text.empty() && matches(pattern, text.substr(1)));
    return !text.empty() &&
           (pattern.front() == '%' || pattern.front() == text.front()) &&
           matches(pattern.substr(1), text.substr(1));
}

/* A host file the command line speaks of.  A plain name brings its files
 * of every extension, since a program adds its own (MACRO reads
 * PRIMER.MAC for PRIMER and writes PRIMER.OBJ over an earlier one); a
 * name with a wildcard brings what the specification matches. */
bool spokenOfFile(const std::vector<Spoken> &spoken, const fs::path &file)
{
    const std::string name = lower(file.stem().string());
    std::string extension = lower(file.extension().string());
    if (!extension.empty()) extension.erase(0, 1);
    for (const auto &s : spoken) {
        const bool wild = s.name.find_first_of("*%") != std::string::npos;
        if (!wild ? s.name == name
                  : matches(s.name, name) &&
                    (s.extension.empty() || matches(s.extension, extension)))
            return true;
    }
    return false;
}

/* The volume RT-11 will see at the start: the program and the files its
 * command line speaks of.  The folder's other files stay out - a folder
 * may hold more than a volume takes - until the program asks for one by
 * name (Machine::fileAsked).  `rt11Name` receives the program's. */
disk::RtfsDescriptor describeVolume(const fs::path &program,
                                    std::span<const std::string> arguments,
                                    std::string &rt11Name)
{
    disk::RtfsDescriptor desc;
    desc.device = disk::RtfsDescriptor::Device::Hd;
    desc.blocks = disk::kRtfsMaxBlocks;

    const auto spoken = spokenOf(arguments);
    const std::string programFile = lower(program.filename().string());
    std::string programHost;

    std::vector<disk::RtfsHostFile> listing;
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(program.parent_path(), ec)) {
        if (!de.is_regular_file(ec)) continue;
        const std::string host = de.path().filename().string();
        /* The program as the folder spells it: the host may not tell
         * dir.sav from DIR.SAV, the volume's list does. */
        const bool isProgram = fs::equivalent(de.path(), program, ec) ||
                               (programHost.empty() && lower(host) == programFile);
        if (!isProgram && !spokenOfFile(spoken, de.path())) continue;
        if (isProgram) programHost = host;
        listing.push_back({host, de.file_size(ec), 0});
    }
    disk::autoFillRtfs(desc, listing);
    for (const auto &f : desc.files)
        if (f.hostName == programHost) rt11Name = f.rt11Name;
    return desc;
}

/* The folder's file RT-11 would call `rt11Name`; empty when none. */
fs::path folderFile(const fs::path &folder, const std::string &rt11Name)
{
    std::error_code ec;
    for (const auto &de : fs::directory_iterator(folder, ec))
        if (de.is_regular_file(ec) &&
            disk::mangleRt11Name(de.path().filename().string()) == rt11Name)
            return de.path();
    return {};
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
    /* A name without an extension is a .SAV, as it is for RUN. */
    fs::path found = program;
    if (!fs::is_regular_file(found, ec) && !program.has_extension())
        for (const char *extension : {".SAV", ".sav"}) {
            found = program;
            found += extension;
            if (fs::is_regular_file(found, ec)) break;
        }
    if (!fs::is_regular_file(found, ec))
        return Status{"no such program: " + program.string()};
    const fs::path absolute = fs::absolute(found, ec);
    const fs::path folder = absolute.parent_path();

    std::string rt11Name;
    auto desc = describeVolume(absolute, arguments, rt11Name);
    if (rt11Name.empty())
        return Status{"the program does not fit an RT-11 volume: " +
                      program.string()};

    std::string command;
    if (auto r = runCommand(command, rt11Name, arguments); !r)
        return r;
    if (!emu_.mountHdInMemory(folder.string(), std::move(desc)))
        return Status{"cannot serve the folder " + folder.string()};
    folder_ = folder;
    installMonitorWatch(emu_);
    setFileAsked([this](const std::string &device, const std::string &name) {
        fileAsked(device, name);
    });
    if (!handCommand(emu_, command))
        return Status{"the starter does not take the command"};
    return {};
}

Machine::~Machine()
{
    setFileAsked({});
}

/*
 * A program asks the monitor for a file by name.  If the folder has it
 * and the volume asked does not, it is put there before the monitor
 * looks: on DK: taken into the folder volume, on SY: written onto the
 * system diskette's copy in memory (where MACRO looks for SYSMAC.SML and
 * LINK for SYSLIB.OBJ).  The monitor is then made to read the directory
 * from the volume instead of the segment it holds in memory.
 */
void Machine::fileAsked(const std::string &device, const std::string &name)
{
    bool given = false;
    if (device.empty() || device == "DK" || device == "HD" || device == "HD0") {
        given = emu_.admitHdFile(name);
    } else if (device == "SY" || device == "DZ" || device == "DZ0") {
        const auto mounted = emu_.diskImage(0);
        std::vector<uint8_t> image(mounted.begin(), mounted.end());
        const auto opened = disk::openImage(image);
        const fs::path file = folderFile(folder_, name);
        if (!opened || opened->directory.find(name) || file.empty())
            return;
        std::ifstream in(file, std::ios::binary);
        const std::vector<uint8_t> data{std::istreambuf_iterator<char>(in), {}};
        try {
            disk::putFile(image, 0, false, name, data);
        } catch (const std::exception &) {
            return;                             /* no room on the diskette */
        }
        given = emu_.mountDiskImage(0, std::move(image));
    }
    if (given) {
        const uint16_t rmon = emu_.readWord(kRmonPointer);
        emu_.writeWord(static_cast<uint16_t>(rmon + kDirectoryInMemory), 0);
    }
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
