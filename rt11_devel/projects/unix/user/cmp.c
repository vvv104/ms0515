/* cmp.c - /bin/cmp [-s] FILE1 FILE2: whether the files are the same - the
 * first byte and line where they differ told, or which one ended first;
 * -s nothing told.  The status: 0 the same, 1 not, 2 a file not read. */
#include "unistd.h"

static struct reader {
	int fd, have, at;
	char buf[256];
} a, b;

/* The next byte of r, -1 at its end. */
static int next(struct reader *r)
{
	if (r->at == r->have) {
		r->at = 0;
		if ((r->have = read(r->fd, r->buf, sizeof r->buf)) <= 0) {
			r->have = 0;
			return -1;
		}
	}
	return (unsigned char)r->buf[r->at++];
}

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

int main(int argc, char **argv)
{
	unsigned long byte = 0, line = 1;
	int silent = argc == 4 && !strcmp(argv[1], "-s"), x, y;

	argv += silent;
	if (argc != 3 + silent) {
		puts_fd(2, "usage: cmp [-s] FILE1 FILE2\n");
		return 2;
	}
	if ((a.fd = open(argv[1], 0)) < 0 || (b.fd = open(argv[2], 0)) < 0) {
		complain("cmp", a.fd < 0 ? argv[1] : argv[2]);
		return 2;
	}
	for (;;) {
		x = next(&a);
		y = next(&b);
		byte++;
		if (x == y && x < 0)
			return 0;
		if (x != y)
			break;
		if (x == '\n')
			line++;
	}
	if (silent)
		return 1;
	if (x < 0 || y < 0) {
		puts_fd(1, "cmp: EOF on ");
		puts_fd(1, argv[x < 0 ? 1 : 2]);
	} else {
		puts_fd(1, argv[1]);
		puts_fd(1, " ");
		puts_fd(1, argv[2]);
		puts_fd(1, " differ: byte ");
		number(byte);
		puts_fd(1, ", line ");
		number(line);
	}
	puts_fd(1, "\n");
	return 1;
}
