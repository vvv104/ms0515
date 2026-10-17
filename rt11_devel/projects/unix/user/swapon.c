/* swapon.c - /bin/swapon FILE: the file swap from now on.
 *
 * The kernel takes a file in one extent only - mkfs -s lays one down with
 * a volume.  Each 32 KB of it is a process's area, one kept for the
 * exchange: 64 KB for the three processes there may be. */
#include "unistd.h"

int main(int argc, char **argv)
{
	if (argc != 2) {
		puts_fd(2, "usage: swapon FILE\n");
		return 1;
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
