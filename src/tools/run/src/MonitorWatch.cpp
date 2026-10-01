/*
 * MonitorWatch.cpp - see MonitorWatch.hpp.
 */

#include "MonitorWatch.hpp"

#include "Starter.hpp"

#include <utility>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
}

namespace ms0515::run {

namespace {

constexpr uint16_t kEmtPrint  = 0104351;    /* .PRINT, the text at R0     */
constexpr uint16_t kExtind    = 0416;       /* RMON + this: EXTIND        */
constexpr uint16_t kRomOutput = 0160000;    /* ROM: write the character in R0 */
constexpr uint8_t  kPromptDot = '.';
constexpr uint8_t  kNoNewline = 0200;       /* ends a .PRINT without CRLF */

struct {
    bool        prompted = false;
    uint8_t     severity = 0;
    std::string output;
} seen;

bool monitorWatchThunk(ms0515_cpu *cpu, uint16_t vector)
{
    if (vector != CPU_VEC_EMT || cpu->instruction != kEmtPrint)
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

bool monitorPrompted() noexcept { return seen.prompted; }

uint8_t endSeverity() noexcept { return seen.severity; }

std::string takeConsoleOutput()
{
    return std::exchange(seen.output, {});
}

} /* namespace ms0515::run */
