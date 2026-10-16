/* lib.c - the little library of a process (unistd.h): strings and
 * numbers, and the two functions the compiler calls on its own. */
#include "unistd.h"

int strlen(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

void puts_fd(int fd, const char *s)
{
	write(fd, s, strlen(s));
}

void putnum(unsigned n)
{
	char digits[6];
	int i = sizeof digits;

	do {
		digits[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	write(1, digits + i, (int)sizeof digits - i);
}

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
