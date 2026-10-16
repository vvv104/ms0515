/* mkfs.c - /bin/mkfs DEVICE [BLOCKS]: an empty file system on the device,
 * of BLOCKS blocks (1024, the RAM disk's, by default) - vol.c's layout,
 * tools/mkfs.py's. */
#include "unistd.h"
#include "vol.h"

int main(int argc, char **argv)
{
	unsigned blocks = argc > 2 ? number(argv[2]) : 1024;

	if (argc < 2 || argc > 3) {
		puts_fd(2, "usage: mkfs DEVICE [BLOCKS]\n");
		return 1;
	}
	if (mkfs_volume(argv[1], blocks) < 0) {
		complain("mkfs", argv[1]);
		return 1;
	}
	puts_fd(1, "mkfs: ");
	puts_fd(1, argv[1]);
	puts_fd(1, ": ");
	putnum(blocks);
	puts_fd(1, " blocks\n");
	return 0;
}
