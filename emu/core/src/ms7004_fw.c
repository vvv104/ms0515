/*
 * ms7004_fw.c — firmware-driven MS 7004 keyboard backend (phase 3b
 * skeleton).  See ms7004_fw.h and docs/kb/MS7004_WIRING.md.
 *
 * Wiring summary (phase 3b only; UART RX/TX land in 3c):
 *
 *   P1 [out latch]  — driven freely by firmware; read-back returns the
 *                     latch since the firmware never IN-A,P1's anyway
 *                     (confirmed by ROM scan).
 *   P2 [out latch]  — high nibble are LEDs (latch passthrough); low
 *                     nibble doubles as the i8243 cmd/data bus and is
 *                     mirrored into the expander on every write.
 *   PROG            — strobes the expander on every MOVD/ANLD/ORLD.
 *   T1              — when P1[4]=0 (STROBE asserted), returns the
 *                     latched key bit; otherwise 0.  The latch itself
 *                     is recomputed on every i8243 column write.
 *   T0, INT, BUS    — unused in phase 3b: T0 returns 0, INT idles
 *                     high (no host data), BUS reads return 0xFF.
 */

#include <ms0515/ms7004_fw.h>

#include <string.h>

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

static bool cb_read_int(void *ctx) { (void)ctx; return false; }   /* idle high */

static void cb_prog(void *ctx, bool level)
{
    ms7004_fw_t *fw = (ms7004_fw_t *)ctx;
    i8243_prog(&fw->exp, level);
    /* On the falling edge with a WRITE command (cmd=01), the column-
     * select nibble has been latched — recompute the keylatch for the
     * next T1 read.  ORLD/ANLD/READ commands don't pick a single
     * column for sensing, so we only act on WRITE.
     *
     * The MAME callback computes sense from the addressed column and
     * indexes by the row bits of P1[2..0].  Our matrix[c] encodes one
     * byte per column, bit r set if (col=c, row=r) is pressed; we just
     * pick the bit. */
    if (!level && fw->exp.cmd == 1) {
        /* Column index = expander_port * 4 + bit_pos_of_set_bit_in_data.
         * The data nibble was driven on the previous P2 write, then the
         * i8035 lowered PROG; the expander has now latched (cmd, port)
         * but the data nibble is what's currently on P2[3:0].  The
         * MAME model uses the data nibble as a one-hot bit: 0x01..0x08.
         */
        uint8_t data = fw->exp.p2_low;
        int col = -1;
        switch (data) {
        case 0x01: col = (fw->exp.port << 2) + 0; break;
        case 0x02: col = (fw->exp.port << 2) + 1; break;
        case 0x04: col = (fw->exp.port << 2) + 2; break;
        case 0x08: col = (fw->exp.port << 2) + 3; break;
        default:   fw->keylatch = false;          return;
        }
        int row = fw->cpu.p1_out & 7;
        fw->keylatch = (fw->matrix[col] >> row) & 1;
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
    i8035_reset(&fw->cpu);
    i8243_reset(&fw->exp);
    memset(fw->matrix, 0, sizeof(fw->matrix));
    fw->keylatch = false;
}

void ms7004_fw_press(ms7004_fw_t *fw, int col, int row, bool down)
{
    if (col < 0 || col >= 16 || row < 0 || row >= 8) return;
    uint8_t mask = (uint8_t)(1u << row);
    if (down) fw->matrix[col] |= mask;
    else      fw->matrix[col] &= (uint8_t)~mask;
}

int ms7004_fw_run_cycles(ms7004_fw_t *fw, int cycles)
{
    int spent = 0;
    while (spent < cycles) {
        spent += i8035_step(&fw->cpu);
    }
    return spent;
}
