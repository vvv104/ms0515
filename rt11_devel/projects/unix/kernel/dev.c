/* dev.c - the block devices: the diskettes through /boot/fd, or the ROM's
 * 160010 without it, and the RAM disk through /boot/rd, none without it.
 *
 * A diskette is a device by a side, 800 blocks: fd0 and fd1 the boot
 * drive's sides 0 and 1, fd2 and fd3 the second drive's - a single-sided
 * image as the other emulators take one.  fdx0 and fdx1 are a drive's two
 * sides as one volume of 1600 blocks, side 0's first.  The ROM numbers a
 * side of a drive side * 2 + drive (RT-11's DZ0..DZ3), so fd1 is its unit
 * 2 and fd2 its unit 1.  A block of fdx is the same block of a side's
 * device: dev_canon gives the cache one name for it. */
#include "sys.h"
#include "rom.h"

#define SIDE 800			/* a side's blocks */

/* The diskettes' blocks go by the ROM until /boot/fd takes them over; a
 * track is written only by /boot/fd. */
static int (*fd_rw)(unsigned unit, unsigned block, void *buf, int write) = rom_rw;
int (*fd_format)(unsigned unit, unsigned track, const void *image, unsigned len);
static int (*rd_rw)(unsigned block, void *buf, int write);
static unsigned rd_blocks;		/* the RAM disk's, 0 with none */

/* Each diskette device's drive (bit 2) and sides (bit 0 side 0, bit 1
 * side 1), by its number. */
const unsigned char dev_place[DEV_FDX1 + 1] = {1, 2, 5, 6, 3, 7};
#define place dev_place

unsigned dev_blocks(unsigned dev)
{
	if (dev <= DEV_FDX1)
		return (place[dev] & 3) == 3 ? 2 * SIDE : SIDE;
	return dev == DEV_RD0 ? rd_blocks : 0;
}

/* A block of fdx0/fdx1 as the side's device's block: fd0..fd3 are the
 * drive's place over 2 plus the side. */
void dev_canon(unsigned *dev, unsigned *block)
{
	if (*dev <= DEV_FDX1 && (place[*dev] & 3) == 3) {
		*dev = (place[*dev] >> 1 & 2) + (*block >= SIDE);
		if (*block >= SIDE)
			*block -= SIDE;
	}
}

/* Whether two devices share blocks: the same drive, a side in common. */
int dev_overlap(unsigned a, unsigned b)
{
	if (a > DEV_FDX1 || b > DEV_FDX1)
		return a == b;
	return (place[a] >> 2) == (place[b] >> 2) && (place[a] & place[b] & 3);
}

/* The block read into buf or written from it, 0 or -1: a diskette's
 * through the ROM or /boot/fd, its unit side * 2 + drive; the RAM disk's
 * through /boot/rd. */
int dev_rw(unsigned dev, unsigned block, void *buf, int write)
{
	unsigned unit;

	if (block >= dev_blocks(dev))
		return -1;
	if (dev == DEV_RD0)
		return rd_rw(block, buf, write);
	dev_canon(&dev, &block);
	unit = (dev & 1) << 1 | dev >> 1;
	return fd_rw(unit, block, buf, write);
}

/* A driver read in from its file at base (part.c): there, or 0 - a file
 * that is no driver said so. */
static const void *driver(const char *path, unsigned base, unsigned top, unsigned magic, unsigned version)
{
	int got = part_load(path, base, top - base, magic, version);

	if (got < 0) {
		tty_puts("kernel: ");
		tty_puts(path);
		tty_puts(" is no driver\n");
	}
	return got > 0 ? (const void *)base : 0;
}

/* The drivers, once the root is mounted: /boot/rd's board found, or no RAM
 * disk; /boot/fd made the diskettes' way once its init is ready, else the
 * ROM's way kept. */
void drivers_load(void)
{
	const struct rd_driver *rd = driver("/boot/rd", RD_BASE, DRV_TOP, RD_MAGIC, RD_VERSION);
	const struct fd_driver *fd = driver("/boot/fd", DRV_BASE, RD_BASE, DRV_MAGIC, DRV_VERSION);

	if (rd && (rd_blocks = rd->init()) != 0)
		rd_rw = rd->rw;
	tty_puts(rd_blocks ? "kernel: a RAM disk of 512 KB, /dev/rd0\n" : "kernel: no RAM disk\n");
	if (fd && fd->init() == 0) {
		fd_rw = fd->rw;
		fd_format = fd->format;
		tty_puts("kernel: the diskettes by /boot/fd\n");
	}
}
