/*
 * ms7004.c — Elektronika MS 7004 keyboard, firmware-driven backend.
 *
 * Runs the real 2 KB keyboard firmware ROM on top of i8035 + i8243
 * cores.  The previous hand-rolled state machine (LK201-style press
 * tracking, ALL-UP synthesis, OSK case overrides) is gone — it lives
 * in git history if anyone needs it back.  Real firmware behaviour is
 * the new ground truth; OSK behavioural overrides moved into the
 * frontend on-screen-keyboard handler in phase 3d-final.
 *
 * Wiring summary (see docs/kb/MS7004_WIRING.md for the full spec):
 *
 *   P1 [out latch]  — read-back returns the latch since the firmware
 *                     never IN-A,P1's anyway (confirmed by ROM scan).
 *                     Bit 7 is the TX line watched by the reassembler.
 *   P2 [out latch]  — high nibble are LEDs (latch passthrough); low
 *                     nibble doubles as the i8243 cmd/data bus and is
 *                     mirrored into the expander on every write.
 *   PROG            — strobes the expander on every MOVD/ANLD/ORLD.
 *   T1              — when P1[4]=0 (STROBE asserted), returns the
 *                     latched key bit; otherwise 0.  The latch itself
 *                     is recomputed on every i8243 column write.
 *   T0, BUS         — unused: T0 returns 0, BUS reads return 0xFF.
 *   !INT            — driven by the RX bit driver (active-low: low
 *                     for each "0" bit window during a host-to-keyboard
 *                     transfer, high otherwise).  Edge to low trips
 *                     the i8035 external IRQ vector at 003H.
 *
 * Serial timing: 4800 baud at 4.608 MHz / 15 = 64 instruction cycles
 * per bit, 1 start + 8 data (LSB first) + 1 stop bit per byte.
 */

#include <ms0515/ms7004.h>
#include <ms0515/keyboard.h>

#include <string.h>

/* ── Serial timing constants ─────────────────────────────────────────── */

#define BIT_CYCLES         64
#define FIRST_SAMPLE_DELAY (BIT_CYCLES + BIT_CYCLES / 2)

/* Well-known scancodes the firmware emits for the toggle keys. */
#define SC_CAPS    0260
#define SC_RUSLAT  0262

/* ── Canonical scancode lookup ───────────────────────────────────────── */

/*
 * LK201-derived scancode for each enum value.  Used by the public
 * ms7004_scancode() helper for tests/diagnostics; the firmware emits
 * these same bytes for plain make-codes (verified against six
 * representative keys — see phase 3d-prep journal entry).
 */
