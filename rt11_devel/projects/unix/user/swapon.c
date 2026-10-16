/* swapon.c - /bin/swapon FILE [KB]: the file swap from now on; made first,
 * KB kilobytes of zeros, when it is not there and a size is given.
 *
 * The kernel takes a file in one extent only: made on a volume with room in
 * one piece, as a fresh RAM disk's, it is laid down so.  Each 32 KB of it is
 * a process's area, one kept for the exchange. */
#include "unistd.h"

static char zeros[512];

static int number(const char *s)
{
	int n = 0;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (*s++ - '0');
	return n;
}

/* The file made, kb kilobytes of zeros; 0, or -1. */
static int make(const char *file, int kb)
{
	int fd = creat(file, 0600), i;

	if (fd < 0)
		return -1;
	for (i = 0; i < kb * 2; i++)
		if (write(fd, zeros, sizeof zeros) != sizeof zeros) {
			close(fd);
			return -1;
		}
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	int kb = argc > 2 ? number(argv[2]) : 0, fd;

	if (argc < 2 || argc > 3) {
		puts_fd(2, "usage: swapon FILE [KB]\n");
		return 1;
	}
	if (kb && (fd = open(argv[1], 0)) < 0) {
		if (make(argv[1], kb) < 0) {
			complain("swapon", argv[1]);
			return 1;
		}
	} else if (kb) {
		close(fd);
	}
	if (swapon(argv[1]) < 0) {
		complain("swapon", argv[1]);
		return 1;
	}
	puts_fd(1, "swapon: ");
	puts_fd(1, argv[1]);
	puts_fd(1, "\n");
	return 0;
}
