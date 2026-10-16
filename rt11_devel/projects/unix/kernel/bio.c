/* bio.c - the buffer cache: blocks of the boot drive read through the ROM.
 *
 * A few buffers in bank 6 for now, the least recently used taken for a new
 * block; the shadow banks 1 and 6 hold the full cache later (SPEC.md "The
 * kernel").  Stage 3 only reads. */
#include "high.h"
#include "rom.h"

#define bufs (HIGH->bufs)
static unsigned clock_hand;		/* for the least recently used */

struct buf *bread(unsigned block)
{
	struct buf *b, *oldest = bufs;
	int i;

	for (i = 0; i < NBUF; i++) {
		b = &bufs[i];
		if (b->valid && b->block == block) {
			b->used = ++clock_hand;
			return b;
		}
		if (!b->valid || b->used < oldest->used)
			oldest = b;
	}
	b = oldest;
	b->valid = 0;
	if (rom_bread(block, b->data) != 0)
		return 0;
	b->block = block;
	b->valid = 1;
	b->used = ++clock_hand;
	return b;
}
