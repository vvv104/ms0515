/* mkboot.c - /bin/mkboot [-a] DEVICE: the system copied to a diskette
 * that boots.
 *
 * The root's volume - the RAM disk's when the system runs from it, else
 * the boot diskette's - goes to DEVICE block for block (clone.c), the
 * system alone - /home and /tmp left empty - or with -a all of it: the boot
 * block and /boot/kernel's blocks where the superblock says, the rest as
 * the bitmap has it, the copy told DEVICE's size - 1600 blocks on fdx, 800
 * on a side - and a volume number of its own.  The diskette is to be
 * formatted (fdformat); one mounted, the root's own among them, the kernel
 * refuses to write.  With the system in memory the boot drive itself
 * takes a new diskette: umount /dev/fdx0, the diskette changed, mkboot
 * /dev/fdx0. */
#include "unistd.h"
#include "clone.h"

int main(int argc, char **argv)
{
	struct stat st;
	const char *root;
	unsigned blocks;
	int from, to, bad, all = argc == 3 && !strcmp(argv[1], "-a");

	if (argc != 2 + all) {
		puts_fd(2, "usage: mkboot [-a] DEVICE\n");
		return 1;
	}
	argv += all;
	if (stat(argv[1], &st) < 0) {
		complain("mkboot", argv[1]);
		return 1;
	}
	if ((st.mode & 0170000) != 0060000) {
		errno = ENOTBLK;
		complain("mkboot", argv[1]);
		return 1;
	}
	if (stat("/", &st) < 0 || !(root = device_path(st.dev)) || (from = open(root, 0)) < 0) {
		complain("mkboot", "/");
		return 1;
	}
	if ((to = open(argv[1], 2)) < 0) {
		complain("mkboot", argv[1]);
		close(from);
		return 1;
	}
	if (!(blocks = device_blocks(to))) {
		errno = 0;			/* no diskette in the drive, or not formatted */
		complain("mkboot", argv[1]);
		bad = -1;
	} else if ((bad = clone_volume(from, to, blocks, "UNIX", !all)) < 0) {
		complain("mkboot", argv[1]);
	}
	close(from);
	close(to);
	if (bad < 0)
		return 1;
	puts_fd(1, "mkboot: ");
	puts_fd(1, argv[1]);
	puts_fd(1, ": ");
	putnum(blocks);
	puts_fd(1, " blocks, the system copied\n");
	return 0;
}
