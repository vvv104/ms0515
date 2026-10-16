/* part.c - the parts of the system that are files: /boot/fd, the
 * diskettes' driver (dev.c), and /boot/hi, the kernel's second part
 * (hi.h), each read in flat once the root is mounted. */
#include "hi.h"

#define HEADER_WORDS 3			/* the magic, the version, the size */

const struct hi_part *hi;
unsigned kernel_key = 1;		/* its address is the key, in .data after all the code */

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

/* /boot/hi read in, its bss cleared, when it is this kernel's; else said
 * so, and its calls stay ENOSYS. */
void hi_load(void)
{
	const struct hi_part *h = (const struct hi_part *)HI_BASE;
	int got = part_load("/boot/hi", HI_BASE, HI_TOP - HI_BASE, HI_MAGIC, HI_VERSION);

	if (got > 0 && h->key == (unsigned)&kernel_key) {
		memset((char *)HI_BASE + h->size, 0, HI_TOP - HI_BASE - h->size);
		hi = h;
	} else if (got) {
		tty_puts("UNIX: /boot/hi is not this kernel's\n");
	}
}