static const uint8_t kScancode[MS7004_KEY__COUNT] = {
    [MS7004_KEY_NONE]            = 0,

    [MS7004_KEY_F1]              = 0126,
    [MS7004_KEY_F2]              = 0127,
    [MS7004_KEY_F3]              = 0130,
    [MS7004_KEY_F4]              = 0131,
    [MS7004_KEY_F5]              = 0132,
    [MS7004_KEY_F6]              = 0144,
    [MS7004_KEY_F7]              = 0145,
    [MS7004_KEY_F8]              = 0146,
    [MS7004_KEY_F9]              = 0147,
    [MS7004_KEY_F10]             = 0150,
    [MS7004_KEY_F11]             = 0161,
    [MS7004_KEY_F12]             = 0162,
    [MS7004_KEY_F13]             = 0163,
    [MS7004_KEY_F14]             = 0164,
    [MS7004_KEY_HELP]            = 0174,
    [MS7004_KEY_PERFORM]         = 0175,
    [MS7004_KEY_F17]             = 0200,
    [MS7004_KEY_F18]             = 0201,
    [MS7004_KEY_F19]             = 0202,
    [MS7004_KEY_F20]             = 0203,

    [MS7004_KEY_LBRACE_PIPE]     = 0374,
    [MS7004_KEY_SEMI_PLUS]       = 0277,
    [MS7004_KEY_1]               = 0300,
    [MS7004_KEY_2]               = 0305,
    [MS7004_KEY_3]               = 0313,
    [MS7004_KEY_4]               = 0320,
    [MS7004_KEY_5]               = 0325,
    [MS7004_KEY_6]               = 0333,
    [MS7004_KEY_7]               = 0340,
    [MS7004_KEY_8]               = 0345,
    [MS7004_KEY_9]               = 0352,
    [MS7004_KEY_0]               = 0357,
    [MS7004_KEY_MINUS_EQ]        = 0371,
    [MS7004_KEY_RBRACE_LEFTUP]   = 0365,
    [MS7004_KEY_BS]              = 0274,

    [MS7004_KEY_TAB]             = 0276,
    [MS7004_KEY_J]               = 0301,
    [MS7004_KEY_C]               = 0306,
    [MS7004_KEY_U]               = 0314,
    [MS7004_KEY_K]               = 0321,
    [MS7004_KEY_E]               = 0327,
    [MS7004_KEY_N]               = 0334,
    [MS7004_KEY_G]               = 0341,
    [MS7004_KEY_LBRACKET]        = 0346,
    [MS7004_KEY_RBRACKET]        = 0353,
    [MS7004_KEY_Z]               = 0360,
    [MS7004_KEY_H]               = 0366,
    [MS7004_KEY_COLON_STAR]      = 0372,
    [MS7004_KEY_TILDE]           = 0304,
    [MS7004_KEY_RETURN]          = 0275,

    [MS7004_KEY_CTRL]            = 0257,
    [MS7004_KEY_CAPS]            = SC_CAPS,
    [MS7004_KEY_F]               = 0302,
    [MS7004_KEY_Y]               = 0307,
    [MS7004_KEY_W]               = 0315,
    [MS7004_KEY_A]               = 0322,
    [MS7004_KEY_P]               = 0330,
    [MS7004_KEY_R]               = 0335,
    [MS7004_KEY_O]               = 0342,
    [MS7004_KEY_L]               = 0347,
    [MS7004_KEY_D]               = 0354,
    [MS7004_KEY_V]               = 0362,
    [MS7004_KEY_BACKSLASH]       = 0373,
    [MS7004_KEY_PERIOD]          = 0367,
    [MS7004_KEY_HARDSIGN]        = 0361,

    [MS7004_KEY_SHIFT_L]         = 0256,
    [MS7004_KEY_RUSLAT]          = SC_RUSLAT,
    [MS7004_KEY_Q]               = 0303,
    [MS7004_KEY_CHE]             = 0310,
    [MS7004_KEY_S]               = 0316,
    [MS7004_KEY_M]               = 0323,
    [MS7004_KEY_I]               = 0331,
    [MS7004_KEY_T]               = 0336,
    [MS7004_KEY_X]               = 0343,
    [MS7004_KEY_B]               = 0350,
    [MS7004_KEY_AT]              = 0355,
    [MS7004_KEY_COMMA]           = 0363,
    [MS7004_KEY_SLASH]           = 0312,
    [MS7004_KEY_UNDERSCORE]      = 0361,
    [MS7004_KEY_SHIFT_R]         = 0256,

    [MS7004_KEY_COMPOSE]         = 0261,
    [MS7004_KEY_SPACE]           = 0324,
    [MS7004_KEY_KP0_WIDE]        = 0222,
    [MS7004_KEY_KP_ENTER]        = 0225,

    [MS7004_KEY_FIND]            = 0212,
    [MS7004_KEY_INSERT]          = 0213,
    [MS7004_KEY_REMOVE]          = 0214,
    [MS7004_KEY_SELECT]          = 0215,
    [MS7004_KEY_PREV]            = 0216,
    [MS7004_KEY_NEXT]            = 0217,

    [MS7004_KEY_UP]              = 0252,
    [MS7004_KEY_DOWN]            = 0251,
    [MS7004_KEY_LEFT]            = 0247,
    [MS7004_KEY_RIGHT]           = 0250,

    [MS7004_KEY_PF1]             = 0241,
    [MS7004_KEY_PF2]             = 0242,
    [MS7004_KEY_PF3]             = 0243,
    [MS7004_KEY_PF4]             = 0244,

    [MS7004_KEY_KP_1]            = 0226,
    [MS7004_KEY_KP_2]            = 0227,
    [MS7004_KEY_KP_3]            = 0230,
    [MS7004_KEY_KP_4]            = 0231,
    [MS7004_KEY_KP_5]            = 0232,
    [MS7004_KEY_KP_6]            = 0233,
    [MS7004_KEY_KP_7]            = 0235,
    [MS7004_KEY_KP_8]            = 0236,
    [MS7004_KEY_KP_9]            = 0237,
    [MS7004_KEY_KP_DOT]          = 0224,
    [MS7004_KEY_KP_COMMA]        = 0234,
    [MS7004_KEY_KP_MINUS]        = 0240,
};

