/* vol.c - volumes, for mkfs and mount -a (vol.h).
 *
 * mkfs_volume writes tools/mkfs.py's layout (kernel/fs.h): block 0 left for
 * a boot block, 1 the superblock, 2 the bitmap, 3..10 the table of 128
 * inodes, the data from 11 - a volume made here keeps no swap area - and
 * the root, inode 1, a directory of "." and ".." in the first data block;
 * the blocks one after another from block 0 up to the root's.  A swap file
 * asked for is inode 2, "swap" in the root, the blocks after the root's in
 * one extent: taken in the bitmap, their bytes whatever the device held -
 * swap reads back only what it wrote. */
#include "unistd.h"
#include "vol.h"
#include "../kernel/fs.h"

#define BITMAP 2
#define ITABLE 3
#define IBLOCKS 8
#define DATA (ITABLE + IBLOCKS)

static char block[BSIZE];

static int put(int fd)
{
	int n = write(fd, block, BSIZE);

	memset(block, 0, BSIZE);
	return n == BSIZE ? 0 : -1;
}

int mkfs_volume(const char *device, unsigned blocks, unsigned swap_kb)
{
	struct superblock *sb = (struct superblock *)block;
	struct dinode *ino = (struct dinode *)block;
	struct direct *d = (struct direct *)block;
	unsigned b, swap = swap_kb * 2;
	int fd;

	errno = 0;
	if (blocks <= DATA + 1 + swap || blocks > 8 * BSIZE) {
		errno = EINVAL;
		return -1;
	}
	if ((fd = open(device, 1)) < 0)
		return -1;
	memset(block, 0, BSIZE);
	if (put(fd) < 0)			/* 0: no boot block */
		goto fail;
	sb->magic = FS_MAGIC;			/* 1: the superblock */
	sb->version = FS_VERSION;
	sb->blocks = blocks;
	sb->bitmap = BITMAP;
	sb->itable = ITABLE;
	sb->iblocks = IBLOCKS;
	sb->data = DATA;
	sb->inodes = IBLOCKS * (BSIZE / sizeof(struct dinode));
	sb->volume = (unsigned long)time() * 40503u + (unsigned)getpid();
	memcpy(sb->label, "RAMDISK", 7);
	if (put(fd) < 0)
		goto fail;
	for (b = 0; b <= DATA + swap; b++)	/* 2: the bitmap: up to the root's block and the swap file's */
		block[b >> 3] |= (char)(1 << (b & 7));
	if (put(fd) < 0)
		goto fail;
	ino[0].mode = S_IFDIR | S_RIGHTS;		/* 3..10: the inodes, the root first */
	ino[0].nlink = 2;
	ino[0].nextents = 1;
	ino[0].size = (swap ? 3 : 2) * sizeof(struct direct);
	ino[0].ext[0].start = DATA;
	ino[0].ext[0].len = 1;
	if (swap) {
		ino[1].mode = S_IFREG;		/* ---: the kernel's alone */
		ino[1].nlink = 1;
		ino[1].nextents = 1;
		ino[1].size = (unsigned long)swap * BSIZE;
		ino[1].ext[0].start = DATA + 1;
		ino[1].ext[0].len = swap;
	}
	for (b = ITABLE; b < DATA; b++)
		if (put(fd) < 0)
			goto fail;
	d[0].ino = ROOT_INO;			/* 11: the root's "." and ".." - and "swap" */
	d[0].name[0] = '.';
	d[1].ino = ROOT_INO;
	d[1].name[0] = d[1].name[1] = '.';
	if (swap) {
		d[2].ino = ROOT_INO + 1;
		memcpy(d[2].name, "swap", 4);
	}
	if (put(fd) < 0)
		goto fail;
	close(fd);
	return 0;
fail:
	close(fd);
	if (!errno)
		errno = ENOSPC;
	return -1;
}

unsigned number(const char *s)
{
	unsigned n = 0;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (unsigned)(*s++ - '0');
	return n;
}
