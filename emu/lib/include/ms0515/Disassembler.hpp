/*
 * Disassembler.hpp — PDP-11 / KR1807VM1 instruction disassembler.
 *
 * Decodes a single instruction starting at a given address.  Reads memory
 * through a caller-supplied callback so the same disassembler can be used
 * against ROM buffers, raw byte arrays, or a live Emulator.
 *
 * Output is formatted to match the existing reference listings:
 *   AAAAAA: MNEMONIC<TAB>operands
 * with all addresses and immediate values printed in 6-digit octal.
 */

#ifndef MS0515_DISASSEMBLER_HPP
#define MS0515_DISASSEMBLER_HPP

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace ms0515 {

class Emulator;

/* Callback signature for memory reads. */
using MemoryReader = std::function<uint16_t(uint16_t address)>;

struct DisassembledInstruction {
    uint16_t    address;     /* address of the first word                  */
    uint16_t    length;      /* total instruction length in bytes (2,4,6) */
    std::string mnemonic;    /* e.g. "MOV", "JSR", "BR"                    */
    std::string operands;    /* e.g. "#000100, R0"                         */

    [[nodiscard]] std::string text() const;
};

class Disassembler {
public:
    [[nodiscard]] static DisassembledInstruction
    decode(uint16_t address, const MemoryReader &read);

    [[nodiscard]] static DisassembledInstruction
    decode(uint16_t address, std::span<const uint8_t> buffer, uint16_t base);

    [[nodiscard]] static DisassembledInstruction
    decode(uint16_t address, const Emulator &emu);
};

} /* namespace ms0515 */

#endif /* MS0515_DISASSEMBLER_HPP */
