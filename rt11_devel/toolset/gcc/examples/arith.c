/* ARITH - the 16-bit arithmetic helpers of rt/arith.s put to the test.
 *
 * The processor has no MUL or DIV, so every `*`, `/` and `%` of an int is
 * a call to a helper.  For each pair of the table the program prints the
 * signed quotient, remainder and product, and the unsigned quotient and
 * remainder, one line a pair: "a b: q r p uq ur".  The pairs are read
 * through volatile so that GCC computes nothing at compile time.  The
 * harness (../tests) does the same arithmetic on the host and compares. */

#include <rt11.h>

static const int pairs[][2] = {
	{7, 2}, {-7, 2}, {7, -2}, {-7, -2},
	{32767, 1}, {-32768, 1}, {-32768, -1}, {0, 5}, {1, 32767},
	{30000, 7}, {-30000, 7}, {12345, -123}, {-32768, 2}, {32767, 32767},
	{255, 256}, {-1, 1}, {-1, 2}, {-1, -1}, {1000, 1000}, {4096, 64},
};

static void putnum(int n)
{
	unsigned u = (unsigned)n;
	char buf[7];
	int i = 0;

	if (n < 0) {
		rt11_ttyout('-');
		u = (unsigned)(-n);
	}
	do {
		buf[i++] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	while (i > 0)
		rt11_ttyout(buf[--i]);
}

static void putunum(unsigned u)
{
	char buf[7];
	int i = 0;

	do {
		buf[i++] = (char)('0' + u % 10);
		u /= 10;
	} while (u);
	while (i > 0)
		rt11_ttyout(buf[--i]);
}

int main(void)
{
	for (int k = 0; k < (int)(sizeof pairs / sizeof pairs[0]); k++) {
		volatile int va = pairs[k][0];
		volatile int vb = pairs[k][1];
		int a = va;
		int b = vb;

		putnum(a);
		rt11_ttyout(' ');
		putnum(b);
		rt11_ttyout(':');
		rt11_ttyout(' ');
		putnum(a / b);
		rt11_ttyout(' ');
		putnum(a % b);
		rt11_ttyout(' ');
		putnum(a * b);
		rt11_ttyout(' ');
		putunum((unsigned)a / (unsigned)b);
		rt11_ttyout(' ');
		putunum((unsigned)a % (unsigned)b);
		rt11_ttyout('\r');
		rt11_ttyout('\n');
	}
	rt11_print("ARITH DONE");
	return 0;
}
