/* sort.c - /bin/sort [-r] [-n] [-u] [FILE...]: the lines of the files, or
 * of the input with none, in order - the characters' codes, or with -n
 * the numbers they begin with; -r the other way, -u each line once.  The
 * lines are held in memory, TEXT bytes and LINES lines at most. */
#include "unistd.h"

#define TEXT 16384
#define LINES 1024

static char text[TEXT];
static char *line[LINES];
static unsigned size;
static int lines, reverse, numeric, unique;

/* fd's bytes after the rest: 0, or -1 when there is no room. */
static int take(int fd)
{
	int n;

	while (size < TEXT && (n = read(fd, text + size, TEXT - size)) > 0)
		size += (unsigned)n;
	return size == TEXT ? -1 : 0;
}

static long value(const char *s)
{
	long n = 0;
	int minus = 0;

	while (*s == ' ' || *s == '\t')
		s++;
	if (*s == '-')
		minus = 1, s++;
	while (*s >= '0' && *s <= '9')
		n = n * 10 + *s++ - '0';
	return minus ? -n : n;
}

static int compare(const char *a, const char *b)
{
	long x, y;
	int c;

	if (numeric) {
		x = value(a);
		y = value(b);
		c = x < y ? -1 : x > y;
	} else {
		c = strcmp(a, b);
	}
	return reverse ? -c : c;
}

int main(int argc, char **argv)
{
	unsigned i;
	int fd, a, b, gap, bad = 0;
	char *t;
	const char *o;

	for (; argc > 1 && argv[1][0] == '-' && argv[1][1]; argc--, argv++)
		for (o = argv[1] + 1; *o; o++) {
			reverse |= *o == 'r';
			numeric |= *o == 'n';
			unique |= *o == 'u';
		}
	if (argc < 2)
		bad = take(0);
	for (a = 1; a < argc && !bad; a++) {
		if ((fd = open(argv[a], 0)) < 0) {
			complain("sort", argv[a]);
			return 2;
		}
		bad = take(fd);
		close(fd);
	}
	if (size && text[size - 1] != '\n' && size < TEXT)
		text[size++] = '\n';
	for (i = 0, t = text; i < size && !bad; i++)	/* the lines cut at their newlines */
		if (text[i] == '\n') {
			text[i] = 0;
			if (lines == LINES)
				bad = 1;
			else
				line[lines++] = t;
			t = text + i + 1;
		}
	if (bad) {
		puts_fd(2, "sort: too much to sort\n");
		return 2;
	}
	for (gap = lines / 2; gap > 0; gap /= 2)	/* Shell's sort */
		for (a = gap; a < lines; a++)
			for (b = a - gap; b >= 0 && compare(line[b], line[b + gap]) > 0; b -= gap) {
				t = line[b];
				line[b] = line[b + gap];
				line[b + gap] = t;
			}
	for (a = 0; a < lines; a++) {
		if (unique && a > 0 && !compare(line[a - 1], line[a]))
			continue;
		puts_fd(1, line[a]);
		puts_fd(1, "\n");
	}
	return 0;
}
