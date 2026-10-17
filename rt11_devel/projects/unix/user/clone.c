/* clone.c - a volume copied block for block (clone.h): the system into the
 * RAM disk (ramroot), onto a diskette that boots (mkboot). */
#include "unistd.h"
#include "clone.h"
#include "../kernel/fs.h"

static const char *const devices[] = {
	"/dev/fd0", "/dev/fd1", "/dev/fd2", "/dev/fd3", "/dev/fdx0", "/dev/fdx1", "/dev/rd0"
};

static char block[BSIZE], map[BSIZE];

const char *device_path(unsigned dev)
{
	return dev < sizeof devices / sizeof *devices ? devices[dev] : 0;
}

/* Block b of the device at fd into block, or out of it: 0, or -1. */
static int at(int fd, unsigned b, int writing)
{
	if (lseek(fd, (long)b * BSIZE, 0) < 0)
		return -1;
	return (writing ? write(fd, block, BSIZE) : read(fd, block, BSIZE)) == BSIZE ? 0 : -1;
}

unsigned device_blocks(int fd)
{
	static const unsigned sizes[] = {1600, 1024, 800};
	unsigned i;

	for (i = 0; i < sizeof sizes / sizeof *sizes; i++)
		if (at(fd, sizes[i] - 1, 0) == 0)
			return sizes[i];
	return 0;
}

int clone_volume(int from, int to, unsigned blocks, const char *label)
{
	struct superblock *sb = (struct superblock *)block;
	unsigned size, data, b;

	errno = EINVAL;
	if (at(from, 1, 0) < 0 || sb->magic != FS_MAGIC || sb->blocks > 8 * BSIZE)
		return -1;
	size = sb->blocks;
	data = sb->data;
	if (at(from, sb->bitmap, 0) < 0)
		return -1;
	memcpy(map, block, BSIZE);
	for (b = 0; b < size; b++) {
		if (b >= data && !(map[b >> 3] & (1 << (b & 7))))
			continue;
		if (b >= blocks) {
			errno = ENOSPC;
			return -1;
		}
		if (at(from, b, 0) < 0 || at(to, b, 1) < 0)
			return -1;
	}
	if (at(to, 1, 0) < 0)			/* the copy's superblock */
		return -1;
	sb->blocks = blocks;
	sb->volume = (unsigned long)time() * 40503u + (unsigned)getpid();
	memset(sb->label, 0, sizeof sb->label);
	memcpy(sb->label, label, strlen(label));
	return at(to, 1, 1);
}
