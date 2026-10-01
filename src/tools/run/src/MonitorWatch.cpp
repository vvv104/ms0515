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
constexpr uint16_t kEmtRequest = 0104375;   /* the requests with an area at R0 */
constexpr uint8_t  kCodeLookup = 1;         /* .LOOKUP among them         */
constexpr uint16_t kExtind    = 0416;       /* RMON + this: EXTIND        */
constexpr uint16_t kRomOutput = 0160000;    /* ROM: write the character in R0 */
constexpr uint8_t  kPromptDot = '.';
constexpr uint8_t  kNoNewline = 0200;       /* ends a .PRINT without CRLF */

struct {
    bool        prompted = false;
    uint8_t     severity = 0;
    std::string output;
} seen;

FileAsked fileAsked;

/* A .LOOKUP (EMT 375, R0 at an area whose second byte is 1, its second
 * word at the file's name in four RAD50 words): tell who serves files. */
void noteLookup(ms0515_cpu *cpu)
{
    static constexpr char kRad50[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ$.%0123456789";
    ms0515_board_t *board = cpu->board;
    const uint16_t area = cpu->r[0];
    if (board_read_byte(board, static_cast<uint16_t>(area + 1)) != kCodeLookup)
        return;
    const uint16_t name = board_read_word(board, static_cast<uint16_t>(area + 2));
    uint16_t w[4];
    for (int i = 0; i < 4; ++i)
        w[i] = board_read_word(board, static_cast<uint16_t>(name + 2 * i));
    if (w[0] >= 40 * 40 * 40)
        return;
    std::string device{kRad50[w[0] / 1600], kRad50[w[0] / 40 % 40], kRad50[w[0] % 40]};
    while (!device.empty() && device.back() == ' ') device.pop_back();
    fileAsked(device, disk::decodeRad50Name(w[1], w[2], w[3]));
}

bool monitorWatchThunk(ms0515_cpu *cpu, uint16_t vector)
{
    if (vector != CPU_VEC_EMT)
        return false;
    if (cpu->instruction == kEmtRequest && fileAsked)
        noteLookup(cpu);
    if (cpu->instruction != kEmtPrint)
        return false;
    ms0515_board_t *board = cpu->board;
    const uint16_t rmon = board_read_word(board, kRmonPointer);
    if (board_read_word(board, static_cast<uint16_t>(rmon + kKmoninOffset)) == 0)
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

void consoleTap(ms0515_cpu *cpu)
{
    seen.output.push_back(static_cast<char>(cpu->r[0] & 0xFF));
}

} /* namespace */

void installMonitorWatch(ms0515::Emulator &emu)
{
    seen = {};
    /* EXTIND gathers every error since it was last cleared: start clean. */
    const uint16_t rmon = emu.readWord(kRmonPointer);
    emu.writeByte(static_cast<uint16_t>(rmon + kExtind), 0);
    emu.setTrapThunk(&monitorWatchThunk);
    emu.setExecHook(kRomOutput, &consoleTap);
}

void setFileAsked(FileAsked handler)
{
    fileAsked = std::move(handler);
}

bool monitorPrompted() noexcept { return seen.prompted; }

uint8_t endSeverity() noexcept { return seen.severity; }

std::string takeConsoleOutput()
{
    return std::exchange(seen.output, {});
}

} /* namespace ms0515::run */
