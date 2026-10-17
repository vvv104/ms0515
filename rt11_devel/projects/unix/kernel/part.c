/* part.c - the parts of the system that are files: /boot/fd and
 * /boot/rd, the drivers (dev.c), read in flat once the root is mounted. */
#include "sys.h"

#define HEADER_WORDS 3			/* the magic, the version, the size */

int part_load(const char *path, unsigned base, unsigned room, unsigned magic, unsigned version)
{
	const unsigned *h = (const unsigned *)base;
	struct inode *ip;
	unsigned size;
	int error, right;

	if (!(ip = namei(path, &error)))
		return 0;
	size = ip->d.size > room ? 0 : (unsigned)ip->d.size;
	right = size >= 2 * HEADER_WORDS && readi(ip, 0, (char *)base, size) == (int)size &&
		h[0] == magic && h[1] == version && h[2] == size;
	iput(ip);
	return right ? 1 : -1;
}
