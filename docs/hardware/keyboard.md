# Keyboard — MS7004 Interface via i8251 USART

## Overview

The MS0515 keyboard port uses a KR580VV51 (КР580ВВ51), a Soviet clone of
the Intel 8251 USART, to communicate with the Elektronika MS 7004 external
keyboard over a serial link at 4800 baud.

## Register Addresses

| Address | Access | Function                    |
|---------|--------|-----------------------------|
| 177440  | Read   | Receiver data buffer        |
| 177460  | Write  | Transmitter data buffer     |
| 177442  | Read   | Status register             |
| 177442  | Write  | Mode/command instruction     |

Note: address 177462 is also mapped to the command register for writes.

## Initialization Sequence

The i8251 requires a two-phase programming sequence after reset:

1. **Hardware reset** occurs on every CPU start/restart via the RESET signal.
2. **Software reset** (for safety): write three zero bytes to the command
   register (177442), then write the reset command (bit 6 = 1).
3. **Mode instruction**: defines baud rate factor, character length, parity,
   and stop bits.
4. **Command instruction**: enables transmitter/receiver, sets DTR/RTS.

The BIOS programs the keyboard port as:
- Asynchronous mode, 1/16 clock divisor
- 8 data bits, 2 stop bits, no parity
- Mode word: 0xCE (0o316)

## Mode Instruction Format

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │ S2 │ S1 │ EP │ PE │ D2 │ D1 │ B2 │ B1 │
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

- **B1-B0**: Baud rate factor (00=sync, 01=1x, 10=16x, 11=64x)
- **D1-D0**: Character length (00=5, 01=6, 10=7, 11=8 bits)
- **PE**: Parity enable
- **EP**: Even parity (when PE=1)
- **S1-S0**: Stop bits (01=1, 10=1.5, 11=2)

## Command Instruction Format

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │HUNT│IRST│ RTS│ERST│SBRK│RxEN│ DTR│TxEN│
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

- **TxEN** (bit 0): Transmit enable
- **DTR** (bit 1): Data Terminal Ready
- **RxEN** (bit 2): Receive enable
- **SBRK** (bit 3): Send break
- **ERST** (bit 4): Error reset (clears PE, OE, FE)
- **RTS** (bit 5): Request To Send
- **IRST** (bit 6): Internal reset (returns to mode phase)
- **HUNT** (bit 7): Hunt mode (sync only)

## Status Register Format

```
   7    6    5    4    3    2    1    0
 ┌────┬────┬────┬────┬────┬────┬────┬────┐
 │ DSR│BRKD│ FE │ OE │ PE │TxE │TxRD│RxRD│
 └────┴────┴────┴────┴────┴────┴────┴────┘
```

- **RxRDY** (bit 0): Receiver has data ready for CPU to read
- **TxRDY** (bit 1): Transmitter ready for next byte
- **TxEMPTY** (bit 2): Transmitter completely empty
- **PE** (bit 3): Parity error
- **OE** (bit 4): Overrun error
- **FE** (bit 5): Framing error
- **BRKD** (bit 6): Break detect (async) / sync detect
- **DSR** (bit 7): Data Set Ready input

## Interrupt

When RxRDY becomes active and RxEN is set in the command register, the
USART asserts an interrupt request:
- **Vector**: 0130
- **Priority**: 5

The CPU services this interrupt to read the received scan code.

## MS7007 Parallel Keyboard (Alternative)

The MS0515 also supports the MS7007 built-in film keyboard via a second
KR580VV55 PPI at addresses 177540–177546.  The MS7007 uses an 8x11 scan
matrix with interrupt on vector 060, priority 4.  This interface is not
yet implemented in the emulator.

---

## MS 7004 Keyboard Protocol

The sections below document the MS 7004 keyboard itself — scancodes it
sends and commands it accepts.  Source: MS 7004 ТО (technical description),
Tables 1–3.

### Table 1 — Alphanumeric Key Scancodes

All codes are octal.  Verified against the ms0515btl emulator's working
scancode table; a few values differ from the ТО scan (likely OCR/print
artifacts in the original document).

**Numpad:**