/* ── Key enum → matrix coords ──────────────────────────────────────────
 *
 * Built from MAME's INPUT_PORTS_START(ms7004) — see
 * docs/kb/MS7004_WIRING.md for the full table.  Entries are
 * {col, row}; (-1, -1) means the enum cap exists in the user-facing
 * key set but has no matrix position on the real keyboard (MAME marks
 * those IPT_UNUSED).
 */
typedef struct { int8_t col; int8_t row; } key_xy_t;

static const key_xy_t kKeyMatrix[MS7004_KEY__COUNT] = {
    [MS7004_KEY_NONE]            = {-1,-1},

    [MS7004_KEY_F1]              = {12, 1},
    [MS7004_KEY_F2]              = {12, 2},
    [MS7004_KEY_F3]              = {13, 2},
    [MS7004_KEY_F4]              = {13, 1},
    [MS7004_KEY_F5]              = {14, 2},
    [MS7004_KEY_F6]              = {11, 2},
    [MS7004_KEY_F7]              = {10, 2},
    [MS7004_KEY_F8]              = { 9, 2},
    [MS7004_KEY_F9]              = { 9, 1},
    [MS7004_KEY_F10]             = { 8, 2},
    [MS7004_KEY_F11]             = { 7, 1},
    [MS7004_KEY_F12]             = { 6, 2},
    [MS7004_KEY_F13]             = { 6, 1},
    [MS7004_KEY_F14]             = { 5, 2},
    [MS7004_KEY_HELP]            = { 3, 2},
    [MS7004_KEY_PERFORM]         = { 1, 2},
    [MS7004_KEY_F17]             = { 0, 2},
    [MS7004_KEY_F18]             = { 0, 1},
    [MS7004_KEY_F19]             = { 4, 2},
    [MS7004_KEY_F20]             = { 4, 1},

    [MS7004_KEY_LBRACE_PIPE]     = {-1,-1},
    [MS7004_KEY_SEMI_PLUS]       = {12, 3},
    [MS7004_KEY_1]               = {13, 0},
    [MS7004_KEY_2]               = {14, 0},
    [MS7004_KEY_3]               = {15, 1},
    [MS7004_KEY_4]               = {15, 0},
    [MS7004_KEY_5]               = {11, 1},
    [MS7004_KEY_6]               = {10, 1},
    [MS7004_KEY_7]               = { 9, 0},
    [MS7004_KEY_8]               = { 8, 0},
    [MS7004_KEY_9]               = { 8, 1},
    [MS7004_KEY_0]               = { 7, 0},
    [MS7004_KEY_MINUS_EQ]        = { 7, 3},
    [MS7004_KEY_RBRACE_LEFTUP]   = {-1,-1},
    [MS7004_KEY_BS]              = { 5, 0},

    [MS7004_KEY_TAB]             = {12, 5},
    [MS7004_KEY_J]               = {13, 3},
    [MS7004_KEY_C]               = {13, 5},
    [MS7004_KEY_U]               = {14, 3},
    [MS7004_KEY_K]               = {15, 3},
    [MS7004_KEY_E]               = {11, 0},
    [MS7004_KEY_N]               = {10, 0},
    [MS7004_KEY_G]               = {10, 3},
    [MS7004_KEY_LBRACKET]        = { 9, 3},
    [MS7004_KEY_RBRACKET]        = { 9, 5},
    [MS7004_KEY_Z]               = { 8, 3},
    [MS7004_KEY_H]               = { 7, 5},
    [MS7004_KEY_COLON_STAR]      = { 6, 5},
    [MS7004_KEY_TILDE]           = {14, 7},
    [MS7004_KEY_RETURN]          = { 5, 5},

    [MS7004_KEY_CTRL]            = {12, 7},
    [MS7004_KEY_CAPS]            = {12, 6},
    [MS7004_KEY_F]               = {13, 7},
    [MS7004_KEY_Y]               = {14, 5},
    [MS7004_KEY_W]               = {15, 5},
    [MS7004_KEY_A]               = {11, 5},
    [MS7004_KEY_P]               = {11, 3},
    [MS7004_KEY_R]               = {10, 5},
    [MS7004_KEY_O]               = { 9, 7},
    [MS7004_KEY_L]               = { 8, 7},
    [MS7004_KEY_D]               = { 8, 5},
    [MS7004_KEY_V]               = { 7, 7},
    [MS7004_KEY_BACKSLASH]       = { 7, 6},
    [MS7004_KEY_PERIOD]          = { 6, 6},
    [MS7004_KEY_HARDSIGN]        = {-1,-1},

    [MS7004_KEY_SHIFT_L]         = {12, 4},
    [MS7004_KEY_RUSLAT]          = {13, 6},
    [MS7004_KEY_Q]               = {14, 6},
    [MS7004_KEY_CHE]             = {-1,-1},
    [MS7004_KEY_S]               = {15, 7},
    [MS7004_KEY_M]               = {11, 7},
    [MS7004_KEY_I]               = {10, 6},
    [MS7004_KEY_T]               = {10, 7},
    [MS7004_KEY_X]               = { 9, 6},
    [MS7004_KEY_B]               = { 9, 4},
    [MS7004_KEY_AT]              = { 8, 6},
    [MS7004_KEY_COMMA]           = { 8, 4},
    [MS7004_KEY_SLASH]           = { 7, 4},
    [MS7004_KEY_UNDERSCORE]      = { 6, 4},
    [MS7004_KEY_SHIFT_R]         = { 5, 6},

    [MS7004_KEY_COMPOSE]         = {13, 4},
    [MS7004_KEY_SPACE]           = {10, 4},
    [MS7004_KEY_KP0_WIDE]        = {-1,-1},
    [MS7004_KEY_KP_ENTER]        = { 4, 4},

    [MS7004_KEY_FIND]            = { 3, 0},
    [MS7004_KEY_INSERT]          = { 3, 1},
    [MS7004_KEY_REMOVE]          = { 2, 1},
    [MS7004_KEY_SELECT]          = { 3, 3},
    [MS7004_KEY_PREV]            = { 2, 3},
    [MS7004_KEY_NEXT]            = { 2, 0},

    [MS7004_KEY_UP]              = { 3, 5},
    [MS7004_KEY_DOWN]            = { 2, 6},
    [MS7004_KEY_LEFT]            = { 3, 6},
    [MS7004_KEY_RIGHT]           = { 2, 7},

    [MS7004_KEY_PF1]             = { 1, 0},
    [MS7004_KEY_PF2]             = { 0, 0},
    [MS7004_KEY_PF3]             = { 0, 3},
    [MS7004_KEY_PF4]             = { 4, 0},

    [MS7004_KEY_KP_1]            = { 1, 7},
    [MS7004_KEY_KP_2]            = { 1, 6},
    [MS7004_KEY_KP_3]            = { 0, 4},
    [MS7004_KEY_KP_4]            = { 1, 5},
    [MS7004_KEY_KP_5]            = { 0, 7},
    [MS7004_KEY_KP_6]            = { 0, 6},
    [MS7004_KEY_KP_7]            = { 1, 3},
    [MS7004_KEY_KP_8]            = { 0, 5},
    [MS7004_KEY_KP_9]            = { 4, 3},
    [MS7004_KEY_KP_DOT]          = { 4, 6},
    [MS7004_KEY_KP_COMMA]        = { 4, 5},
    [MS7004_KEY_KP_MINUS]        = { 4, 7},
};

