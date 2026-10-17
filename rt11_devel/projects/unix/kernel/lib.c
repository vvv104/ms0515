/* lib.c - the few functions of the C library the kernel needs, and the two
 * the compiler calls on its own for copying and clearing. */
#include "kernel.h"

/* By words when both ends and the count are even - a block, an inode -
 * four times the bytes' pace; else by bytes. */
void *memcpy(void *to, const void *from, unsigned n)
{
	if (!(((unsigned)to | (unsigned)from | n) & 1)) {
		unsigned *t = to;
		const unsigned *f = from;

		for (n >>= 1; n; n--)
			*t++ = *f++;
	} else {
		char *t = to;
		const char *f = from;

		while (n--)
			*t++ = *f++;
	}
	return to;
}

void *memset(void *p, int c, unsigned n)
{
	if (!(((unsigned)p | n) & 1)) {
		unsigned *s = p, w = (unsigned char)c * 0401u;

		for (n >>= 1; n; n--)
			*s++ = w;
	} else {
		char *s = p;

		while (n--)
			*s++ = (char)c;
	}
	return p;
}
