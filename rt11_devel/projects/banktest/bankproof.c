/*
 * bankproof.c - pure-core proof that park-RMON bank switching works.
 *
 * No CPU, no RT-11: drive the memory model directly the way the MACRO-11
 * flip stub will.  Confirms that the extended banks are genuinely separate
 * physical storage, so flipping banks 0-6 to extended (parking RMON), doing
 * work, and flipping back leaves the primary banks (RMON) untouched.
 */
#include <ms0515/core/memory.h>
#include <stdio.h>

static void wr(ms0515_memory_t *m, uint16_t a, uint16_t v)
{ mem_write_word(m, mem_translate(m, a), v); }
static uint16_t rd(ms0515_memory_t *m, uint16_t a)
{ return mem_read_word(m, mem_translate(m, a)); }

int main(void)
{
    ms0515_memory_t m;
    mem_init(&m);

    /* FIST runtime banking: banks 0-6 primary, VRAM_EN, window @ 040000. */
    const uint16_t PRIMARY  = 03377;
    const uint16_t EXTENDED = 03377 & ~0177;   /* banks 0-6 -> extended */

    int fails = 0;

    /* Bank 5 (0120000) is RAM under this window. Seed PRIMARY bank 5. */
    m.dispatcher = PRIMARY;
    wr(&m, 0120000, 012345);
    /* Also seed bank 6 (0140000 = RMON region) primary, as an RMON stand-in. */
    wr(&m, 0140100, 0177777);

    /* Park: flip banks 0-6 extended, scribble all over bank 5 + bank 6. */
    m.dispatcher = EXTENDED;
    uint16_t ext_before = rd(&m, 0120000);      /* fresh extended cell */
    wr(&m, 0120000, 054321);
    wr(&m, 0140100, 0044444);                   /* would clobber RMON if shared */

    /* Restore: flip back to primary. */
    m.dispatcher = PRIMARY;
    uint16_t p5 = rd(&m, 0120000);
    uint16_t p6 = rd(&m, 0140100);

    printf("extended bank5 cell before write : %06o (expect 000000 fresh)\n", ext_before);
    printf("primary  bank5 after park+restore: %06o (expect 012345)\n", p5);
    printf("primary  bank6 (RMON) after park : %06o (expect 177777)\n", p6);

    if (ext_before != 0)      { printf("  FAIL: extended bank not fresh/separate\n"); fails++; }
    if (p5 != 012345)         { printf("  FAIL: primary bank5 corrupted by extended write\n"); fails++; }
    if (p6 != 0177777)        { printf("  FAIL: RMON (primary bank6) corrupted by extended write\n"); fails++; }

    /* And confirm the extended write really landed in extended storage. */
    m.dispatcher = EXTENDED;
    uint16_t e5 = rd(&m, 0120000);
    printf("extended bank5 after restore     : %06o (expect 054321 retained)\n", e5);
    if (e5 != 054321)         { printf("  FAIL: extended storage not retained\n"); fails++; }

    printf(fails ? "\nRESULT: FAIL (%d)\n" : "\nRESULT: PARK-RMON BANKING OK\n", fails);
    return fails ? 1 : 0;
}
