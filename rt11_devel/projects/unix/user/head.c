/* head.c - /bin/head [-N] [FILE]: the first N lines of the file, or of the
 * input with none; ten without -N. */
#include "unistd.h"

int main(int argc, char **argv)
{
	char buf[256];
	unsigned lines = 10;
	int fd = 0, n, i;

	if (argc > 1 && argv[1][0] == '-') {
		lines = 0;
		for (i = 1; argv[1][i] >= '0' && argv[1][i] <= '9'; i++)
			lines = lines * 10 + (unsigned)(argv[1][i] - '0');
		argc--;
		argv++;
	}
	if (argc > 1 && (fd = open(argv[1], 0)) < 0) {
		complain("head", argv[1]);
		return 1;
	}
	while (lines && (n = read(fd, buf, sizeof buf)) > 0) {
		for (i = 0; i < n && lines; i++)
			if (buf[i] == '\n')
				lines--;
		write(1, buf, i);
	}
	return 0;
}
