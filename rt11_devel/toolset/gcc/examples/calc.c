/* CALC - primes below 10000 by trial division: the four-language
 * benchmark's algorithm (ms0515_data/bench, 2026-09-25) in C11 for GCC.
 * No multiply, divide or shift in the timed part, as in the others: d*d
 * is a running square (sq += d + d + 1), n mod d goes by a table of the
 * divisor doubled up to n and subtracted back down.  The clock is TICK's
 * (tick.s); the time and the count are printed by .TTYOUT.
 *
 * For the record: MACRO-11 25.0 s, DECUS C 76.1 s (47.2 with register),
 * PAS1 107.0 s, FORTRAN IV 135.2 s; GCC 15.2 -O2 29.3 s. */

#include <rt11.h>

extern void tic0(void);
extern int tic1(void);

static int dds[16];

static int modu(int n, int d)
{
	int k = 0;
	int dd = d;

	while (dd <= n) {
		dds[k++] = dd;
		dd += dd;
	}
	while (k > 0) {
		k--;
		if (n >= dds[k])
			n -= dds[k];
	}
	return n;
}

static void puts_(const char *s)
{
	while (*s)
		rt11_ttyout(*s++);
}

static void putdec(int n)
{
	int q = 0;

	while (n >= 10) {
		n -= 10;
		q++;
	}
	if (q)
		putdec(q);
	rt11_ttyout('0' + n);
}

static void put_time(int ticks)
{
	int s = ticks / 50;
	int hund = (ticks - s * 50) * 2;

	puts_("TIME: ");
	putdec(s);
	rt11_ttyout('.');
	rt11_ttyout('0' + hund / 10);
	rt11_ttyout('0' + hund % 10);
	puts_(" S\r\n");
}

int main(void)
{
	int count = 0;
	int last = 0;

	tic0();
	for (int n = 2; n < 10000; n++) {
		int prime = 1;
		int sq = 4;

		for (int d = 2; sq <= n; d++) {
			if (modu(n, d) == 0) {
				prime = 0;
				break;
			}
			sq += d + d + 1;
		}
		if (prime) {
			count++;
			last = n;
		}
	}
	put_time(tic1());
	puts_("PRIMES BELOW 10000: ");
	putdec(count);
	puts_(" LAST ");
	putdec(last);
	puts_("\r\n");
	return 0;
}