static bool key_valid(ms7004_key_t k)
{
    return k > MS7004_KEY_NONE && k < MS7004_KEY__COUNT;
}

/* ── i8035 host callbacks ───────────────────────────────────────────── */

static uint8_t cb_port_read(void *ctx, uint8_t port)
{
    ms7004_t *kbd = (ms7004_t *)ctx;
    switch (port) {
    case I8035_PORT_BUS: return 0xFF;
    case I8035_PORT_P1:  return kbd->cpu.p1_out;
    case I8035_PORT_P2:  return kbd->cpu.p2_out;
    }
    return 0xFF;
}

static void cb_port_write(void *ctx, uint8_t port, uint8_t val)
{
    ms7004_t *kbd = (ms7004_t *)ctx;
    /* P2's low nibble must be mirrored into the i8243 so a falling
     * PROG latches the right cmd/port. */
    if (port == I8035_PORT_P2) {
        i8243_p2_write(&kbd->exp, (uint8_t)(val & 0x0F));
    }
    (void)val;
}

static bool cb_read_t0(void *ctx) { (void)ctx; return false; }

static bool cb_read_t1(void *ctx)
{
    ms7004_t *kbd = (ms7004_t *)ctx;
    /* MAME: `if (!BIT(m_p1, 4)) return m_keylatch; else return 0;` */
    if ((kbd->cpu.p1_out & 0x10) == 0)
        return kbd->keylatch;
    return false;
}

