/* wc.c - /bin/wc [FILE...]: the lines, words and bytes of each file, or of
 * the input with none; a total after several. */
#include "unistd.h"

static unsigned long total[3];

static void number(unsigned long n)
{
	char digits[11];
	int i = sizeof digits;

	do {
		digits[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	write(1, digits + i, (int)sizeof digits - i);
}

static void tell(const unsigned long *c, const char *name)
{
	int i;

	for (i = 0; i < 3; i++) {
		if (i)
			puts_fd(1, " ");
		number(c[i]);
	}
	if (name) {
		puts_fd(1, " ");
		puts_fd(1, name);
	}
	puts_fd(1, "\n");
}

/* The counts of fd: lines, words - runs of what is not a space, a tab or a
 * line end - and bytes; told, and added to the total. */
static void count(int fd, const char *name)
{
	char buf[256];
	unsigned long c[3] = {0, 0, 0};
	int n, i, in = 0, space;

	while ((n = read(fd, buf, sizeof buf)) > 0) {
		c[2] += (unsigned)n;
		for (i = 0; i < n; i++) {
			if (buf[i] == '\n')
				c[0]++;
			space = buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r';
			if (!space && !in)
				c[1]++;
			in = !space;
		}
	}
	tell(c, name);
	for (i = 0; i < 3; i++)
		total[i] += c[i];
}

int main(int argc, char **argv)
{
	int i, fd, bad = 0;

	if (argc < 2) {
		count(0, 0);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		if ((fd = open(argv[i], 0)) < 0) {
			complain("wc", argv[i]);
			bad = 1;
			continue;
		}
		count(fd, argv[i]);
		close(fd);
	}
	if (argc > 2)
		tell(total, "total");
	return bad;
}
