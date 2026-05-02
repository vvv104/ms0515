/*
 * rom_patches.c — Runtime patches for guest ROM/OS quirks.
 *
 * Lives in its own compilation unit so cpu.c stays a faithful
 * K1801VM1 emulation and board.c stays a faithful MS0515 board
 * model, with no knowledge of which specific ROM image happens to
 * be loaded.  See rom_patches.h for the rationale.
 */

#include <ms0515/rom_patches.h>
#include <ms0515/board.h>
#include <ms0515/cpu.h>

bool rom_patches_apply(ms0515_board_t *board)
{
    ms0515_cpu_t  *cpu = &board->cpu;
    const uint16_t pc  = cpu->r[CPU_REG_PC];

    /* ── ROM-A cassette autoloader stub ──────────────────────────────────── */
    /*
     * The patched ROM-A places a cassette-tape autoloader at 0o162360.
     * On real hardware: with a cassette inserted the routine reads
     * bits via Reg B bit 7 (CSIN), shifts them into R0, hunts for
     * sync byte 0o346 / 0o031, then jumps via `MOV @#157704, PC` to
     * the loaded-from-tape entry point.  Without a cassette the
     * inner spin (0o162522…0o162536) loops forever waiting for bit 7
     * to flip — and on real hardware this would also hang.
     *
     * Omega's resident timer ISR (RT-11SJ kernel, RT11SJ.SYS file
     * offset 0x685c → RAM 0o146210) calls this entry every 16 frames
     * unconditionally as part of an "auto-load from tape if user
     * inserts one" feature.  Without a real cassette, the periodic
     * call hangs the boot before the prompt even appears.
     *
     * We intercept the routine's entry and emulate "no cassette" by
     * performing an immediate RTS back to the timer ISR's call site.
     * R0 is left unchanged — the caller at 0o146214 ignores it.
     *
     * Gated by signature: ROM-A's tape bootstrap starts with
     * `BIC #4, @#157706` = word 042737.  ROM-B's service-3 entry at
     * the same address starts with `MOVB R5, R1` = 110501 and is a
     * legitimate floppy service that must run.
     *
     * This patch will be obsoleted by a proper cassette device
     * emulation (planned core/src/cassette.c).
     */
    if (pc == 0162360 && board_read_word(board, 0162360) == 042737u) {
        cpu->r[CPU_REG_PC] = board_read_word(board, cpu->r[CPU_REG_SP]);
        cpu->r[CPU_REG_SP] += 2;
        cpu->cycles = 21;  /* approximate cost of an RTS */
        return true;
    }

    return false;
}