static bool cb_read_int(void *ctx)
{
    /* Active-low: the i8035 IRQ pin reads `true` when the wire is held
     * low (asserted).  rx_int_low tracks the bit currently being driven
     * by the RX state machine; idle is false (line high). */
    return ((ms7004_t *)ctx)->rx_int_low;
}

static void cb_prog(void *ctx, bool level)
{
    ms7004_t *kbd = (ms7004_t *)ctx;
    i8243_prog(&kbd->exp, level);
    /* On the rising edge with a WRITE command (cmd=01), the new data
     * nibble has just been latched into exp->latch[port].  Recompute
     * the keylatch using the addressed column and row.  Per MAME:
     * writing 0 to deselect leaves the previous keylatch intact. */
    if (level && kbd->exp.cmd == 1) {
        uint8_t data = kbd->exp.latch[kbd->exp.port];
        int col = -1;
        switch (data) {
        case 0x01: col = (kbd->exp.port << 2) + 0; break;
        case 0x02: col = (kbd->exp.port << 2) + 1; break;
        case 0x04: col = (kbd->exp.port << 2) + 2; break;
        case 0x08: col = (kbd->exp.port << 2) + 3; break;
        default:   return;
        }
        int row = kbd->cpu.p1_out & 7;
        kbd->keylatch = ((kbd->matrix[col] >> row) & 1) != 0;
    }
}

/* ── TX reassembler ──────────────────────────────────────────────────── */

static void tx_push_assembled(ms7004_t *kbd, uint8_t byte)
{
    if (kbd->tx_history_count < MS7004_TX_HISTORY_SIZE) {
        kbd->tx_history[kbd->tx_history_count++] = byte;
    }
    /* Observe toggle-key emissions for caps_on / ruslat_on queries. */
    if (byte == SC_CAPS)
        kbd->caps_on = !kbd->caps_on;
    else if (byte == SC_RUSLAT)
        kbd->ruslat_on = !kbd->ruslat_on;

    if (kbd->uart) {
        kbd_push_scancode(kbd->uart, byte);
    }
}

