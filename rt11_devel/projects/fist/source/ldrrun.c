/*
 * ldrrun.c - boot RT-11, inject the park-RMON loader .SAV, run it, and dump
 * the 16 KB VRAM.  The standard oracle can't run the loader (it loads with VRAM
 * on, which corrupts the GST embed in banks 2-3); booting RT-11 first puts the
 * machine in the VRAM-off monitor state the loader expects.  Scratch harness.
 *   usage: ldrrun <rom> <bootdisk> <loader.sav> <vram.out>
 */
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
#include <ms0515/core/memory.h>
#include <ms0515/core/floppy.h>
#include <stdio.h>

static void wr(ms0515_memory_t *m, uint16_t a, uint8_t v)
{ mem_write_byte(m, mem_translate(m, a), v); }

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: ldrrun rom disk sav vramout\n"); return 2; }
    static ms0515_board_t board;
    board_init(&board);
    FILE *rf = fopen(argv[1], "rb"); static uint8_t rb[65536];
    size_t rn = fread(rb, 1, sizeof rb, rf); fclose(rf);
    board_load_rom(&board, rb, (uint32_t)rn);
    fdc_attach(&board.fdc, 0, argv[2], true);
    fdc_attach(&board.fdc, 2, argv[2], true);
    board_reset(&board);

    /* boot to the monitor idle loop (dispatcher settles, VRAM off) */
    uint16_t last = 0xFFFF; int stable = 0;
    for (long i = 0; i < 40000000L; ++i) {
        board_step_cpu(&board);
        if ((i % 500000) == 0) {
            uint16_t d = board.mem.dispatcher;
            if (d == last) { if (++stable >= 8) break; } else { stable = 0; last = d; }
        }
    }
    printf("booted: DISP=%06o PC=%06o\n", board.mem.dispatcher, board.cpu.r[7]);

    /* inject the loader .SAV at 0 (VRAM off now, so banks 0-5 load as RAM) */
    FILE *sf = fopen(argv[3], "rb"); static uint8_t img[65536];
    size_t sn = fread(img, 1, sizeof img, sf); fclose(sf);
    for (size_t k = 0; k < sn; ++k) wr(&board.mem, (uint16_t)k, img[k]);
    board.cpu.r[7] = 01000;
    board.cpu.psw = 0;
    board.cpu.halted = false;

    /* run long enough for relocate + dojo render + fighter decode/overlay */
    for (long s = 0; s < 8000000L; ++s) {
        board_step_cpu(&board);
        if (board.cpu.halted) { printf("HALT @%06o\n", board.cpu.r[7]); break; }
    }
    printf("after run: PC=%06o DISP=%06o\n", board.cpu.r[7], board.mem.dispatcher);

    const uint8_t *vram = board_get_vram(&board);
    FILE *o = fopen(argv[4], "wb");
    fwrite(vram, 1, MEM_VRAM_SIZE, o);
    fclose(o);
    int nz = 0; for (int i = 0; i < MEM_VRAM_SIZE; ++i) if (vram[i]) ++nz;
    printf("VRAM dumped (%d non-zero bytes)\n", nz);
    return 0;
}