| Key      | Code |
|----------|------|
| 0 (wide) | 222  |
| .        | 224  |
| ВВОД     | 225  |
| 1        | 226  |
| 2        | 227  |
| 3        | 230  |
| 4        | 231  |
| 5        | 232  |
| 6        | 233  |
| , (comma)| 234  |
| 7        | 235  |
| 8        | 236  |
| 9        | 237  |
| — (minus)| 240  |

**Main keyboard (by row):**

Row 1 (digit row):

| Key      | Code | Key      | Code | Key      | Code |
|----------|------|----------|------|----------|------|
| {/\|     | 374  | 4/$      | 320  | 8/(      | 345  |
| ;/+      | 277  | 5/%      | 325  | 9/)      | 352  |
| !/1      | 300  | 6/&      | 333  | 0/Ø      | 357  |
| 2/"      | 305  | 7/↑      | 340  | -/=      | 371  |
| 3/#      | 313  |          |      | }/↖      | 365  |
|          |      |          |      | ЗБ (BS)  | 274  |

Row 2 (top letter row):

| Key      | Code | Key      | Code | Key      | Code |
|----------|------|----------|------|----------|------|
| ТАБ      | 276  | Н/N      | 334  | З/Z      | 360  |
| Й/J      | 301  | Г/G      | 341  | Х/H      | 366  |
| Ц/C      | 306  | Ш/[      | 346  | :/*      | 372  |
| У/U      | 314  | Щ/]      | 353  | ~/Г      | 304  |
| К/K      | 321  |          |      | ВК (CR)  | 275  |
| Е/E      | 327  |          |      |          |      |

Row 3 (home row):

| Key      | Code | Key      | Code | Key      | Code |
|----------|------|----------|------|----------|------|
| Ф/F      | 302  | Р/R      | 335  | Ж/V      | 362  |
| Ы/Y      | 307  | О/O      | 342  | Э/\\     | 373  |
| В/W      | 315  | Л/L      | 347  | ./>      | 367  |
| А/A      | 322  | Д/D      | 354  | Ъ/—      | 361  |
| П/P      | 330  |          |      |          |      |

Row 4 (bottom letter row):

| Key      | Code | Key      | Code | Key      | Code |
|----------|------|----------|------|----------|------|
| Я/Q      | 303  | И/I      | 331  | Ю/@      | 355  |
| Ч/¬      | 310  | Т/T      | 336  | ,/<      | 363  |
| С/S      | 316  | Ь/X      | 343  | //?      | 312  |
| М/M      | 323  | Б/B      | 350  | _        | 361  |

Row 5: Пробел (Space) = 324.

Note: scancode 361 is shared by Ъ (РУС mode, home row) and _ (ЛАТ mode,
bottom row) — these are two different key caps that produce the same code;
the ROM's character table selects the output glyph based on mode.

Резервная клавиша (reserve key) = 311 — not present on standard layout.

### Table 2 — Function Key Scancodes

All codes are octal.

| Key              | Code | Key              | Code |
|------------------|------|------------------|------|
| Стоп кадр (F1)   | 126  | ФКС (CapsLock)   | 260  |
| Печать кадра (F2)| 127  | ВР (Shift) press | 256  |
| Пауза (F3)       | 130  | ВР (Shift) rel.  | 263  |
| Уст. режима (F4) | 131  | КМП (Compose)    | 261  |
| Ф5               | 132  | Рус/Лат          | 262  |
| Прерыв. (F6)     | 144  | НТ (Find)        | 212  |
| Продолж. (F7)    | 145  | Вст. (Insert)    | 213  |
| Отмен (F8)       | 146  | Удал. (Remove)   | 214  |
| Основн. кадр (F9)| 147  | Выбр. (Select)   | 215  |
| Выход (F10)      | 150  | Пред. кадр       | 216  |
| Ф11 (АР2)       | 161  | След. кадр       | 217  |
| Ф12 (ВШ)        | 162  | ПФ1              | 241  |
| Ф13 (ПС)        | 163  | ПФ2              | 242  |
| Доп. вариант (F14)| 164 | ПФ3              | 243  |
| ПМ (Help)        | 174  | ПФ4              | 244  |
| ИСП (Perform)    | 175  | ↑ (Up)           | 252  |
| Ф17              | 200  | ← (Left)         | 247  |
| Ф18              | 201  | ↓ (Down)         | 251  |
| Ф19              | 202  | → (Right)        | 250  |
| Ф20              | 203  |                  |      |
| СУ (Ctrl) press  | 257  | **Auto-repeat**  | 254  |

Special scancodes:
- **256** (ВР press) — sent when either Shift key is pressed.
- **263** (ALL-UP) — sent when the last held key is released.  Also
  used as the "ВР release" code since Shift uses the held-modifier
  protocol.
- **254** (auto-repeat) — listed in the ТО as auto-repeat scancode,
  but the ROM maps it to the `$` character.  Real auto-repeat sends
  the held key's own scancode, not 254.

### Table 3 — Host → Keyboard Commands

Commands sent by the CPU to the keyboard via the USART TX register.
All code values are octal.

**Single-byte commands:**

| Code (oct) | Code (hex) | Function                              |
|------------|------------|---------------------------------------|
| 021        | 0x11       | Latin indicator ON                    |
| 210        | 0x88       | Data output disabled                  |
| 213        | 0x8B       | Data output enabled                   |
| 231        | 0x99       | Keyclick disabled                     |
| 237        | 0x9F       | Produce click sound                   |
| 241        | 0xA1       | Sound disabled                        |
| 247        | 0xA7       | Produce bell sound                    |
| 220        | 0x90       | Auto-repeat enabled (delay 500ms, period 33ms) |
| 253        | 0xAB       | ID probe → keyboard responds 001, 000|
| 341        | 0xE1       | Auto-repeat disabled                  |
| 331        | 0xD9       | Auto-repeat disabled (alternate)      |
| 343        | 0xE3       | Auto-repeat enabled (alternate)       |
| 375        | 0xFD       | Power-up reset → responds 001,000,000,000 |

Note: 0x90 is listed as a two-byte prefix in the original ТО table layout,
but the ROM firmware always sends it as a standalone single-byte command.

Auto-repeat and click are independent flags in the firmware's parameters
byte (bit 5 = repeat, bit 3 = click).  Each command touches exactly one
bit: 0x99 only clears click; 0xE1 / 0xD9 only clear auto-repeat; 0x90 /
0xE3 only set auto-repeat.  Verified against the 8035 ROM disasm.

**Two-byte commands (prefix + second byte):**

| Byte 1 | Byte 2 | Hex        | Function                     |
|--------|--------|------------|------------------------------|
| 043    | 2XX    | 0x23 0xXX  | Sound enabled (+ volume)     |
| 033    | 2XX    | 0x1B 0xXX  | Keyclick enabled (+ volume)  |

**LED control (2-byte, mode + mask):**

The first byte is the mode (0o023 = ON, 0o021 = OFF), the second byte
is a mask selecting LEDs (bits 0–3 of byte OR'd with 0o200).  The ROM's
CALL 177042 sends R3 low byte first: e.g. R3=0x8413 → sends 0x13 (ON),
then 0x84 (CapsLock).

When no valid mask follows (second byte outside 0o200–0o217), the mode
byte acts as a standalone Latin indicator command: 0o021 = Latin
indicator ON, 0o023 = Latin indicator OFF.

| Mask bit | LED indicator    | Mask byte (oct) |
|----------|------------------|-----------------|
| 0        | Ожидание (Wait)  | 201             |
| 1        | Композиция       | 202             |
| 2        | Фиксация (Caps)  | 204             |
| 3        | Стоп-кадр (Hold) | 210             |

Multiple bits may be set simultaneously (e.g. 0o203 = Wait + Compose).

**Keyboard-generated error:**

| Code (oct) | Function                                           |
|------------|----------------------------------------------------|
| 266        | Input error — second byte not received within 100ms|

## Sources

- Intel 8251 USART datasheet (AFN-01819B)
- NS4 technical description (3.858.420 TO), section 4.10.1, Table 12
- MS 7004 ТО (technical description), Tables 1–3
