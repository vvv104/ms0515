/* bio.c - the buffer cache: blocks of the devices, a few buffers in bank
 * 6, the least recently used given up for a new block.  A write goes
 * through to the device at once: a diskette may be taken out at any time,
 * and what was written is on it.  The shadow banks 1 and 6 hold a fuller
 * cache later (SPEC.md "The kernel"). */
#include "high.h"

#define bufs (HIGH->bufs)
static unsigned clock_hand;		/* for the least recently used */

/* The buffer of (dev, block) if one holds it; else the least recently used,
 * marked as holding nothing. */
static struct buf *find(unsigned dev, unsigned block)
{
	struct buf *b, *oldest = bufs;
	int i;

	for (i = 0; i < NBUF; i++) {
		b = &bufs[i];
		if (b->valid && b->dev == dev && b->block == block) {
			b->used = ++clock_hand;
			return b;
		}
		if (!b->valid || b->used < oldest->used)
			oldest = b;
	}
	oldest->valid = 0;
	oldest->dev = dev;
	oldest->block = block;
	oldest->used = ++clock_hand;
	return oldest;
}

struct buf *bread(unsigned dev, unsigned block)
{
	struct buf *b = find(dev, block);

	if (b->valid)
		return b;
	if (dev_read(dev, block, b->data) != 0)
		return 0;
	b->valid = 1;
	return b;
}

/* A buffer for a block about to be written whole: not read, cleared. */
struct buf *getblk(unsigned dev, unsigned block)
{
	struct buf *b = find(dev, block);

	memset(b->data, 0, BSIZE);
	b->valid = 1;
	return b;
}

int bwrite(struct buf *b)
{
	return dev_write(b->dev, b->block, b->data) == 0 ? 0 : -EIO;
}
