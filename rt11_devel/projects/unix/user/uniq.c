/* uniq.c - /bin/uniq [-c] [-d] [FILE]: the lines of the file, or of the
 * input, a run of the same line once; -c each with how many times it
 * came, -d only those that came more than once. */
#include "unistd.h"

#define LINE 256

static struct {
	int fd, have, at;
	char buf[256];
} in;

/* The next line into line, its newline gone: its length, -1 at the end. */
static int next(char *line)
{
	int n = 0;
	char c;

	for (;;) {
		if (in.at == in.have) {
			in.at = 0;
			if ((in.have = read(in.fd, in.buf, sizeof in.buf)) <= 0) {
				in.have = 0;
				if (!n)
					return -1;
				break;
			}
		}
		if ((c = in.buf[in.at++]) == '\n')
			break;
		if (n < LINE - 1)
			line[n++] = c;
	}
	line[n] = 0;
	return n;
}

static int counted, repeated;

static void tell(const char *line, unsigned times)
{
	if (repeated && times < 2)
		return;
	if (counted) {
		putnum(times);
		puts_fd(1, " ");
	}
	puts_fd(1, line);
	puts_fd(1, "\n");
}

int main(int argc, char **argv)
{
	char last[LINE], line[LINE];
	unsigned times = 0;
	const char *o;

	for (; argc > 1 && argv[1][0] == '-' && argv[1][1]; argc--, argv++)
		for (o = argv[1] + 1; *o; o++) {
			counted |= *o == 'c';
			repeated |= *o == 'd';
		}
	if (argc > 1 && (in.fd = open(argv[1], 0)) < 0) {
		complain("uniq", argv[1]);
		return 1;
	}
	while (next(line) >= 0) {
		if (times && !strcmp(line, last)) {
			times++;
			continue;
		}
		if (times)
			tell(last, times);
		memcpy(last, line, LINE);
		times = 1;
	}
	if (times)
		tell(last, times);
	return 0;
}
