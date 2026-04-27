# MS7004 hardware wiring

Pin and matrix wiring derived from MAME `src/mame/shared/ms7004.cpp`
(commit at github.com/mamedev/mame, master).  The MAME source is read as
*data* — pin assignments and the key-matrix table — not as code; our
implementation is original.

## Hardware

- Intel 8035 CPU @ 4.608 MHz (one machine cycle = 15 osc periods →
  3.255 µs/cycle, 307 200 inst/s).
- Intel 8243 4-bit port expander on PROG line, command/data via P2[3:0].
- 2 KB external program ROM (`mc7004_keyboard_original.rom`,
  CRC32 69fcab53, SHA1 2d7cc7cd182f2ee09ecf2c539e33db3c2195f778).
- No external RAM, no XRAM access.

## CPU port pin map

### Port 1 (output)

| Bit | Function                                              |
|-----|-------------------------------------------------------|
|  0  | Matrix row select bit 0                               |
|  1  | Matrix row select bit 1                               |
|  2  | Matrix row select bit 2  (3-bit row addr → 8 rows)    |
|  3  | Speaker                                               |
|  4  | !STROBE — when LOW, T1 reads keylatch; HIGH → T1 = 0  |
|  5  | LED "Latin"                                           |
|  6  | (unused)                                              |
|  7  | Serial TX out → host UART RX                          |

### Port 2 (output, low nibble doubles as 8243 cmd/data)

| Bit | Function                                              |
|-----|-------------------------------------------------------|
| 0..3| Column-select data routed to 8243 ports P4..P7        |
|  4  | LED "Wait"                                            |
|  5  | LED "Compose"                                         |
|  6  | LED "Caps"                                            |
|  7  | LED "Hold"                                            |

### Test-input pins

- **T1** — keyboard sense.  Returns the value latched by the 8243 column
  callback at P1[0..2] row, but only when P1[4]=0; otherwise 0.
- **T0** — unused.
- **!INT** (IRQ) — Serial RX in (host→keyboard).  Active low: when the
  host UART drives a logic-0 line, INT is asserted.  Firmware uses
  INT-driven receive for host commands.

### PROG

Drives the 8243 latch — falling edge captures cmd/port from
P2[3:0]; rising edge captures data for WRITE/ORLD/ANLD or ends the
READ window.  We already model this exactly in `i8035.c` + `i8243.c`.

## Matrix scan model

The key matrix is **16 columns × 8 rows**.  Each column is one byte;
bit `r` set means the key at (column, row=r) is held.

Firmware scan loop:
1. Pick a column.  Column index = `P*4 + bit_pos` where `P` is the 8243
   port (0..3, i.e. P4..P7) and `bit_pos` is the index of the single
   set bit (0..3) in the data nibble.  E.g. writing 0x04 to expander
   port 2 selects column `2*4 + 2 = 10`.
2. The 8243 callback computes `keylatch = bit(P1 & 7) of matrix[col]`.
3. Firmware then drives !STROBE low and reads T1 → that one bit.
4. Repeat for every (col, row) pair, building a press map in RAM.

Our wrapper exposes `matrix[16]` as bytes; on every `i8243_port_w`
event we precompute the cell that would be sensed and stash it for T1.

## Key → (column, row) table

Derived from MAME's `INPUT_PORTS_START(ms7004)`.  Column = KBD%u port
number; row = bit-position of the PORT_BIT mask.

```
col=0  (KBD0):  bit 2=F17,    bit 1=F18,    bit 0=PF2,    bit 3=PF3,
                bit 5=Num8,   bit 7=Num5,   bit 6=Num6,   bit 4=Num3
col=1  (KBD1):  bit 2=F16/Do, bit 0=PF1,    bit 3=Num7,   bit 5=Num4,
                bit 7=Num1,   bit 6=Num2,   bit 4=Num0
col=2  (KBD2):  bit 1=Remove, bit 0=Next,   bit 3=Prev,   bit 7=Right,
                bit 6=Down
col=3  (KBD3):  bit 2=Help,   bit 1=InsHere,bit 0=Find,   bit 3=Select,
                bit 5=Up,     bit 6=Left
col=4  (KBD4):  bit 2=F19,    bit 1=F20,    bit 0=PF4,    bit 3=Num9,
                bit 5=Num',   bit 7=Num-,   bit 6=Num.,   bit 4=NumEnter
col=5  (KBD5):  bit 2=F14,    bit 0=Delete<X (BS),         bit 5=Return,
                bit 6=RShift
col=6  (KBD6):  bit 2=F12/BS, bit 1=F13/LF, bit 5=":*",   bit 6=".>",
                bit 4="_"
col=7  (KBD7):  bit 1=F11/ESC,bit 0='0',    bit 3="-=",   bit 5=H,
                bit 7=V,      bit 6="\\|",   bit 4="/?"
col=8  (KBD8):  bit 2=F10/Exit,bit 1='9',   bit 0='8',    bit 3=Z,
                bit 5=D,      bit 7=L,      bit 6="@`",   bit 4=",<"
col=9  (KBD9):  bit 2=F8/Cancel,bit 1=F9/MainScreen,bit 0='7',bit 3='[',
                bit 5=']',    bit 7=O,      bit 6=X,      bit 4=B
col=10 (KBD10): bit 2=F7/Resume,bit 1='6',  bit 0=N,      bit 3=G,
                bit 5=R,      bit 7=T,      bit 6=I,      bit 4=Space
col=11 (KBD11): bit 2=F6/Interrupt,bit 1='5',bit 0=E,     bit 3=P,
                bit 5=A,      bit 7=M
col=12 (KBD12): bit 2=F2,     bit 1=F1,     bit 3=";+",   bit 5=Tab,
                bit 7=LCtrl,  bit 6=ShiftLock,bit 4=LShift
col=13 (KBD13): bit 2=F3/Setup,bit 1=F4/Talk,bit 0='1',   bit 3=J,
                bit 5=C,      bit 7=F,      bit 6=Rus/Lat,bit 4=Compose
col=14 (KBD14): bit 2=F5/Break,bit 0='2',   bit 3=U,      bit 5=Y,
                bit 7="^~",   bit 6=Q
col=15 (KBD15): bit 1='3',    bit 0='4',    bit 3=K,      bit 5=W,
                bit 7=S
```

(Several positions are marked IPT_UNUSED in MAME — listed there with
captions like "{|", "}", "_ sends LF" — they map to physical caps that
exist on the keyboard but the firmware reads them as fixed scancodes
or doesn't bind them.)

## Serial protocol

- 4800 baud, 8N1 (start bit, 8 data bits LSB-first, 1 stop bit).
- 4.608 MHz / 15 / 4800 = **64 instruction cycles per bit time**.
- TX: firmware bit-bangs P1[7].  Host samples on falling edge of start.
- RX: host drives !INT.  Falling edge fires the 8035's external IRQ;
  firmware reads further bits by polling INT level inside the ISR.

For our wrapper we'll model:
- A small bit-timer on the host side that snapshots P1[7] at 64-cycle
  intervals once a start bit (1→0) is seen, assembles 8 bits, and pushes
  the byte to `kbd_push_scancode`.
- For host→keyboard: drive INT low for one bit-time per data bit at
  the same 64-cycle cadence, starting with the start bit.

(Concrete RX timing has to be verified by disassembling the firmware's
ISR — phase 3a-2.)
