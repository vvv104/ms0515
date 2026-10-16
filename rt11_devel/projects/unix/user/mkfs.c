/* mkfs.c - /bin/mkfs [-s KB] DEVICE [BLOCKS]: an empty file system on the
 * device, of BLOCKS blocks - by default the device's, as its name says: 800
 * a diskette's side (fd0..fd3), 1600 both sides (fdx0, fdx1), 1024 the RAM
 * disk - vol.c's layout, tools/mkfs.py's; with -s, a swap file /swap of KB
 * kilobytes in it, laid down with the volume. */
#include "unistd.h"
#include "vol.h"

/* The device's blocks by its name's last part. */
static unsigned device_blocks(const char *device)
{
	const char *name = device;

	for (; *device; device++)
		if (*device == '/')
			name = device + 1;
	if (name[0] != 'f' || name[1] != 'd')
		return 1024;
	return name[2] == 'x' ? 1600 : 800;
}

int main(int argc, char **argv)
{
	unsigned blocks, swap = 0;

	if (argc > 2 && !strcmp(argv[1], "-s")) {
		swap = number(argv[2]);
		argc -= 2;
		argv += 2;
	}
	if (argc < 2 || argc > 3) {
		puts_fd(2, "usage: mkfs [-s KB] DEVICE [BLOCKS]\n");
		return 1;
	}
	blocks = argc > 2 ? number(argv[2]) : device_blocks(argv[1]);
	if (mkfs_volume(argv[1], blocks, swap) < 0) {
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
