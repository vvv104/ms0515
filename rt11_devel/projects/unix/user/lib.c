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

/* Why, by errno: each the number's byte, then the words, a NUL after. */
static const char why[] =
	"\002not found\0" "\024not a directory\0" "\013no room for another process\0"
	"\021there already\0" "\025a directory\0" "\034no room on the volume\0"
	"\017not a block device\0" "\020busy\0" "\026no file system there\0"
	"\022on another volume\0" "\023not without /boot/fd\0" "\015not allowed\0"
	"\007too long\0" "\102not empty\0";

/* who: what: and why, by errno, on the error output. */
void complain(const char *who, const char *what)
{
	const char *s = why;

	while (*s && *s != errno)
		s += strlen(s) + 1;
	puts_fd(2, who);
	puts_fd(2, ": ");
	puts_fd(2, what);
	puts_fd(2, ": ");
	puts_fd(2, *s ? s + 1 : "cannot");
	puts_fd(2, "\n");
}

/* dir, a '/' after it unless it ends in one, and the first n bytes of name
 * up to its NUL, into path, PATHMAX bytes with the NUL: 0, or -1 with
 * errno E2BIG when it does not fit - never a path cut short, which would
 * name another file. */
int join(char *path, const char *dir, const char *name, int n)
{
	int at = 0;

	while (*dir && at < PATHMAX)
		path[at++] = *dir++;
	if (at && path[at - 1] != '/' && n && *name && at < PATHMAX)
		path[at++] = '/';
	while (n-- && *name && at < PATHMAX)
		path[at++] = *name++;
	if (at == PATHMAX) {
		errno = E2BIG;
		return -1;
	}
	path[at] = 0;
	return 0;
}

int memcmp(const void *a, const void *b, unsigned n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
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
