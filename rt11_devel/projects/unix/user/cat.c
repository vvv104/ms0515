/* cat.c - /bin/cat: each file given, one after another, on the output;
 * the input without one. */
#include "unistd.h"

int main(int argc, char **argv)
{
	char buf[256];
	int i, fd, n, bad = 0;

	if (argc < 2) {
		while ((n = read(0, buf, sizeof buf)) > 0)
			write(1, buf, n);
		return 0;
	}
	for (i = 1; i < argc; i++) {
		if ((fd = open(argv[i], 0)) < 0) {
			complain("cat", argv[i]);
			bad = 1;
			continue;
		}
		while ((n = read(fd, buf, sizeof buf)) > 0)
			write(1, buf, n);
		close(fd);
	}
	return bad;
}
