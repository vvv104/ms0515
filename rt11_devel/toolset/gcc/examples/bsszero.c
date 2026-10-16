/* BSSZERO - a zeroed static array in the bss, the way GCC 15.2's pdp11
 * backend did not put it (README.md, "A trap in the compiler"): with the
 * GNU assembler's syntax it wrote every zeroed variable into .data as
 * `.=.+N`, so a program's file carried its zeros - harmless in an RT-11
 * .SAV, which holds the memory whole, but a UNIX program or a kernel
 * image grew by every buffer it had.  The program says whether its
 * arrays are between _edata and _end, the bss the linker counts, and
 * that the zeroed ones read zero and the initialized one its value. */

#include <stdio.h>

extern char _edata[], _end[];

static char block[2048];		/* zeroed, file-local */
unsigned table[64];			/* zeroed, global */
static unsigned seed[4] = {1, 2, 3, 4};	/* initialized: stays in .data */

static int in_bss(const void *p, unsigned n)
{
	return (const char *)p >= _edata && (const char *)p + n <= _end;
}

int main(void)
{
	unsigned i, nonzero = 0;

	for (i = 0; i < sizeof block; i++)
		nonzero += block[i] != 0;
	for (i = 0; i < 64; i++)
		nonzero += table[i] != 0;
	printf("BLOCK %d TABLE %d SEED %d\n", in_bss(block, sizeof block),
	       in_bss(table, sizeof table), in_bss(seed, sizeof seed));
	printf("NONZERO %u SEED %u%u%u%u\n", nonzero, seed[0], seed[1], seed[2], seed[3]);
	puts("BSSZERO DONE");
	return 0;
}
