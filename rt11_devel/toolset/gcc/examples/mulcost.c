/* MULCOST - a walk over a table of structures by a pointer, the way GCC
 * 15.2's pdp11 backend made a multiplication of it (README.md, "A trap in
 * the compiler"): without EIS a multiply is a call of __mulhi3, some
 * eighty instructions, but the backend priced it as one MUL and, under
 * -Os, as any other instruction, so the loop's pointers were counted as
 * a step number times the structure's size - two calls a step.  Built at
 * -Os, with the functions kept in the file's order
 * (-fno-toplevel-reorder): the program reads its own lookup, from it to
 * lookup_end, and counts what refers to __mulhi3 - a JSR PC to it, or its
 * address as a word - then prints what the walks found.  The harness
 * (../tests) wants no such reference. */

#include <stdio.h>

struct entry {
	unsigned dev, block, used;
};

/* The table at an address of its own, as a kernel's tables are: past the
 * program, and a constant. */
#define TABLE ((struct entry *)060000)
#define ENTRIES 15
#define AREA ((char *)070000)
#define STEP 512

extern int __mulhi3(int, int);

/* The entry holding (dev, block) and its area's address in *at. */
__attribute__((noinline)) struct entry *lookup(unsigned dev, unsigned block, char **at)
{
	struct entry *e;
	char *a = AREA;

	for (e = TABLE; e < TABLE + ENTRIES; e++, a += STEP)
		if (e->used && e->dev == dev && e->block == block) {
			*at = a;
			return e;
		}
	return 0;
}

/* Where lookup's code ends. */
__attribute__((noinline)) void lookup_end(void)
{
}

#define JSR_PC_REL 004767		/* jsr pc,x: the next word is x's offset */

/* The words of [from, to) that call __mulhi3 or hold its address. */
static int references(const unsigned *from, const unsigned *to)
{
	const unsigned target = (unsigned)__mulhi3;
	const unsigned *w;
	int n = 0;

	for (w = from; w < to; w++) {
		if (*w == target)
			n++;
		else if (*w == JSR_PC_REL && w + 1 < to && (unsigned)(w + 2) + w[1] == target)
			n++;
	}
	return n;
}

static unsigned devs[ENTRIES], blocks[ENTRIES];

int main(void)
{
	struct entry *e;
	char *at;
	int k;

	for (k = 0, e = TABLE; k < ENTRIES; k++, e++) {
		e->dev = devs[k] = (unsigned)k & 1;
		e->block = blocks[k] = (unsigned)k * 10;
		e->used = k != 7;
	}
	printf("MULHI3 CALLS %d\n", references((const unsigned *)lookup, (const unsigned *)lookup_end));
	for (k = 0; k < ENTRIES; k++) {
		at = 0;
		e = lookup(devs[k], blocks[k], &at);
		printf("%u %o\n", e ? e->block : 0177777u, (unsigned)at);
	}
	puts("MULCOST DONE");
	return 0;
}
