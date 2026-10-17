/* files.c - the little library's reader of the system's file system
 * (own.h): a file found by its path on the volume the program came from,
 * its blocks read through the ROM's 160010.  The layout is kernel/fs.h's;
 * no mounts are seen - the kernel that kept them is gone. */
#include "own.h"
#include "../../kernel/fs.h"

#define SIDE 800

static char block[BSIZE];

/* Block b of the program's volume: from 800 on side 1 when it is both
 * sides of the drive. */
static int volume_block(unsigned b, void *buf)
{
	unsigned unit = own_unit;

	if (own_both && b >= SIDE) {
		unit |= 2;
		b -= SIDE;
	}
	return own_block(unit, b, buf, 0);
}

/* Inode n into f. */
static int inode(unsigned n, struct own_file *f)
{
	const struct superblock *sb = (const struct superblock *)block;
	const struct dinode *d;
	unsigned i;

	if (volume_block(1, block) != 0 || sb->magic != FS_MAGIC)
		return -1;
	if (volume_block(sb->itable + (n - 1) / 16, block) != 0)
		return -1;
	d = (const struct dinode *)block + (n - 1) % 16;
	f->mode = d->mode;
	f->size = d->size;
	f->nextents = d->nextents > OWN_EXTENTS ? OWN_EXTENTS : d->nextents;
	for (i = 0; i < OWN_EXTENTS; i++) {
		f->ext[i].start = d->ext[i].start;
		f->ext[i].len = d->ext[i].len;
	}
	return 0;
}

int own_read(const struct own_file *f, unsigned b, void *buf)
{
	unsigned i;

	if ((unsigned long)b * BSIZE >= f->size)
		return -1;
	for (i = 0; i < f->nextents; i++) {
		if (b < f->ext[i].len)
			return volume_block(f->ext[i].start + b, buf);
		b -= f->ext[i].len;
	}
	return -1;
}

/* The entry `name` (len bytes) of directory dir: its inode, 0 when none. */
static unsigned lookup(const struct own_file *dir, const char *name, unsigned len)
{
	const struct direct *e;
	unsigned b, k, i;

	for (b = 0; own_read(dir, b, block) == 0; b++)
		for (k = 0, e = (const struct direct *)block; k < BSIZE / sizeof *e; k++, e++) {
			if (!e->ino || (unsigned long)b * BSIZE + k * sizeof *e >= dir->size)
				continue;
			for (i = 0; i < len && i < DIRSIZ && e->name[i] == name[i]; i++)
				;
			if (i == len && (len == DIRSIZ || !e->name[len]))
				return e->ino;
		}
	return 0;
}

int own_open(const char *path, struct own_file *f)
{
	unsigned n = ROOT_INO, len;

	if (inode(n, f) != 0)
		return -1;
	for (;;) {
		while (*path == '/')
			path++;
		if (!*path)
			return 0;
		for (len = 0; path[len] && path[len] != '/'; len++)
			;
		if ((f->mode & S_IFMT) != S_IFDIR || !(n = lookup(f, path, len)) || inode(n, f) != 0)
			return -1;
		path += len;
	}
}
