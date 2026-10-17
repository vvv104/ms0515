/* re.c - the patterns of grep and ed (re.h): Kernighan and Pike's matcher,
 * with \x, and the match's ends told. */
#include "re.h"

/* The atom at re: its length, *c the character it stands for, -1 any. */
static int atom(const char *re, int *c)
{
	if (re[0] == '\\' && re[1]) {
		*c = (unsigned char)re[1];
		return 2;
	}
	*c = re[0] == '.' ? -1 : (unsigned char)re[0];
	return 1;
}

static int is(int c, int t)
{
	return c < 0 || c == (unsigned char)t;
}

/* The pattern matched at the start of the text: the match's end, or 0. */
static const char *here(const char *re, const char *text)
{
	const char *t, *end;
	int c, n;

	if (!re[0])
		return text;
	if (re[0] == '$' && !re[1])
		return *text ? 0 : text;
	n = atom(re, &c);
	if (re[n] == '*') {			/* the longest run first */
		for (t = text; *t && is(c, *t); t++)
			;
		for (;; t--) {
			if ((end = here(re + n + 1, t)) != 0)
				return end;
			if (t == text)
				return 0;
		}
	}
	return *text && is(c, *text) ? here(re + n, text + 1) : 0;
}

const char *re_match(const char *re, const char *text, const char **start)
{
	const char *end;

	if (re[0] == '^') {
		*start = text;
		return here(re + 1, text);
	}
	do {
		if ((end = here(re, text)) != 0) {
			*start = text;
			return end;
		}
	} while (*text++);
	return 0;
}
