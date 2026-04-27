/*
 * ms7004_fw.c — firmware-driven MS 7004 keyboard backend.
 * See ms7004_fw.h and docs/kb/MS7004_WIRING.md.
 *
 * Wiring summary:
 *
 *   P1 [out latch]  — driven freely by firmware; read-back returns the
 *                     latch since the firmware never IN-A,P1's anyway
 *                     (confirmed by ROM scan).  Bit 7 is the TX line
 *                     watched by the TX reassembler.
 *   P2 [out latch]  — high nibble are LEDs (latch passthrough); low
 *                     nibble doubles as the i8243 cmd/data bus and is
 *                     mirrored into the expander on every write.
 *   PROG            — strobes the expander on every MOVD/ANLD/ORLD.
 *   T1              — when P1[4]=0 (STROBE asserted), returns the
 *                     latched key bit; otherwise 0.  The latch itself
 *                     is recomputed on every i8243 column write.
 *   T0, BUS         — unused: T0 returns 0, BUS reads return 0xFF.
 *   !INT            — driven by the RX bit driver: low (asserted) for
 *                     each "0" bit window during a host-to-keyboard
 *                     transfer, high (idle) otherwise.  Edge to low
 *                     trips the i8035 external IRQ vector at 003H.
 *
 * Serial timing: 4800 baud at 4.608 MHz / 15 = 64 instruction cycles
 * per bit, 1 start + 8 data (LSB first) + 1 stop bit per byte.  Both
 * the TX reassembler and RX driver run in 64-cycle bit slots; the TX
 * sample point is at +96 cycles after a falling edge (mid of bit 0)
 * then every +64 thereafter.
 */

#include <ms0515/ms7004_fw.h>
#include <ms0515/keyboard.h>

#include <string.h>

/* ── Serial timing constants ─────────────────────────────────────────── */

/* 4.608 MHz oscillator / 15 cyc per machine cycle = 307 200 cyc/sec.
 * 307 200 / 4800 baud = 64 cycles per bit. */
#define BIT_CYCLES         64

/* Sample point inside a bit cell: half a bit time after the bit's
 * leading edge.  For start-bit detection we add a full bit time on
 * top to skip past the start bit and land in the middle of bit 0. */
#define FIRST_SAMPLE_DELAY (BIT_CYCLES + BIT_CYCLES / 2)

/* ── i8035 callbacks ─────────────────────────────────────────────────── */

static uint8_t cb_port_read(void *ctx, uint8_t port)
{
    ms7004_fw_t *fw = (ms7004_fw_t *)ctx;
    switch (port) {
    case I8035_PORT_BUS: return 0xFF;
    case I8035_PORT_P1:  return fw->cpu.p1_out;
    case I8035_PORT_P2:  return fw->cpu.p2_out;
    }
    return 0xFF;
}

static void cb_port_write(void *ctx, uint8_t port, uint8_t val)
{
    ms7004_fw_t *fw = (ms7004_fw_t *)ctx;
    /*
     * P1 changes will eventually feed the TX bit reassembler (phase 3c).
     * P2's low nibble must be mirrored into the i8243 so a falling PROG
     * latches the right cmd/port — the i8035 core also calls this
     * callback before raising/lowering PROG for MOVD instructions, so
     * the expander always sees the up-to-date data nibble.
     */
    if (port == I8035_PORT_P2) {
        i8243_p2_write(&fw->exp, (uint8_t)(val & 0x0F));
    }
    (void)val;
}

static bool cb_read_t0(void *ctx) { (void)ctx; return false; }

static bool cb_read_t1(void *ctx)
{
    ms7004_fw_t *fw = (ms7004_fw_t *)ctx;
    /* MAME: `if (!BIT(m_p1, 4)) return m_keylatch; else return 0;` */
    if ((fw->cpu.p1_out & 0x10) == 0)
        return fw->keylatch;
    return false;
}

static bool cb_read_int(void *ctx)
{
    /* Active low: the i8035 IRQ pin reads `true` when the wire is
     * being held low (asserted).  rx_int_low tracks the bit currently
     * being driven by the RX state machine; idle is false (high). */
    return ((ms7004_fw_t *)ctx)->rx_int_low;
}

