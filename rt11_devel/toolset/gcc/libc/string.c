/* string.c - bytes and C strings, a byte at a time. */

#include <string.h>

void *memcpy(void *to, const void *from, size_t n)
{
	char *t = to;
	const char *f = from;

	while (n--)
		*t++ = *f++;
	return to;
}

void *memmove(void *to, const void *from, size_t n)
{
	char *t = to;
	const char *f = from;

	if (t <= f || t >= f + n) {
		while (n--)
			*t++ = *f++;
	} else {
		t += n;
		f += n;
		while (n--)
			*--t = *--f;
	}
	return to;
}

void *memset(void *block, int c, size_t n)
{
	char *b = block;

	while (n--)
		*b++ = (char)c;
	return block;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *x = a, *y = b;

	for (; n; n--, x++, y++)
		if (*x != *y)
			return *x - *y;
	return 0;
}

void *memchr(const void *block, int c, size_t n)
{
	const unsigned char *b = block;

	for (; n; n--, b++)
		if (*b == (unsigned char)c)
			return (void *)b;
	return 0;
}

size_t strlen(const char *s)
{
	const char *e = s;

	while (*e)
		e++;
	return (size_t)(e - s);
}

char *strcpy(char *to, const char *from)
{
	char *t = to;

	while ((*t++ = *from++) != 0)
		;
	return to;
}

char *strncpy(char *to, const char *from, size_t n)
{
	char *t = to;

	for (; n && *from; n--)
		*t++ = *from++;
	while (n--)
		*t++ = 0;
	return to;
}

char *strcat(char *to, const char *from)
{
	strcpy(to + strlen(to), from);
	return to;
}

char *strncat(char *to, const char *from, size_t n)
{
	char *t = to + strlen(to);

	for (; n && *from; n--)
		*t++ = *from++;
	*t = 0;
	return to;
}

int strcmp(const char *a, const char *b)
{
	for (; *a && *a == *b; a++, b++)
		;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n && *a && *a == *b; n--, a++, b++)
		;
	return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return 0;
	}
}

char *strrchr(const char *s, int c)
{
	const char *found = 0;

	for (;; s++) {
		if (*s == (char)c)
			found = s;
		if (!*s)
			return (char *)found;
	}
}

char *strstr(const char *s, const char *needle)
{
	size_t n = strlen(needle);

	for (; *s; s++)
		if (strncmp(s, needle, n) == 0)
			return (char *)s;
	return n ? 0 : (char *)s;
}