static void tx_advance(ms7004_t *kbd, int cycles)
{
    bool now_bit7 = (kbd->cpu.p1_out & 0x80) != 0;

    if (kbd->tx_state == MS7004_TX_IDLE) {
        if (kbd->tx_last_bit7 && !now_bit7) {
            kbd->tx_state            = MS7004_TX_SAMPLING;
            kbd->tx_cycles_to_sample = FIRST_SAMPLE_DELAY;
            kbd->tx_bit_index        = 0;
            kbd->tx_byte             = 0;
        }
    } else {
        kbd->tx_cycles_to_sample -= cycles;
        while (kbd->tx_state != MS7004_TX_IDLE &&
               kbd->tx_cycles_to_sample <= 0) {
            if (kbd->tx_state == MS7004_TX_SAMPLING) {
                if (now_bit7)
                    kbd->tx_byte |= (uint8_t)(1u << kbd->tx_bit_index);
                kbd->tx_bit_index++;
                if (kbd->tx_bit_index >= 8) {
                    kbd->tx_state             = MS7004_TX_STOP_CHECK;
                    kbd->tx_cycles_to_sample += BIT_CYCLES;
                } else {
                    kbd->tx_cycles_to_sample += BIT_CYCLES;
                }
            } else { /* MS7004_TX_STOP_CHECK */
                /* Real UART hardware would frame-error a byte whose
                 * stop bit isn't high; we use the same check to
                 * discard glitches like the boot speaker beep. */
                if (now_bit7) {
                    tx_push_assembled(kbd, kbd->tx_byte);
                }
                kbd->tx_state = MS7004_TX_IDLE;
            }
        }
    }
    kbd->tx_last_bit7 = now_bit7;
}

/* ── RX driver ───────────────────────────────────────────────────────── */

static void rx_set_data_bit(ms7004_t *kbd)
{
    bool bit_value = ((kbd->rx_byte >> kbd->rx_bit_index) & 1) != 0;
    kbd->rx_int_low = !bit_value;
}

