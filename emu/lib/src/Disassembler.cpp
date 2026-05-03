/*
 * Disassembler.cpp — PDP-11 (KR1807VM1) instruction decoder.
 *
 * Ported from tools/pdp11_disasm.py.  All numeric output is in 6-digit
 * octal to match the reference listings.
 */

#include "ms0515/Disassembler.hpp"
#include "EmulatorInternal.hpp"

#include <array>
#include <format>
#include <unordered_map>

namespace ms0515 {

namespace {

constexpr std::array<const char *, 8> kReg = {
    "R0", "R1", "R2", "R3", "R4", "R5", "SP", "PC"
};

/* ── Helpers ──────────────────────────────────────────────────────────── */

std::string formatOctal6(uint16_t value)
{
    return std::format("{:06o}", value);
}

std::string formatOctal3(uint8_t value)
{
    return std::format("{:03o}", static_cast<unsigned>(value));
}

/* Decoding cursor — owns the read callback and tracks how many extension
 * words have been consumed past the opcode word. */
struct Cursor {
    uint16_t           base;       /* address of opcode word       */
    uint16_t           extWords;   /* extension words consumed     */
    const MemoryReader *read;

    uint16_t curAddr() const { return static_cast<uint16_t>(base + 2 + extWords * 2); }

    uint16_t readNext()
    {
        uint16_t addr = curAddr();
        ++extWords;
        return (*read)(addr);
    }
};

std::string decodeOperand(int mode, int reg, Cursor &cur)
{
    switch (mode) {
    case 0:
        return kReg[reg];
    case 1:
        return std::string("(") + kReg[reg] + ")";
    case 2:
        if (reg == 7) {
            uint16_t val = cur.readNext();
            return "#" + formatOctal6(val);
        }
        return std::string("(") + kReg[reg] + ")+";
    case 3:
        if (reg == 7) {
            uint16_t val = cur.readNext();
            return "@#" + formatOctal6(val);
        }
        return std::string("@(") + kReg[reg] + ")+";
    case 4:
        return std::string("-(") + kReg[reg] + ")";
    case 5:
        return std::string("@-(") + kReg[reg] + ")";
    case 6: {
        uint16_t offset = cur.readNext();
        if (reg == 7) {
            uint16_t target = static_cast<uint16_t>(cur.curAddr() + offset);
            return formatOctal6(target);
        }
        return formatOctal6(offset) + "(" + kReg[reg] + ")";
    }
    case 7: {
        uint16_t offset = cur.readNext();
        if (reg == 7) {
            uint16_t target = static_cast<uint16_t>(cur.curAddr() + offset);
            return "@" + formatOctal6(target);
        }
        return std::string("@") + formatOctal6(offset) + "(" + kReg[reg] + ")";
    }
    }
    return "???";
}

std::string decodeBranchTarget(uint16_t opcode, const Cursor &cur)
{
    int offset = opcode & 0xFF;
    if (offset & 0x80) {
        offset -= 256;
    }
    /* PC at the time the branch executes is curAddr() (one past opcode). */
    uint16_t target = static_cast<uint16_t>(cur.curAddr() + offset * 2);
    return formatOctal6(target);
}

std::string decodeCccScc(uint16_t op)
{
    bool isSet = (op & 0000020) != 0;
    int  bits  = op & 0xF;
    if (bits == 0) {
        return "NOP";
    }
    const char *prefix = isSet ? "SE" : "CL";
    if (bits == 0xF) {
        return isSet ? "SCC" : "CCC";
    }
    /* Single-flag forms get a one-letter suffix. */
    int popcount = ((bits & 1) != 0) + ((bits & 2) != 0) +
                   ((bits & 4) != 0) + ((bits & 8) != 0);
    if (popcount == 1) {
        const char *flag = (bits == 1) ? "C" :
                           (bits == 2) ? "V" :
                           (bits == 4) ? "Z" : "N";
        return std::string(prefix) + flag;
    }
    /* Multi-flag combined form. */
    std::string out;
    if (bits & 1) { out += prefix; out += "C "; }
    if (bits & 2) { out += prefix; out += "V "; }
    if (bits & 4) { out += prefix; out += "Z "; }
    if (bits & 8) { out += prefix; out += "N "; }
    if (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

const std::unordered_map<int, const char *> kSingleOps = {
    {00050, "CLR"},  {00051, "COM"},  {00052, "INC"},  {00053, "DEC"},
    {00054, "NEG"},  {00055, "ADC"},  {00056, "SBC"},  {00057, "TST"},
    {00060, "ROR"},  {00061, "ROL"},  {00062, "ASR"},  {00063, "ASL"},
    {00067, "SXT"},
    {01050, "CLRB"}, {01051, "COMB"}, {01052, "INCB"}, {01053, "DECB"},
    {01054, "NEGB"}, {01055, "ADCB"}, {01056, "SBCB"}, {01057, "TSTB"},
    {01060, "RORB"}, {01061, "ROLB"}, {01062, "ASRB"}, {01063, "ASLB"},
    {01064, "MTPS"}, {01067, "MFPS"},
};

const std::unordered_map<int, const char *> kBranches = {
    {0000400, "BR"},
    {0001000, "BNE"},  {0001400, "BEQ"},
    {0002000, "BGE"},  {0002400, "BLT"},
    {0003000, "BGT"},  {0003400, "BLE"},
    {0100000, "BPL"},  {0100400, "BMI"},
    {0101000, "BVC"},  {0101400, "BVS"},
    {0102000, "BHIS"}, {0102400, "BLO"},
    {0103000, "BHI"},  {0103400, "BLOS"},
};

const std::unordered_map<int, const char *> kDoubleWord = {
    {001, "MOV"}, {002, "CMP"}, {003, "BIT"},
    {004, "BIC"}, {005, "BIS"}, {006, "ADD"},
};

const std::unordered_map<int, const char *> kDoubleByte = {
    {011, "MOVB"}, {012, "CMPB"}, {013, "BITB"},
    {014, "BICB"}, {015, "BISB"},
};

/* Output destination for the decoder. */
struct Decoded {
    std::string mnemonic;
    std::string operands;
};

/* Helper that turns "FOO" + first/second operand strings into a Decoded. */
Decoded one(const char *m, std::string a)
{
    return Decoded{m, std::move(a)};
}
Decoded two(const char *m, std::string a, std::string b)
{
    return Decoded{m, std::move(a) + ", " + std::move(b)};
}

Decoded decodeWord(uint16_t op, Cursor &cur)
{
    /* ── Zero-operand ── */
    switch (op) {
    case 0000000: return Decoded{"HALT",  ""};
    case 0000001: return Decoded{"WAIT",  ""};
    case 0000002: return Decoded{"RTI",   ""};
    case 0000003: return Decoded{"BPT",   ""};
    case 0000004: return Decoded{"IOT",   ""};
    case 0000005: return Decoded{"RESET", ""};
    case 0000006: return Decoded{"RTT",   ""};
    case 0000007: return Decoded{"MFPT",  ""};
    }

    /* ── RTS / RETURN ── */
    if ((op & 0177770) == 0000200) {
        int reg = op & 7;
        if (reg == 7) {
            return Decoded{"RETURN", ""};
        }
        return Decoded{"RTS", kReg[reg]};
    }

    /* ── Condition code set/clear ── */
    if ((op & 0177740) == 0000240) {
        return Decoded{decodeCccScc(op), ""};
    }

    /* ── SWAB ── */
    if ((op & 0177700) == 0000300) {
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        return one("SWAB", decodeOperand(dstMode, dstReg, cur));
    }

    /* ── Single-operand ── */
    int top10 = (op >> 6) & 01777;
    auto it = kSingleOps.find(top10);
    if (it != kSingleOps.end()) {
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        return one(it->second, decodeOperand(dstMode, dstReg, cur));
    }

    /* ── JMP ── */
    if ((op & 0177700) == 0000100) {
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        if (dstMode == 0) {
            /* JMP Rn is illegal — emit as data word. */
            return Decoded{".WORD", formatOctal6(op)};
        }
        return one("JMP", decodeOperand(dstMode, dstReg, cur));
    }

    /* ── JSR / CALL ── */
    if ((op & 0177000) == 0004000) {
        int reg     = (op >> 6) & 7;
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        std::string operand = decodeOperand(dstMode, dstReg, cur);
        if (reg == 7) {
            return one("CALL", std::move(operand));
        }
        return two("JSR", kReg[reg], std::move(operand));
    }

    /* ── EMT / TRAP ── */
    if ((op & 0177400) == 0104000) {
        return one("EMT", formatOctal3(static_cast<uint8_t>(op & 0xFF)));
    }
    if ((op & 0177400) == 0104400) {
        return one("TRAP", formatOctal3(static_cast<uint8_t>(op & 0xFF)));
    }

    /* ── SOB ── */
    if ((op & 0177000) == 0077000) {
        int reg    = (op >> 6) & 7;
        int offset = op & 077;
        uint16_t target = static_cast<uint16_t>(cur.curAddr() - offset * 2);
        return two("SOB", kReg[reg], formatOctal6(target));
    }

    /* ── Branches ── */
    {
        int code = op & 0177400;
        auto bit = kBranches.find(code);
        if (bit != kBranches.end()) {
            return one(bit->second, decodeBranchTarget(op, cur));
        }
    }

    /* ── Double-operand (word and byte) ── */
    int top4 = (op >> 12) & 0xF;
    auto dwIt = kDoubleWord.find(top4);
    auto dbIt = kDoubleByte.find(top4);
    if (dwIt != kDoubleWord.end() || dbIt != kDoubleByte.end()) {
        const char *mnem = (dwIt != kDoubleWord.end()) ? dwIt->second : dbIt->second;
        int srcMode = (op >> 9) & 7;
        int srcReg  = (op >> 6) & 7;
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        std::string src = decodeOperand(srcMode, srcReg, cur);
        std::string dst = decodeOperand(dstMode, dstReg, cur);
        return two(mnem, std::move(src), std::move(dst));
    }

    /* ── SUB ── */
    if (top4 == 016) {
        int srcMode = (op >> 9) & 7;
        int srcReg  = (op >> 6) & 7;
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        std::string src = decodeOperand(srcMode, srcReg, cur);
        std::string dst = decodeOperand(dstMode, dstReg, cur);
        return two("SUB", std::move(src), std::move(dst));
    }

    /* ── XOR ── */
    if ((op & 0177000) == 0074000) {
        int reg     = (op >> 6) & 7;
        int dstMode = (op >> 3) & 7;
        int dstReg  = op & 7;
        return two("XOR", kReg[reg], decodeOperand(dstMode, dstReg, cur));
    }

    /* ── Unknown — emit as .WORD ── */
    return Decoded{".WORD", formatOctal6(op)};
}

} /* anonymous namespace */

/* ── DisassembledInstruction::text ────────────────────────────────────── */

std::string DisassembledInstruction::text() const
{
    if (operands.empty()) {
        return mnemonic;
    }
    return mnemonic + "\t" + operands;
}

/* ── Disassembler::decode (callback variant) ──────────────────────────── */

DisassembledInstruction Disassembler::decode(uint16_t address,
                                             const MemoryReader &read)
{
    DisassembledInstruction out;
    out.address = address;

    Cursor cur{address, 0, &read};
    uint16_t opcode = read(address);
    Decoded  d      = decodeWord(opcode, cur);

    out.mnemonic = std::move(d.mnemonic);
    out.operands = std::move(d.operands);
    out.length   = static_cast<uint16_t>(2 + cur.extWords * 2);
    return out;
}

/* ── Disassembler::decode (raw buffer variant) ────────────────────────── */

DisassembledInstruction Disassembler::decode(uint16_t address,
                                             std::span<const uint8_t> buffer,
                                             uint16_t base)
{
    auto read = [buffer, base](uint16_t addr) -> uint16_t {
        auto off = static_cast<std::size_t>(static_cast<uint16_t>(addr - base));
        if (off + 1 >= buffer.size())
            return 0;
        return static_cast<uint16_t>(buffer[off] | (buffer[off + 1] << 8));
    };
    return decode(address, MemoryReader{read});
}

/* ── Disassembler::decode (live emulator variant) ─────────────────────── */

DisassembledInstruction Disassembler::decode(uint16_t address, const Emulator &emu)
{
    auto read = [&emu](uint16_t addr) -> uint16_t {
        return board_read_word(
            const_cast<ms0515_board_t *>(&internal::board(emu)), addr);
    };
    return decode(address, MemoryReader{read});
}

} /* namespace ms0515 */
