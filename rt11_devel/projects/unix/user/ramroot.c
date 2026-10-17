/* ramroot.c - /bin/ramroot: the system into the RAM disk and the root
 * there, run by /etc/rc first.
 *
 * The boot diskette's volume is copied to /dev/rd0 block for block - the
 * blocks below the data area and those its bitmap has taken, so a few
 * hundred of the 1600 - its superblock then told the RAM disk's size;
 * mount(rd0, "/") makes the copy the root, and the diskette's volume goes
 * to /disk, from where a file is saved and a program that owns the
 * machine is run, and where umount lets the diskette go.  With no RAM disk
 * nothing is done and nothing said; with a root that is not a diskette's
 * - ramroot run again - neither. */
#include "unistd.h"
#include "../kernel/fs.h"

#define RD "/dev/rd0"
#define RD_BLOCKS 1024
#define DISK "/disk"

/* The diskettes' devices by their numbers (kernel.h DEV_FD0..DEV_FDX1). */
static const char *const devices[] = {
	"/dev/fd0", "/dev/fd1", "/dev/fd2", "/dev/fd3", "/dev/fdx0", "/dev/fdx1"
};

static char block[BSIZE], map[BSIZE];

/* Block b of the device at fd into block, or out of it: 0, or -1. */
static int at(int fd, unsigned b, int writing)
{
	if (lseek(fd, (long)b * BSIZE, 0) < 0)
		return -1;
	return (writing ? write(fd, block, BSIZE) : read(fd, block, BSIZE)) == BSIZE ? 0 : -1;
}

/* The volume on from copied to to, which takes RD_BLOCKS: 0, or -1 with
 * errno - ENOSPC when a block taken lies past the RAM disk's end. */
static int copy_volume(int from, int to)
{
	struct superblock *sb = (struct superblock *)block;
	unsigned blocks, data, b;

	errno = EINVAL;
	if (at(from, 1, 0) < 0 || sb->magic != FS_MAGIC || sb->blocks > 8 * BSIZE)
		return -1;
	blocks = sb->blocks;
	data = sb->data;
	if (at(from, sb->bitmap, 0) < 0)
		return -1;
	memcpy(map, block, BSIZE);
	for (b = 0; b < blocks; b++) {
		if (b >= data && !(map[b >> 3] & (1 << (b & 7))))
			continue;
		if (b >= RD_BLOCKS) {
			errno = ENOSPC;
			return -1;
		}
		if (at(from, b, 0) < 0 || at(to, b, 1) < 0)
			return -1;
	}
	if (at(to, 1, 0) < 0)			/* the copy's superblock: its size */
		return -1;
	sb->blocks = RD_BLOCKS;
	memcpy(sb->label, "RAMDISK", 8);
	return at(to, 1, 1);
}

int main(void)
{
	struct stat st;
	const char *disk;
	int from, to, bad;

	if (stat("/", &st) < 0 || st.dev >= sizeof devices / sizeof *devices)
		return 0;			/* the root not a diskette's: done already */
	disk = devices[st.dev];
	if ((to = open(RD, 2)) < 0)
		return 0;
	if (at(to, 0, 0) < 0) {			/* no RAM disk */
		close(to);
		return 0;
	}
	if ((from = open(disk, 0)) < 0) {
		complain("ramroot", disk);
		close(to);
		return 1;
	}
	bad = copy_volume(from, to);
	close(from);
	close(to);
	if (bad < 0 || mount(RD, "/") < 0) {
		complain("ramroot", RD);
		return 1;
	}
	if (mkdir(DISK) < 0 || mount(disk, DISK) < 0) {
		complain("ramroot", DISK);
		return 1;
	}
	puts_fd(1, "ramroot: the root in " RD ", the diskette at " DISK "\n");
	return 0;
}
