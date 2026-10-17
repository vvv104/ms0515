/* grep.c - /bin/grep [-vnc] PATTERN [FILE...]: the lines of each file, or of
 * the input with none, that hold the pattern - with -v those that do not,
 * -n each with its number, -c only how many; the file's name before each
 * when there are several.  The pattern is re.c's: ^ the line's start, $
 * its end, . any character, x* any number of x, \x the x itself.
 * The status: 0 a line found, 1 none, 2 a file not read. */
#include "unistd.h"
#include "re.h"

#define LINE 256

static int invert, numbered, counted, named;

/* Whether the line holds the pattern (re.c). */
static int match(const char *re, const char *line)
{
	const char *start;

	return re_match(re, line, &start) != 0;
}

/* The lines of fd against the pattern: how many were told. */
static unsigned search(const char *re, int fd, const char *name)
{
	char buf[LINE], line[LINE];
	unsigned found = 0, number = 0;
	int have = 0, at = 0, n = 0, end = 0;

	while (!end) {
		if (at == have) {
			at = 0;
			if ((have = read(fd, buf, sizeof buf)) <= 0) {
				have = 0;
				end = 1;
				if (!n)
					break;
			}
		}
		if (!end && buf[at] != '\n') {
			if (n < LINE - 1)
				line[n++] = buf[at];
			at++;
			continue;
		}
		at += !end;
		line[n] = 0;
		number++;
		n = 0;
		if (match(re, line) == invert)
			continue;
		found++;
		if (counted)
			continue;
		if (named) {
			puts_fd(1, name);
			puts_fd(1, ":");
		}
		if (numbered) {
			putnum(number);
			puts_fd(1, ":");
		}
		puts_fd(1, line);
		puts_fd(1, "\n");
	}
	if (counted) {
		if (named) {
			puts_fd(1, name);
			puts_fd(1, ":");
		}
		putnum(found);
		puts_fd(1, "\n");
	}
	return found;
}

int main(int argc, char **argv)
{
	unsigned found = 0;
	int i, fd, bad = 0;
	const char *o;

	for (; argc > 1 && argv[1][0] == '-' && argv[1][1]; argc--, argv++)
		for (o = argv[1] + 1; *o; o++) {
			invert |= *o == 'v';
			numbered |= *o == 'n';
			counted |= *o == 'c';
		}
	if (argc < 2) {
		puts_fd(2, "usage: grep [-vnc] PATTERN [FILE...]\n");
		return 2;
	}
	if (argc == 2)
		return search(argv[1], 0, 0) ? 0 : 1;
	named = argc > 3;
	for (i = 2; i < argc; i++) {
		if ((fd = open(argv[i], 0)) < 0) {
			complain("grep", argv[i]);
			bad = 1;
			continue;
		}
		found += search(argv[1], fd, argv[i]);
		close(fd);
	}
	return bad ? 2 : found ? 0 : 1;
}
