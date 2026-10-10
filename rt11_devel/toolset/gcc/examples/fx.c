/* FX - the fixed-point arithmetic of fx.h put to the test: for each
 * pair of the table a line "num den: q" of fx_div8 and "a b: p" of
 * fx_mul, then the sine and the cosine of some angles; the harness
 * (../tests) does the sums on the host.  The operands pass through
 * volatile so that GCC folds nothing. */

#include <fx.h>
#include <stdio.h>

static const int divs[][2] = {
	{32640, 128}, {-32640, 128}, {255, 1}, {-255, 1}, {0, 7},
	{12800, 100}, {-12800, 100}, {30000, 200}, {1000, 1000}, {999, 1000},
	{20480, 81}, {-20480, 81}, {4096, 17}, {255, 255}, {32767, 129},
};

static const int muls[][2] = {
	{256, 256}, {512, 256}, {-512, 256}, {256, -256}, {-256, -256},
	{1000, 128}, {-1000, 128}, {32767, 255}, {-32767, 255}, {3, 100},
	{-3, 100}, {181, 181}, {30000, -2}, {255, 255}, {0, 32767},
};

static const int angles[] = {0, 1, 32, 63, 64, 65, 96, 127, 128, 129, 160, 191, 192, 193, 224, 255, 256, 300, -1, -64};

int main(void)
{
	int k;

	for (k = 0; k < (int)(sizeof divs / sizeof divs[0]); k++) {
		volatile int vn = divs[k][0], vd = divs[k][1];
		printf("%d %d: %d\n", divs[k][0], divs[k][1], fx_div8(vn, vd));
	}
	for (k = 0; k < (int)(sizeof muls / sizeof muls[0]); k++) {
		volatile int va = muls[k][0], vb = muls[k][1];
		printf("%d %d: %d\n", muls[k][0], muls[k][1], fx_mul(va, vb));
	}
	for (k = 0; k < (int)(sizeof angles / sizeof angles[0]); k++) {
		volatile int va = angles[k];
		printf("%d: %d %d\n", angles[k], fx_sin(va), fx_cos(va));
	}
	puts("FX DONE");
	return 0;
}
