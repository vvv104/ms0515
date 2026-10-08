/* stdlib.c - numbers from text, a random sequence, abs. */

#include <ctype.h>
#include <stdlib.h>

int abs(int n) { return n < 0 ? -n : n; }
long labs(long n) { return n < 0 ? -n : n; }

static int digit(int c)
{
	if (isdigit(c)) return c - '0';
	if (isupper(c)) return c - 'A' + 10;
	if (islower(c)) return c - 'a' + 10;
	return 99;
}

unsigned long strtoul(const char *s, char **end, int base)
{
	unsigned long value = 0;
	int negative = 0, d;
	const char *start = s;

	while (isspace(*s))
		s++;
	if (*s == '+' || *s == '-')
		negative = *s++ == '-';
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && digit(s[2]) < 16) {
		base = 16;
		s += 2;
	} else if (base == 0) {
		base = *s == '0' ? 8 : 10;
	}
	if (digit(*s) >= base) {
		if (end) *end = (char *)start;
		return 0;
	}
	while ((d = digit(*s)) < base) {
		value = value * (unsigned)base + (unsigned)d;
		s++;
	}
	if (end) *end = (char *)s;
	return negative ? -value : value;
}

long strtol(const char *s, char **end, int base)
{
	return (long)strtoul(s, end, base);
}

long atol(const char *s) { return strtol(s, 0, 10); }

int atoi(const char *s)
{
	int value = 0, negative = 0;

	while (isspace(*s))
		s++;
	if (*s == '+' || *s == '-')
		negative = *s++ == '-';
	while (isdigit(*s))
		value = value * 10 + (*s++ - '0');
	return negative ? -value : value;
}

/* The 16-bit multiplier of the Numerical Recipes kind: 32 bits of state
 * through libgcc's long multiply, the high 15 bits out. */
static unsigned long seed = 1;

int rand(void)
{
	seed = seed * 1103515245UL + 12345UL;
	return (int)((seed >> 16) & RAND_MAX);
}

void srand(unsigned s) { seed = s; }

void abort(void) { exit(EXIT_FAILURE); }
