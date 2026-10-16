/* lib.c - the few functions of the C library the kernel needs, and the two
 * the compiler calls on its own for copying and clearing. */
#include "kernel.h"

void *memcpy(void *to, const void *from, unsigned n)
{
	char *t = to;
	const char *f = from;

	while (n--)
		*t++ = *f++;
	return to;
}

void *memset(void *p, int c, unsigned n)
{
	char *s = p;

	while (n--)
		*s++ = (char)c;
	return p;
}
