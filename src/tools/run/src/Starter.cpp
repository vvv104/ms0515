/*
 * Starter.cpp - START.SAV and the hand-over of a command; see Starter.hpp.
 */

#include "Starter.hpp"

#include <array>

namespace ms0515::run {

namespace {

/* The RT-11 job's fixed places. */
constexpr uint16_t kJobStart   = 040;      /* start address               */
constexpr uint16_t kJobStack   = 042;      /* initial stack pointer       */
constexpr uint16_t kJobLimit   = 050;      /* highest address in use      */
constexpr uint16_t kLoadBitmap = 0360;     /* the blocks RUN loads        */
constexpr uint16_t kChainCount = 0510;     /* bytes of commands for KMON  */
constexpr uint16_t kChainText  = 0512;     /* the commands, ASCIZ         */
constexpr std::size_t kChainRoom = 01000 - kChainText;  /* the area ends at 777 */

constexpr uint16_t kBase = 01000;

/*
 *   1000  MOV   #TEXT,R1         ; ESC H ESC J: home, erase the screen
 *   1004  MOV   #4,R2
 *   1010  MOVB  (R1)+,R0
 *   1012  EMT   341              ; .TTYOUT
 *   1014  BCS   1012
 *   1016  SOB   R2,1010
 *   1020  TST   @#FLAG           ; the wait
 *   1024  BEQ   1020
 *   1026  BIS   #40,@#44         ; SPXIT$: commands for KMON at 510
 *   1034  CLR   R0
 *   1036  EMT   350              ; .EXIT
 *   1040  FLAG: .WORD 0
 *   1042  TEXT: .BYTE 33,'H,33,'J
 */
constexpr std::array<uint16_t, 19> kCode = {
    0012701, 01042,
    0012702, 4,
    0112100,
    0104341,
    0103776,
    0077204,
    0005737, 01040,
    0001775,
    0052737, 040, 044,
    0005000,
    0104350,
    0,
    0x481B, 0x4A1B,
};
constexpr uint16_t kWait     = 01020;
constexpr uint16_t kWaitEnd  = 01024;
constexpr uint16_t kFlag     = 01040;
constexpr uint16_t kLimit    = kBase + 2 * static_cast<uint16_t>(kCode.size());

void putWord(std::vector<uint8_t> &image, std::size_t at, uint16_t value)
{
    image[at]     = static_cast<uint8_t>(value & 0xFF);
    image[at + 1] = static_cast<uint8_t>(value >> 8);
}

} /* namespace */

std::vector<uint8_t> starterProgram()
{
    std::vector<uint8_t> image(2 * 512, 0);
    putWord(image, kJobStart, kBase);
    putWord(image, kJobStack, kBase);
    putWord(image, kJobLimit, kLimit);
    image[kLoadBitmap] = 0xC0;                  /* blocks 0 and 1 */
    for (std::size_t i = 0; i < kCode.size(); ++i)
        putWord(image, kBase + 2 * i, kCode[i]);
    return image;
}

bool starterWaiting(ms0515::Emulator &emu)
{
    const uint16_t pc = emu.pc();
    if (pc < kWait || pc > kWaitEnd)
        return false;
    for (std::size_t i = 0; i < kCode.size(); ++i)
        if (emu.readWord(static_cast<uint16_t>(kBase + 2 * i)) != kCode[i])
            return false;
    return true;
}

bool handCommands(ms0515::Emulator &emu, std::span<const std::string> lines)
{
    std::size_t bytes = 0;
    for (const auto &line : lines) {
        if (line.empty() || line.size() > kMaxCommand)
            return false;
        bytes += line.size() + 1;
    }
    if (lines.empty() || bytes > kChainRoom || !starterWaiting(emu))
        return false;
    uint16_t at = kChainText;
    for (const auto &line : lines) {
        for (char c : line)
            emu.writeByte(at++, static_cast<uint8_t>(c));
        emu.writeByte(at++, 0);
    }
    emu.writeWord(kChainCount, static_cast<uint16_t>(at - kChainText));
    emu.writeWord(kFlag, 1);
    return true;
}

bool monitorInControl(ms0515::Emulator &emu)
{
    const uint16_t rmon = emu.readWord(kRmonPointer);
    return emu.readWord(static_cast<uint16_t>(rmon + kKmoninOffset)) != 0;
}

} /* namespace ms0515::run */
