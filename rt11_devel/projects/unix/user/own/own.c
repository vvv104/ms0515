/* own.c - the little library's C half (own.h). */
#include "own.h"

void own_puts(const char *s)
{
	while (*s)
		own_putc(*s++);
}

void own_putoct(unsigned n)
{
	char digits[7], *p = digits + sizeof digits;

	*--p = 0;
	do
		*--p = (char)('0' + (n & 7));
	while ((n >>= 3) != 0);
	own_puts(p);
}
