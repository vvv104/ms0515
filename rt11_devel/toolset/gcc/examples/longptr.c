/* LONGPTR - a long read through a pointer into the registers the
 * pointer was in, the way GCC 15.2's pdp11 backend got it wrong (README.md,
 * "A trap in the compiler"): with the pointer in r0 and the long going to
 * r0:r1 it loaded the high word first, over the pointer, and read the low
 * word through what it had just loaded.  Built twice, at -O0 (LONGP0),
 * where every such read went wrong, and at -Os (LONGPS), where a pointer
 * a function gave back did.  A line a case, the long in hex; the harness
 * (../tests) knows the answers. */

#include <stdio.h>

struct record {
	unsigned a, b;
	unsigned long size;
	long delta;
};

static struct record records[2] = {
	{1, 2, 0x12345678UL, -70000L},
	{3, 4, 0x0001FFFFUL, 40000L},
};

/* Each through a pointer it is given, kept from being folded. */
__attribute__((noinline)) static unsigned long size_of(const struct record *r) { return r->size; }
__attribute__((noinline)) static unsigned low_of(const struct record *r) { return (unsigned)r->size; }
__attribute__((noinline)) static unsigned long plus(const struct record *r, unsigned long x) { return x + r->size; }
__attribute__((noinline)) static long delta_of(const struct record *r) { return r->delta; }

/* A pointer a function gives back: in r0, the long wanted in r0:r1. */
__attribute__((noinline)) static struct record *pick(int k) { return &records[k]; }
__attribute__((noinline)) static unsigned long picked(int k) { return pick(k)->size; }

int main(void)
{
	for (int k = 0; k < 2; k++) {
		const struct record *r = &records[k];

		printf("%lx %x %lx %lx %lx\n", size_of(r), low_of(r), plus(r, 0x10001UL),
		       (unsigned long)delta_of(r), picked(k));
	}
	puts("LONGPTR DONE");
	return 0;
}