static void cb_prog(void *ctx, bool level)
{
    ms7004_fw_t *fw = (ms7004_fw_t *)ctx;
    i8243_prog(&fw->exp, level);
    /* On the rising edge with a WRITE command (cmd=01), the new data
     * nibble has just been latched into exp->latch[port] — that's the
     * one-hot column-select bit driven on this strobe.  Recompute the
     * keylatch for any subsequent T1 read.
     *
     * MAME guards on `if (data)` — writing 0 to deselect leaves the
     * previous keylatch intact.  This is how the firmware's scan
     * sequence (MOVD Pp,#one-hot ; MOVD Pp,#0 ; sample T1) works:
     * the deselect strobe must not clear what we sensed.
     *
     * On the falling edge p2_low is the COMMAND nibble — reading it
     * there gives garbage; we only act on rising edges. */
    if (level && fw->exp.cmd == 1) {
        uint8_t data = fw->exp.latch[fw->exp.port];
        int col = -1;
        switch (data) {
        case 0x01: col = (fw->exp.port << 2) + 0; break;
        case 0x02: col = (fw->exp.port << 2) + 1; break;
        case 0x04: col = (fw->exp.port << 2) + 2; break;
        case 0x08: col = (fw->exp.port << 2) + 3; break;
        default:   return;                          /* unchanged */
        }
        int row = fw->cpu.p1_out & 7;
        fw->keylatch = ((fw->matrix[col] >> row) & 1) != 0;
    }
}

/* ── Lifecycle ───────────────────────────────────────────────────────── */

void ms7004_fw_init(ms7004_fw_t *fw,
                    const uint8_t *rom, uint16_t rom_size,
                    struct ms0515_keyboard *uart)
{
    memset(fw, 0, sizeof(*fw));
    fw->uart = uart;
    i8243_init(&fw->exp);
    i8035_init(&fw->cpu, rom, rom_size, fw,
               cb_port_read, cb_port_write,
               cb_read_t0, cb_read_t1, cb_read_int,
               cb_prog);
    i8035_reset(&fw->cpu);
}

void ms7004_fw_reset(ms7004_fw_t *fw)
{
    /* Re-init clears every ephemeral field — TX/RX state, history,
     * matrix, keylatch — without disturbing the ROM binding. */
    const uint8_t *rom = fw->cpu.rom;
    uint16_t rom_size  = fw->cpu.rom_size;
    struct ms0515_keyboard *uart = fw->uart;
    ms7004_fw_init(fw, rom, rom_size, uart);
}

void ms7004_fw_press(ms7004_fw_t *fw, int col, int row, bool down)
{
    if (col < 0 || col >= 16 || row < 0 || row >= 8) return;
    uint8_t mask = (uint8_t)(1u << row);
    if (down) fw->matrix[col] |= mask;
    else      fw->matrix[col] &= (uint8_t)~mask;
}

void ms7004_fw_send_host_byte(ms7004_fw_t *fw, uint8_t byte)
{
    int next_tail = (fw->rx_q_tail + 1) % MS7004_RX_QUEUE_SIZE;
    if (next_tail == fw->rx_q_head) return;          /* queue full */
    fw->rx_queue[fw->rx_q_tail] = byte;
    fw->rx_q_tail = next_tail;
}

/* ── TX reassembler ──────────────────────────────────────────────────── */

static void tx_push_assembled(ms7004_fw_t *fw, uint8_t byte)
{
    if (fw->tx_history_count < MS7004_TX_HISTORY_SIZE) {
        fw->tx_history[fw->tx_history_count++] = byte;
    }
    /* In production a real ms0515_keyboard receives the byte too. */
    if (fw->uart) {
        kbd_push_scancode(fw->uart, byte);
    }
}

