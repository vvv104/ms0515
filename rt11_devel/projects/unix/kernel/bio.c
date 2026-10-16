/* bio.c - the buffer cache: blocks of the devices, in two levels.  A few
 * buffers in bank 6 are the blocks in hand, the least recently used given
 * up for a new block; a block given up goes to the shadow copy of bank 1,
 * 15 blocks more, and comes back from there without the device when it is
 * wanted again (xcopy, start.s).  A block is in one level or the other,
 * never both.
 *
 * A write goes through to the device at once: a diskette may be taken out
 * at any time, and what was written is on it.  Only the bitmap is let
 * wait, marked dirty, until the end of the system call that changed it
 * (bflush) or its buffer is wanted for another block - a block a call, not
 * a block a bit.  The shadow bank holds clean blocks only. */
#include "high.h"

#define bufs (HIGH->bufs)
#define shtab (HIGH->shadow)
#define WORDS (BSIZE / 2)
#define SHADOW_ON (DISP_COPY & ~BANK1_BIT)
static unsigned clock_hand;		/* for the least recently used */

/* The shadow bank walked by pointers, its blocks' addresses alongside: an
 * index into it would cost a multiplication, the T-11 having none. */

/* The shadow entry holding (dev, block) and its block's address in *at;
 * 0 when none does. */
static struct shadow *shadow_of(unsigned dev, unsigned block, char **at)
{
	struct shadow *s;
	char *a = SHADOW_BLOCK(0);

	for (s = shtab; s < shtab + NSHADOW; s++, a += BSIZE)
		if (s->used && s->dev == dev && s->block == block) {
			*at = a;
			return s;
		}
	return 0;
}

/* The buffer's block kept in the shadow bank: in a free block or the least
 * recently used one, not in `keep`. */
static void stash(const struct buf *b, const struct shadow *keep)
{
	struct shadow *s, *t = 0;
	char *a = SHADOW_BLOCK(0), *ta = a;

	for (s = shtab; s < shtab + NSHADOW; s++, a += BSIZE)
		if (s != keep && (!t || s->used < t->used)) {
			t = s;
			ta = a;
		}
	xcopy(ta, b->data, WORDS, SHADOW_ON);
	t->dev = b->dev;
	t->block = b->block;
	t->used = ++clock_hand;
}

/* The buffer of (dev, block) if one holds it; else the least recently
 * used, its block written if it was dirty and kept in the shadow bank, and
 * now holding (dev, block): valid when the shadow bank had it and `fill`
 * asks for its data, else not. */
static struct buf *find(unsigned dev, unsigned block, int fill)
{
	struct buf *b, *oldest = bufs;
	struct shadow *h;
	char *at = 0;
	int i;

	dev_canon(&dev, &block);

	for (i = 0; i < NBUF; i++) {
		b = &bufs[i];
		if (b->valid && b->dev == dev && b->block == block) {
			b->used = ++clock_hand;
			return b;
		}
		if (!b->valid || b->used < oldest->used)
			oldest = b;
	}
	h = shadow_of(dev, block, &at);
	if (oldest->valid) {
		if (oldest->dirty)
			dev_write(oldest->dev, oldest->block, oldest->data);
		stash(oldest, h);
	}
	oldest->dirty = 0;
	oldest->valid = 0;
	if (h) {
		if (fill) {
			xcopy(oldest->data, at, WORDS, SHADOW_ON);
			oldest->valid = 1;
		}
		h->used = 0;
	}
	oldest->dev = dev;
	oldest->block = block;
	oldest->used = ++clock_hand;
	return oldest;
}

struct buf *bread(unsigned dev, unsigned block)
{
	struct buf *b = find(dev, block, 1);

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
	struct buf *b = find(dev, block, 0);

	memset(b->data, 0, BSIZE);
	b->valid = 1;
	return b;
}

/* The blocks of dev [start, start + n) forgotten in both levels: swap
 * writes them past the cache.  A block at a time, each by its side's
 * name. */
void binval(unsigned dev, unsigned start, unsigned n)
{
	struct shadow *s;
	unsigned d, block;
	int i;

	for (; n; n--, start++) {
		d = dev;
		block = start;
		dev_canon(&d, &block);
		for (i = 0; i < NBUF; i++)
			if (bufs[i].dev == d && bufs[i].block == block)
				bufs[i].valid = 0;
		for (s = shtab; s < shtab + NSHADOW; s++)
			if (s->dev == d && s->block == block)
				s->used = 0;
	}
}

int bwrite(struct buf *b)
{
	b->dirty = 0;
	return dev_write(b->dev, b->block, b->data) == 0 ? 0 : -EIO;
}

void bdirty(struct buf *b)
{
	b->dirty = 1;
}

/* The dirty buffers written out. */
void bflush(void)
{
	int i;

	for (i = 0; i < NBUF; i++)
		if (bufs[i].valid && bufs[i].dirty)
			bwrite(&bufs[i]);
}
