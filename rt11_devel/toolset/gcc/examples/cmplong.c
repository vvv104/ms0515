/* CMPLONG - signed comparisons of longs, the way GCC 15.2's pdp11
 * backend gets them wrong (README.md, "A trap in the compiler"): when
 * the high words are equal it compares the low words with a signed
 * branch, so a low word with bit 15 set reads as negative.  For each
 * pair of the table a line of six digits: x > y, x < y, x >= y, x <= y,
 * x >= 0, x < 0, 1 for true; the harness (../tests) knows the answers
 * and marks the test as one that may fail until the compiler is fixed.
 * The operands pass through volatile so that nothing is folded. */

#include <stdio.h>

static const long pairs[][2] = {
	{100000L, 5L}, {-100000L, 5L},		/* the high words differ */
	{32768L, 5L}, {40000L, 32767L}, {-32768L, -40000L},
	{32768L, 0L}, {0x10000L + 32768L, 0x10000L + 5L},
	{5L, 32768L}, {-5L, -32768L}, {32768L, 32768L},
};

int main(void)
{
	for (int k = 0; k < (int)(sizeof pairs / sizeof pairs[0]); k++) {
		volatile long vx = pairs[k][0], vy = pairs[k][1];
		long x = vx, y = vy;

		printf("%d%d%d%d%d%d\n", x > y, x < y, x >= y, x <= y, x >= 0, x < 0);
	}
	puts("CMPLONG DONE");
	return 0;
}
