/* heap.c - malloc over .SETTOP.
 *
 * The heap begins where the program's stack ends (rt11_memtop, the SP the
 * program started with) and grows up by asking the monitor for more with
 * .SETTOP, a few hundred bytes at a time; the monitor gives what there is
 * below the USR and the resident monitor.  The blocks lie one after
 * another, each behind a header with its size and whether it is free;
 * malloc takes the first free one that fits, splitting what is left,
 * and free merges a block with a free neighbour above it. */

#include <rt11.h>
#include <stdlib.h>
#include <string.h>

struct header {
	size_t size;		/* the header included */
	int free;
};

#define GRAIN 512		/* what .SETTOP is asked for at the least */

static char *heapEnd;		/* the first address not yet the heap's */

static struct header *first(void) { return (struct header *)rt11_memtop; }
static struct header *next(struct header *h) { return (struct header *)((char *)h + h->size); }
static struct header *end(void) { return (struct header *)heapEnd; }

/* More heap: n bytes at the end, as one free block, or 0 when the monitor
 * has no more. */
static struct header *grow(size_t n)
{
	size_t ask = (n + GRAIN - 1) & ~(size_t)(GRAIN - 1);
	char *top;
	struct header *h;

	if (!heapEnd)
		heapEnd = rt11_memtop;
	top = rt11_settop(heapEnd + ask - 1);
	if ((size_t)(top + 1 - heapEnd) < n)
		return 0;
	h = end();
	h->size = (size_t)(top + 1 - heapEnd);
	h->free = 1;
	heapEnd = top + 1;
	return h;
}

static void split(struct header *h, size_t size)
{
	if (h->size >= size + sizeof(struct header) + 2) {
		struct header *rest = (struct header *)((char *)h + size);
		rest->size = h->size - size;
		rest->free = 1;
		h->size = size;
	}
	h->free = 0;
}

void *malloc(size_t size)
{
	struct header *h;

	if (!heapEnd)
		heapEnd = rt11_memtop;
	size = (size + sizeof(struct header) + 1) & ~(size_t)1;
	for (h = first(); h < end(); h = next(h)) {
		while (h->free && next(h) < end() && next(h)->free)
			h->size += next(h)->size;
		if (h->free && h->size >= size) {
			split(h, size);
			return h + 1;
		}
	}
	h = grow(size);
	if (!h)
		return 0;
	split(h, size);
	return h + 1;
}

void free(void *block)
{
	struct header *h;

	if (!block)
		return;
	h = (struct header *)block - 1;
	h->free = 1;
	while (next(h) < end() && next(h)->free)
		h->size += next(h)->size;
}

void *calloc(size_t count, size_t size)
{
	void *block = malloc(count * size);

	if (block)
		memset(block, 0, count * size);
	return block;
}

void *realloc(void *block, size_t size)
{
	struct header *h;
	void *moved;

	if (!block)
		return malloc(size);
	h = (struct header *)block - 1;
	if (h->size - sizeof(struct header) >= size)
		return block;
	moved = malloc(size);
	if (moved) {
		memcpy(moved, block, h->size - sizeof(struct header));
		free(block);
	}
	return moved;
}
