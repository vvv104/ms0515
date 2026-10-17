/* tr.c - /bin/tr SET1 SET2, tr -d SET: the input to the output, each
 * character of SET1 turned into the one at its place in SET2 - the last
 * of SET2 for the rest - or with -d taken out.  a-z is a range; \n, \t
 * and \\ the newline, the tab and the backslash. */
#include "unistd.h"

static unsigned char map[256];
static char drop[256];

/* The set at s, ranges spread, into out: its length. */
static int spread(const char *s, unsigned char *out)
{
	int n = 0, c, to;

	while (*s && n < 256) {
		c = (unsigned char)*s++;
		if (c == '\\' && *s) {
			c = (unsigned char)*s++;
			c = c == 'n' ? '\n' : c == 't' ? '\t' : c;
		}
		if (*s == '-' && s[1]) {
			to = (unsigned char)s[1];
			s += 2;
			for (; c <= to && n < 256; c++)
				out[n++] = (unsigned char)c;
			continue;
		}
		out[n++] = (unsigned char)c;
	}
	return n;
}

int main(int argc, char **argv)
{
	unsigned char from[256], to[256], buf[256];
	int deleting = argc == 3 && !strcmp(argv[1], "-d");
	int nf, nt, i, n, o;

	if (argc != 3) {
		puts_fd(2, "usage: tr SET1 SET2, or tr -d SET\n");
		return 1;
	}
	for (i = 0; i < 256; i++)
		map[i] = (unsigned char)i;
	nf = spread(argv[deleting ? 2 : 1], from);
	nt = deleting ? 0 : spread(argv[2], to);
	for (i = 0; i < nf; i++)
		if (deleting)
			drop[from[i]] = 1;
		else if (nt)
			map[from[i]] = to[i < nt ? i : nt - 1];
	while ((n = read(0, buf, sizeof buf)) > 0) {
		for (i = o = 0; i < n; i++)
			if (!drop[buf[i]])
				buf[o++] = map[buf[i]];
		write(1, buf, o);
	}
	return 0;
}
