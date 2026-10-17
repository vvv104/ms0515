/* od.c - /bin/od [-b|-c|-o|-x] [FILE]: the file, or the input, as numbers:
 * 16 bytes a line, its offset first in octal; octal words (-o, without
 * an option), octal bytes (-b), characters (-c: \n \t \0 and octal for
 * those not printed), hex words (-x).  The offset past the end last. */
#include "unistd.h"

static void field(unsigned n, int base, int width)
{
	char digits[8];
	int i = width;

	while (i > 0) {
		digits[--i] = "0123456789abcdef"[n % (unsigned)base];
		n /= (unsigned)base;
	}
	write(1, digits, width);
}

/* A byte as a character, four columns wide. */
static void character(int c)
{
	const char *s = c == '\n' ? "\\n" : c == '\t' ? "\\t" : c == 0 ? "\\0" : c == '\r' ? "\\r" : 0;
	char ch = (char)c;

	if (s) {
		puts_fd(1, "  ");
		puts_fd(1, s);
	} else if (c < ' ' || c >= 0177) {
		puts_fd(1, " ");
		field((unsigned)c, 8, 3);
	} else {
		puts_fd(1, "   ");
		write(1, &ch, 1);
	}
}

/* A line of n bytes at offset off. */
static void dump(unsigned long off, const unsigned char *b, int n, int how)
{
	int i;

	field((unsigned)off, 8, 7);
	for (i = 0; i < n; i++) {
		if (how == 'c') {
			character(b[i]);
		} else if (how == 'b') {
			puts_fd(1, " ");
			field(b[i], 8, 3);
		} else if (!(i & 1)) {
			puts_fd(1, " ");
			field(b[i] | (i + 1 < n ? b[i + 1] : 0) << 8, how == 'x' ? 16 : 8, how == 'x' ? 4 : 6);
		}
	}
	puts_fd(1, "\n");
}

int main(int argc, char **argv)
{
	unsigned char line[16];
	unsigned long off = 0;
	int how = 'o', fd = 0, n = 0, got;

	if (argc > 1 && argv[1][0] == '-' && argv[1][1]) {
		how = argv[1][1];
		argc--;
		argv++;
	}
	if (argc > 1 && (fd = open(argv[1], 0)) < 0) {
		complain("od", argv[1]);
		return 1;
	}
	while ((got = read(fd, line + n, 16 - n)) > 0) {
		n += got;
		if (n == 16) {
			dump(off, line, 16, how);
			off += 16;
			n = 0;
		}
	}
	if (n) {
		dump(off, line, n, how);
		off += (unsigned)n;
	}
	field((unsigned)off, 8, 7);
	puts_fd(1, "\n");
	return 0;
}
