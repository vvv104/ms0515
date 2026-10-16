/* swapon.c - /bin/swapon FILE [KB]: the file swap from now on; made first,
 * KB kilobytes of zeros, when it is not there and a size is given.
 *
 * The kernel takes a file in one extent only: made on a volume with room in
 * one piece, as a fresh RAM disk's, it is laid down so.  Each 32 KB of it is
 * a process's area, one kept for the exchange. */
#include "unistd.h"
#include "vol.h"

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		puts_fd(2, "usage: swapon FILE [KB]\n");
		return 1;
	}
	if (swap_file_on(argv[1], argc > 2 ? number(argv[2]) : 0) < 0) {
		complain("swapon", argv[1]);
		return 1;
	}
	puts_fd(1, "swapon: ");
	puts_fd(1, argv[1]);
	puts_fd(1, "\n");
	return 0;
}
