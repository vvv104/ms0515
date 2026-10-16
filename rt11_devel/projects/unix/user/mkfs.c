/* mkfs.c - /bin/mkfs DEVICE [BLOCKS]: an empty file system on the device,
 * of BLOCKS blocks - by default the device's, as its name says: 800 a
 * diskette's side (fd0..fd3), 1600 both sides (fdx0, fdx1), 1024 the RAM
 * disk - vol.c's layout, tools/mkfs.py's. */
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
	unsigned blocks = argc > 2 ? number(argv[2]) : argc > 1 ? device_blocks(argv[1]) : 0;

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