static void tx_advance(ms7004_fw_t *fw, int cycles)
{
    bool now_bit7 = (fw->cpu.p1_out & 0x80) != 0;

    if (fw->tx_state == MS7004_TX_IDLE) {
        /* Looking for the falling edge that marks the start bit. */
        if (fw->tx_last_bit7 && !now_bit7) {
            fw->tx_state            = MS7004_TX_SAMPLING;
            fw->tx_cycles_to_sample = FIRST_SAMPLE_DELAY;
            fw->tx_bit_index        = 0;
            fw->tx_byte             = 0;
        }
    } else {
        fw->tx_cycles_to_sample -= cycles;
        while (fw->tx_state != MS7004_TX_IDLE &&
               fw->tx_cycles_to_sample <= 0) {
            if (fw->tx_state == MS7004_TX_SAMPLING) {
                /* Sample the line at the bit's mid-point. LSB first. */
                if (now_bit7)
                    fw->tx_byte |= (uint8_t)(1u << fw->tx_bit_index);
                fw->tx_bit_index++;
                if (fw->tx_bit_index >= 8) {
                    /* Schedule a stop-bit validation one bit time later.
                     * Real UART hardware would frame-error a byte whose
                     * stop bit isn't high; we use the same check to
                     * discard glitches like the boot-time speaker beep
                     * that toggles P1 with bit 7 happening to be low. */
                    fw->tx_state            = MS7004_TX_STOP_CHECK;
                    fw->tx_cycles_to_sample += BIT_CYCLES;
                } else {
                    fw->tx_cycles_to_sample += BIT_CYCLES;
                }
            } else { /* MS7004_TX_STOP_CHECK */
                if (now_bit7) {
                    tx_push_assembled(fw, fw->tx_byte);
                }
                fw->tx_state = MS7004_TX_IDLE;
            }
        }
    }
    fw->tx_last_bit7 = now_bit7;
}

/* ── RX driver ───────────────────────────────────────────────────────── */

static void rx_set_data_bit(ms7004_fw_t *fw)
{
    /* UART data is LSB first; serial logic 0 → !INT asserted (low). */
    bool bit_value = ((fw->rx_byte >> fw->rx_bit_index) & 1) != 0;
    fw->rx_int_low = !bit_value;
}

static void rx_advance(ms7004_fw_t *fw, int cycles)
{
    if (fw->rx_state == MS7004_RX_IDLE) {
        /* Try to start a new byte. */
        if (fw->rx_q_head != fw->rx_q_tail) {
            fw->rx_byte             = fw->rx_queue[fw->rx_q_head];
            fw->rx_q_head           = (fw->rx_q_head + 1) % MS7004_RX_QUEUE_SIZE;
            fw->rx_state            = MS7004_RX_START;
            fw->rx_cycles_to_event  = BIT_CYCLES;
            fw->rx_int_low          = true;          /* start bit (low) */
        } else {
            fw->rx_int_low = false;                  /* line idles high */
        }
        return;
    }
    fw->rx_cycles_to_event -= cycles;
    while (fw->rx_state != MS7004_RX_IDLE &&
           fw->rx_cycles_to_event <= 0) {
        switch (fw->rx_state) {
        case MS7004_RX_START:
            fw->rx_state           = MS7004_RX_DATA;
            fw->rx_bit_index       = 0;
            rx_set_data_bit(fw);
            fw->rx_cycles_to_event += BIT_CYCLES;
            break;
        case MS7004_RX_DATA:
            fw->rx_bit_index++;
            if (fw->rx_bit_index >= 8) {
                fw->rx_state           = MS7004_RX_STOP;
                fw->rx_int_low         = false;       /* stop bit (high) */
                fw->rx_cycles_to_event += BIT_CYCLES;
            } else {
                rx_set_data_bit(fw);
                fw->rx_cycles_to_event += BIT_CYCLES;
            }
            break;
        case MS7004_RX_STOP:
            fw->rx_state           = MS7004_RX_IDLE;
            fw->rx_int_low         = false;
            return;                                  /* let next call peek queue */
        default:
            return;
        }
    }
}

int ms7004_fw_run_cycles(ms7004_fw_t *fw, int cycles)
{
    int spent = 0;
    while (spent < cycles) {
        int c = i8035_step(&fw->cpu);
        tx_advance(fw, c);
        rx_advance(fw, c);
        spent += c;
    }
    return spent;
}
