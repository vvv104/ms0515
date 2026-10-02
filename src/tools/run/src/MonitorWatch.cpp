/*
 * MonitorWatch.cpp - see MonitorWatch.hpp.
 */

#include "MonitorWatch.hpp"

#include "Starter.hpp"

#include <ms0515/disk/Directory.hpp>

#include <utility>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace ms0515::run {

namespace {

constexpr uint16_t kEmtPrint  = 0104351;    /* .PRINT, the text at R0     */
constexpr uint16_t kEmtTtyin  = 0104340;    /* .TTYIN / .TTINR            */
constexpr uint16_t kEmtRequest = 0104375;   /* the requests with an area at R0 */
constexpr uint8_t  kCodeDelete = 0;         /* among them: .DELETE,       */
constexpr uint8_t  kCodeLookup = 1;         /* .LOOKUP,                   */
constexpr uint8_t  kCodeEnter  = 2;         /* .ENTER,                    */
constexpr uint8_t  kCodeRename = 4;         /* .RENAME                    */
constexpr uint16_t kEmtDstatus = 0104342;   /* .DSTATUS, the device's name at R0 */
constexpr uint16_t kEmtFetch   = 0104343;   /* .FETCH, the same           */
constexpr uint16_t kExtind    = 0416;       /* RMON + this: EXTIND        */
constexpr uint16_t kRomOutput = 0160000;    /* ROM: write the character in R0 */
constexpr uint16_t kRomInput  = 0160004;    /* ROM: the next key into R0  */
constexpr uint8_t  kPromptDot = '.';
constexpr uint8_t  kNoNewline = 0200;       /* ends a .PRINT without CRLF */

struct {
    bool        prompted = false;
    uint8_t     severity = 0;
    bool        keyAsked = false;
    std::string output;
} seen;

FileAsked   fileAsked;
DeviceNamed deviceNamed;

/* The requests with an area at R0 (EMT 375) that name a file - .DELETE,
 * .LOOKUP, .ENTER, .RENAME, the area's second byte 0, 1, 2 or 4, its
 * second word at the name in four RAD50 words, the device first: tell
 * of the device, and for a .LOOKUP of the file, who serves them. */
void noteFileRequest(ms0515_cpu *cpu)
{
    ms0515_board_t *board = cpu->board;
    const uint16_t area = cpu->r[0];
    const uint8_t code = board_read_byte(board, static_cast<uint16_t>(area + 1));
    if (code != kCodeDelete && code != kCodeLookup && code != kCodeEnter &&
        code != kCodeRename)
        return;
    const uint16_t name = board_read_word(board, static_cast<uint16_t>(area + 2));
    uint16_t w[4];
    for (int i = 0; i < 4; ++i)
        w[i] = board_read_word(board, static_cast<uint16_t>(name + 2 * i));
    if (w[0] >= 40 * 40 * 40)
        return;
    if (deviceNamed) deviceNamed(w[0]);
    if (code == kCodeLookup && fileAsked)
        fileAsked(rad50Text(w[0]), disk::decodeRad50Name(w[1], w[2], w[3]));
}

/* .DSTATUS and .FETCH (EMT 342, 343): R0 at the device's name. */
void noteDeviceRequest(ms0515_cpu *cpu)
{
    const uint16_t device = board_read_word(cpu->board, cpu->r[0]);
    if (deviceNamed && device < 40 * 40 * 40) deviceNamed(device);
}

bool monitorWatchThunk(ms0515_cpu *cpu, uint16_t vector)
{
    if (vector != CPU_VEC_EMT)
        return false;
    if (cpu->instruction == kEmtRequest)
        noteFileRequest(cpu);
    if (cpu->instruction == kEmtDstatus || cpu->instruction == kEmtFetch)
        noteDeviceRequest(cpu);
    if (cpu->instruction != kEmtPrint && cpu->instruction != kEmtTtyin)
        return false;
    ms0515_board_t *board = cpu->board;
    const uint16_t rmon = board_read_word(board, kRmonPointer);
    const bool kmon =
        board_read_word(board, static_cast<uint16_t>(rmon + kKmoninOffset)) != 0;
    if (cpu->instruction == kEmtTtyin) {
        if (!kmon) seen.keyAsked = true;    /* the program wants a key */
        return false;
    }
    if (!kmon)
        return false;                       /* a program's own .PRINT */

    /* KDOT in KMON.MAC: a .PRINT of nothing for the new line (KCRLF),
     * then a .PRINT of <.><200>.  Neither is printed. */
    const uint16_t text = cpu->r[0];
    const uint8_t first = board_read_byte(board, text);
    if (first == 0)
        return true;
    if (first != kPromptDot ||
        board_read_byte(board, static_cast<uint16_t>(text + 1)) != kNoNewline)
        return false;
    seen.prompted = true;
    seen.severity = board_read_byte(board, static_cast<uint16_t>(rmon + kExtind));
    return true;
}

/* The ROM's two console entries.  The output is the text; a call of the
 * input from below the resident monitor - the return address on the
 * stack says where from - is a program reading the keyboard itself, past
 * the monitor, whose own calls come from within it. */
void consoleTap(ms0515_cpu *cpu)
{
    if (cpu->instruction_pc == kRomOutput) {
        seen.output.push_back(static_cast<char>(cpu->r[0] & 0xFF));
        return;
    }
    ms0515_board_t *board = cpu->board;
    const uint16_t caller = board_read_word(board, cpu->r[CPU_REG_SP]);
    if (caller < board_read_word(board, kRmonPointer))
        seen.keyAsked = true;
}

} /* namespace */

void installMonitorWatch(ms0515::Emulator &emu)
{
    seen = {};
    /* EXTIND gathers every error since it was last cleared: start clean. */
    const uint16_t rmon = emu.readWord(kRmonPointer);
    emu.writeByte(static_cast<uint16_t>(rmon + kExtind), 0);
    emu.setTrapThunk(&monitorWatchThunk);
    static constexpr uint16_t kEntries[] = {kRomOutput, kRomInput};
    emu.setExecHook(std::span<const uint16_t>{kEntries}, &consoleTap);
}

void setFileAsked(FileAsked handler)
{
    fileAsked = std::move(handler);
}

void setDeviceNamed(DeviceNamed handler)
{
    deviceNamed = std::move(handler);
}

std::string rad50Text(uint16_t word)
{
    static constexpr char kRad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";
    if (word >= 40 * 40 * 40)
        return {};
    std::string text{kRad50[word / 1600], kRad50[word / 40 % 40], kRad50[word % 40]};
    while (!text.empty() && text.back() == ' ') text.pop_back();
    return text;
}

uint16_t rad50Word(std::string_view text)
{
    static constexpr std::string_view kRad50 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";
    if (text.empty() || text.size() > 3)
        return 0;
    uint16_t word = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const char c = i < text.size() ? text[i] : ' ';
        const auto code = kRad50.find(c >= 'a' && c <= 'z' ? static_cast<char>(c - 32) : c);
        if (code == std::string_view::npos || c == '%')
            return 0;
        word = static_cast<uint16_t>(word * 40 + code);
    }
    return word;
}

bool monitorPrompted() noexcept { return seen.prompted; }

uint8_t endSeverity() noexcept { return seen.severity; }

bool takeKeyAsked() noexcept
{
    return std::exchange(seen.keyAsked, false);
}

std::string takeConsoleOutput()
{
    return std::exchange(seen.output, {});
}

} /* namespace ms0515::run */
