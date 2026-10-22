# CPU — KR1807VM1 (PDP-11 Compatible Processor)

## Overview

The KR1807VM1 (КР1807ВМ1) is a Soviet 16-bit microprocessor, a clone of the
DEC T-11 (DCT11-AA).  It implements a subset of the PDP-11 instruction set
without the Extended Instruction Set (EIS: MUL, DIV, ASH, ASHC), floating point
and memory management (MMU).

## Key Specifications

| Parameter              | Value                                      |
|------------------------|--------------------------------------------|
| Clock frequency        | 7.5 MHz                                    |
| Base microcycle        | 400 ns (3 clock phases × 133 ns)           |
| Registers              | 8 × 16-bit (R0–R5, SP=R6, PC=R7)          |
| Instruction count      | 66 (DEC's count; the list below names 81 mnemonics) |
| Addressing modes       | 8 (4 direct + 4 indirect)                  |
| Interrupt levels       | 4 priority levels, vectored                |
| Data bus               | 16-bit multiplexed address/data            |

## Processor Status Word (PSW)

```
  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 ┌───┬───┬───┬───┬───┬───┬───┬───┬───────────────┬───┬───┬───┬───┬───┐
 │ 0 │ 0 │ 0 │ 0 │ 0 │ 0 │ 0 │ 0 │   Priority    │ T │ N │ Z │ V │ C │
 └───┴───┴───┴───┴───┴───┴───┴───┴───────────────┴───┴───┴───┴───┴───┘
```

- **C** (bit 0): Carry
- **V** (bit 1): Overflow
- **Z** (bit 2): Zero
- **N** (bit 3): Negative
- **T** (bit 4): Trap — triggers BPT after each instruction
- **Priority** (bits 7-5): Current CPU priority level (0–7)
- Bits 15-8: Read as zero

## Mode Register

The mode register is read during the power-on/reset start sequence and
determines the startup address.

```
  F2FF hex = 1111 0010 1111 1111 binary

  Bits 15-13 = 111 → Start address: 172000, Restart address: 172004
```

At reset the CPU begins fetching instructions at the start address
(172000) - it does not read a PC/PSW pair from there; the ROM's first
word is a `JMP`.  The priority field comes up set: **PSW = 0340**, as it
does on the HALT restart below.  That matters: the ROM's self-test writes
its pattern over the whole of RAM, the vector page included, and only
lowers the priority once the vectors are its own.  At priority 0 a key
pressed during the test would take the keyboard interrupt (vector 0130,
priority 5) through a vector holding the test pattern, and the machine
would run off into a HALT storm.

## Addressing Modes

| Mode | Name                  | Syntax   | Description                     |
|------|-----------------------|----------|---------------------------------|
| 0    | Register              | Rn       | Operand is in the register      |
| 1    | Register deferred     | (Rn)     | Register holds address          |
| 2    | Autoincrement         | (Rn)+    | Address in Rn, then Rn += 2     |
| 3    | Autoincrement deferred| @(Rn)+   | Indirect through (Rn)+          |
| 4    | Autodecrement         | -(Rn)    | Rn -= 2, then use Rn as address |
| 5    | Autodecrement deferred| @-(Rn)   | Indirect through -(Rn)          |
| 6    | Index                 | X(Rn)    | Address = Rn + X (X follows)    |
| 7    | Index deferred        | @X(Rn)   | Indirect through X(Rn)          |

When PC (R7) is used as the register, modes 2/3/6/7 produce immediate,
absolute, relative, and relative-deferred addressing respectively.

For byte instructions, autoincrement/decrement step is 1 for R0–R5 and
2 for SP/PC (to maintain word alignment).

## Instruction Set (81 instructions)

### Zero-operand
HALT, WAIT, RTI, RTT, BPT, IOT, RESET, MFPT, NOP

### Single-operand (word)
CLR, COM, INC, DEC, NEG, ADC, SBC, TST, ROR, ROL, ASR, ASL, SXT, SWAB,
MTPS, MFPS

### Single-operand (byte)
CLRB, COMB, INCB, DECB, NEGB, ADCB, SBCB, TSTB, RORB, ROLB, ASRB, ASLB

### Double-operand (word)
MOV, CMP, BIT, BIC, BIS, ADD, SUB, XOR

### Double-operand (byte)
MOVB, CMPB, BITB, BICB, BISB

### Branch
BR, BNE, BEQ, BPL, BMI, BVC, BVS, BGE, BLT, BGT, BLE, BHI, BLOS, BHIS, BLO

### Subroutine
JMP, JSR, RTS, SOB

### Condition codes
CCC (clear all), SCC (set all), and individual CLC/CLV/CLZ/CLN, SEC/SEV/SEZ/SEN

### Traps
EMT, TRAP

### Not supported
MARK; MUL, DIV, ASH, ASHC (EIS); and all MMU/FPP instructions.

### Where the T-11 is not another PDP-11

As DEC's T-11 User's Guide and Engineering Specification have them, and
as the core does:

- a register source is read after the destination's address steps (UG
  B.2): `MOV R0,(R0)+` stores R0 + 2, `XOR R2,-(R2)` uses R2 - 2,
  `MOV PC,X(R)` the opcode's address + 4;
- an RTI that sets T traps at once; an RTT lets the instruction it
  returns to run first (UG 6.3.5.8);
- MTPS cannot set T (UG p. B-5);
- SP is 000376 after power-up (ES p. 9, UG 3.5.2.5).

An RTI that inhibited the trap as RTT does was once taken for the cure of
RUS/LAT switching (b97c30c, April 2026); with the trap at once every boot
and keyboard test passes, RUS/LAT included.

## Interrupt System

The T-11 uses internal vector decoding via 4 interrupt request lines (CP0–CP3).
Each combination maps to a fixed vector and priority level.

| CP3 | CP2 | CP1 | CP0 | Priority | Vector | Device              |
|-----|-----|-----|-----|----------|--------|---------------------|
|  X  |  X  |  X  |  X  |    8     |   —    | HALT (non-maskable) |
|  L  |  H  |  L  |  L  |    6     | 0100   | Timer               |
|  L  |  H  |  H  |  L  |    6     | 0110   | Serial RX           |
|  L  |  H  |  H  |  H  |    6     | 0114   | Serial TX           |
|  H  |  L  |  H  |  L  |    5     | 0130   | Keyboard MS7004     |
|  H  |  H  |  L  |  L  |    4     | 0060   | Keyboard MS7007     |
|  H  |  H  |  L  |  H  |    4     | 0064   | Monitor (VBlank)    |

Internal trap vectors: bus error (004), reserved instruction (010),
BPT/T-bit (014), IOT (020), EMT (030), TRAP (034).

HALT signal: pushes PC and PSW to stack, loads PC=172004, PSW=0340.

## Implementation Notes

The dispatch table uses a flat 64K-entry function pointer array indexed by the
full 16-bit opcode, giving O(1) dispatch with no branching overhead.  This
trades ~512 KB of memory for maximum decode speed.

## Sources

- PDP-11 Architecture Handbook (DEC, EB-23657-18)
- T-11 User's Guide (EK-DCT11-UG)
- T-11 Engineering Specification, Rev E, March 1982
  (http://www.bitsavers.org/pdf/dec/pdp11/t11/T11_Engineering_Specification_Rev_E_Mar82.pdf)
- NS4 technical description (3.858.420 TO), sections 4.2, 4.5
- Verilog T-11 model: https://github.com/1801BM1/cpu11/tree/master/t11