static void rx_advance(ms7004_t *kbd, int cycles)
{
    if (kbd->rx_state == MS7004_RX_IDLE) {
        if (kbd->rx_q_head != kbd->rx_q_tail) {
            kbd->rx_byte             = kbd->rx_queue[kbd->rx_q_head];
            kbd->rx_q_head           = (kbd->rx_q_head + 1) % MS7004_RX_QUEUE_SIZE;
            kbd->rx_state            = MS7004_RX_START;
            kbd->rx_cycles_to_event  = BIT_CYCLES;
            kbd->rx_int_low          = true;
        } else {
            kbd->rx_int_low = false;
        }
        return;
    }
    kbd->rx_cycles_to_event -= cycles;
    while (kbd->rx_state != MS7004_RX_IDLE &&
           kbd->rx_cycles_to_event <= 0) {
        switch (kbd->rx_state) {
        case MS7004_RX_START:
            kbd->rx_state            = MS7004_RX_DATA;
            kbd->rx_bit_index        = 0;
            rx_set_data_bit(kbd);
            kbd->rx_cycles_to_event += BIT_CYCLES;
            break;
        case MS7004_RX_DATA:
            kbd->rx_bit_index++;
            if (kbd->rx_bit_index >= 8) {
                kbd->rx_state            = MS7004_RX_STOP;
                kbd->rx_int_low          = false;
                kbd->rx_cycles_to_event += BIT_CYCLES;
            } else {
                rx_set_data_bit(kbd);
                kbd->rx_cycles_to_event += BIT_CYCLES;
            }
            break;
        case MS7004_RX_STOP:
            kbd->rx_state    = MS7004_RX_IDLE;
            kbd->rx_int_low  = false;
            return;
        default:
            return;
        }
    }
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void ms7004_init(ms7004_t *kbd, struct ms0515_keyboard *uart)
{
    memset(kbd, 0, sizeof(*kbd));
    kbd->uart = uart;
    i8243_init(&kbd->exp);
    /* CPU stays uninitialised until ms7004_attach_firmware lands the
     * ROM blob.  Calls into ms7004_tick / ms7004_key before that point
     * are safe but no firmware runs (the matrix bits change but the
     * CPU has nothing to execute). */
}

void ms7004_attach_firmware(ms7004_t *kbd,
                            const uint8_t *rom, uint16_t rom_size)
{
    kbd->firmware_rom      = rom;
    kbd->firmware_rom_size = rom_size;
    i8035_init(&kbd->cpu, rom, rom_size, kbd,
               cb_port_read, cb_port_write,
               cb_read_t0, cb_read_t1, cb_read_int,
               cb_prog);
    i8035_reset(&kbd->cpu);
}

void ms7004_reset(ms7004_t *kbd)
{
    /* Re-init clears every ephemeral field (TX/RX, history, matrix,
     * keylatch, toggle states) without disturbing the ROM binding or
     * the UART pointer. */
    const uint8_t *rom = kbd->firmware_rom;
    uint16_t rom_size  = kbd->firmware_rom_size;
    struct ms0515_keyboard *uart = kbd->uart;
    ms7004_init(kbd, uart);
    if (rom)
        ms7004_attach_firmware(kbd, rom, rom_size);
}

/* ── Public input ────────────────────────────────────────────────────── */

void ms7004_key(ms7004_t *kbd, ms7004_key_t key, bool down)
{
    if (!key_valid(key)) return;
    key_xy_t xy = kKeyMatrix[key];
    if (xy.col < 0 || xy.row < 0) return;
    uint8_t mask = (uint8_t)(1u << xy.row);
    if (down) kbd->matrix[xy.col] |= mask;
    else      kbd->matrix[xy.col] &= (uint8_t)~mask;
}

void ms7004_release_all(ms7004_t *kbd)
{
    memset(kbd->matrix, 0, sizeof(kbd->matrix));
}

void ms7004_host_byte(ms7004_t *kbd, uint8_t byte)
{
    int next_tail = (kbd->rx_q_tail + 1) % MS7004_RX_QUEUE_SIZE;
    if (next_tail == kbd->rx_q_head) return;          /* queue full */
    kbd->rx_queue[kbd->rx_q_tail] = byte;
    kbd->rx_q_tail = next_tail;
}

/* ── Time tick → CPU cycle conversion ────────────────────────────────── */

void ms7004_tick(ms7004_t *kbd, uint32_t now_ms)
{
    if (!kbd->firmware_rom) return;             /* nothing to execute */

    if (!kbd->last_tick_valid) {
        kbd->last_tick_ms    = now_ms;
        kbd->last_tick_valid = true;
        return;
    }

    /* 4.608 MHz / 15 = 307.2 inst/ms.  Round to 307 for simplicity;
     * the tiny drift accumulates to <1 cycle every 5 ticks and never
     * matters for keyboard timing. */
    uint32_t delta_ms = now_ms - kbd->last_tick_ms;
    kbd->last_tick_ms = now_ms;
    if (delta_ms == 0) return;

    /* Cap a single-tick budget to avoid pathological catch-up on long
     * pauses (window minimised for 30 s shouldn't burn 9M instructions). */
    if (delta_ms > 100) delta_ms = 100;

    int budget = (int)(delta_ms * 307u);
    int spent  = 0;
    while (spent < budget) {
        int c = i8035_step(&kbd->cpu);
        tx_advance(kbd, c);
        rx_advance(kbd, c);
        spent += c;
    }
}

/* ── Queries ─────────────────────────────────────────────────────────── */

bool ms7004_caps_on  (const ms7004_t *kbd) { return kbd->caps_on; }
bool ms7004_ruslat_on(const ms7004_t *kbd) { return kbd->ruslat_on; }

bool ms7004_is_held(const ms7004_t *kbd, ms7004_key_t key)
{
    if (!key_valid(key)) return false;
    key_xy_t xy = kKeyMatrix[key];
    if (xy.col < 0 || xy.row < 0) return false;
    return ((kbd->matrix[xy.col] >> xy.row) & 1) != 0;
}

uint8_t ms7004_scancode(ms7004_key_t key)
{
    if (!key_valid(key)) return 0;
    return kScancode[key];
}
