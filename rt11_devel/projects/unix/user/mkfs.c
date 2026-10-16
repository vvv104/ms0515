/* mkfs.c - /bin/mkfs DEVICE [BLOCKS]: an empty file system on the device,
 * of BLOCKS blocks (1024, the RAM disk's, by default).
 *
 * The layout is tools/mkfs.py's (kernel/fs.h): block 0 left for a boot
 * block, 1 the superblock, 2 the bitmap, 3..10 the table of 128 inodes,
 * the data from 11 - a volume made here has no swap area to keep - and the
 * root, inode 1, a directory of "." and ".." in the first data block.  The
 * blocks are written one after another from block 0 up to the root's. */
#include "unistd.h"
#include "../kernel/fs.h"

#define BITMAP 2
#define ITABLE 3
#define IBLOCKS 8
#define DATA (ITABLE + IBLOCKS)

static char block[BSIZE];

static int number(const char *s)
{
	int n = 0;

	while (*s >= '0' && *s <= '9')
		n = n * 10 + (*s++ - '0');
	return n;
}

static int put(int fd)
{
	int n = write(fd, block, BSIZE);

	memset(block, 0, BSIZE);
	return n == BSIZE ? 0 : -1;
}

int main(int argc, char **argv)
{
	struct superblock *sb = (struct superblock *)block;
	struct dinode *root = (struct dinode *)block;
	struct direct *d = (struct direct *)block;
	unsigned blocks = argc > 2 ? (unsigned)number(argv[2]) : 1024, b;
	int fd;

	if (argc < 2 || blocks <= DATA || blocks > 8 * BSIZE) {
		puts_fd(2, "usage: mkfs DEVICE [BLOCKS]\n");
		return 1;
	}
	if ((fd = open(argv[1], 1)) < 0) {
		complain("mkfs", argv[1]);
		return 1;
	}
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
	puts_fd(1, "mkfs: ");
	puts_fd(1, argv[1]);
	puts_fd(1, ": ");
	putnum(blocks);
	puts_fd(1, " blocks\n");
	return 0;
fail:
	complain("mkfs", argv[1]);
	return 1;
}
