/* vol.c - volumes and swap files, for mkfs, swapon and mount -a (vol.h).
 *
 * mkfs_volume writes tools/mkfs.py's layout (kernel/fs.h): block 0 left for
 * a boot block, 1 the superblock, 2 the bitmap, 3..10 the table of 128
 * inodes, the data from 11 - a volume made here keeps no swap area - and
 * the root, inode 1, a directory of "." and ".." in the first data block;
 * the blocks one after another from block 0 up to the root's. */
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

int mkfs_volume(const char *device, unsigned blocks)
{
	struct superblock *sb = (struct superblock *)block;
	struct dinode *root = (struct dinode *)block;
	struct direct *d = (struct direct *)block;
	unsigned b;
	int fd;

	errno = 0;
	if (blocks <= DATA || blocks > 8 * BSIZE) {
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
	for (b = 0; b <= DATA; b++)		/* 2: the bitmap, the areas and the root's block taken */
		block[b >> 3] |= (char)(1 << (b & 7));
	if (put(fd) < 0)
		goto fail;
	root->mode = S_IFDIR | 0755;		/* 3..10: the inodes, the root first */
	root->nlink = 2;
	root->nextents = 1;
	root->size = 2 * sizeof(struct direct);
	root->ext[0].start = DATA;
	root->ext[0].len = 1;
	for (b = ITABLE; b < DATA; b++)
		if (put(fd) < 0)
			goto fail;
	d[0].ino = ROOT_INO;			/* 11: the root's "." and ".." */
	d[0].name[0] = '.';
	d[1].ino = ROOT_INO;
	d[1].name[0] = d[1].name[1] = '.';
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

/* The zeros of a swap file, ZEROS blocks a write: the kernel writes the
 * file's inode and the bitmap once a write, not once a block. */
#define ZEROS 8
static char zeros[ZEROS * BSIZE];

int zero_file(const char *path, unsigned kb)
{
	int fd = creat(path, 0600);
	unsigned left = kb * 2, n;

	if (fd < 0)
		return -1;
	for (; left; left -= n) {
		n = left < ZEROS ? left : ZEROS;
		if (write(fd, zeros, n * BSIZE) != (int)(n * BSIZE)) {
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}

int swap_file_on(const char *path, unsigned kb)
{
	int fd;

	if (kb && (fd = open(path, 0)) < 0) {
		if (zero_file(path, kb) < 0)
			return -1;
	} else if (kb) {
		close(fd);
	}
	return swapon(path);
}

unsigned number(const char *s)
{
	unsigned n = 0;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (unsigned)(*s++ - '0');
	return n;
}
