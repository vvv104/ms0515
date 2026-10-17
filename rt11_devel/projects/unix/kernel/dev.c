/* dev.c - the block devices: the diskettes through /boot/fd, or the ROM's
 * 160010 without it, and the RAM disk, 1024 blocks of two pages each.
 *
 * A diskette is a device by a side, 800 blocks: fd0 and fd1 the boot
 * drive's sides 0 and 1, fd2 and fd3 the second drive's - a single-sided
 * image as the other emulators take one.  fdx0 and fdx1 are a drive's two
 * sides as one volume of 1600 blocks, side 0's first.  The ROM numbers a
 * side of a drive side * 2 + drive (RT-11's DZ0..DZ3), so fd1 is its unit
 * 2 and fd2 its unit 1.  A block of fdx is the same block of a side's
 * device: dev_canon gives the cache one name for it. */
#include "sys.h"
#include "ramdisk.h"
#include "rom.h"

#define SIDE 800			/* a side's blocks */

/* The diskettes' blocks go by the ROM until /boot/fd takes them over; a
 * track is written only by /boot/fd. */
static int (*fd_rw)(unsigned unit, unsigned block, void *buf, int write) = rom_rw;
int (*fd_format)(unsigned unit, unsigned track, const void *image, unsigned len);

/* Each diskette device's drive (bit 2) and sides (bit 0 side 0, bit 1
 * side 1), by its number. */
const unsigned char dev_place[DEV_FDX1 + 1] = {1, 2, 5, 6, 3, 7};
#define place dev_place

unsigned dev_blocks(unsigned dev)
{
	if (dev <= DEV_FDX1)
		return (place[dev] & 3) == 3 ? 2 * SIDE : SIDE;
	if (dev == DEV_RD0 && rd_present)
		return RD_PAGES / 2;
	return 0;
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
 * through the ROM, its unit side * 2 + drive; the RAM disk's two pages. */
int dev_rw(unsigned dev, unsigned block, void *buf, int write)
{
	unsigned unit;

	if (block >= dev_blocks(dev))
		return -1;
	if (dev == DEV_RD0) {
		if (write)
			rd_write(block * 2, buf, 2);
		else
			rd_read(block * 2, buf, 2);
		return 0;
	}
	dev_canon(&dev, &block);
	unit = (dev & 1) << 1 | dev >> 1;
	return fd_rw(unit, block, buf, write);
}

/* /boot/fd, when the root has one, read in at DRV_BASE (part.c) and made
 * the diskettes' way once init is ready; else said so, the ROM's way
 * kept. */
void fd_driver_load(void)
{
	const struct fd_driver *d = (const struct fd_driver *)DRV_BASE;
	int got = part_load("/boot/fd", DRV_BASE, DRV_TOP - DRV_BASE, DRV_MAGIC, DRV_VERSION);

	if (got > 0 && d->init() == 0) {
		fd_rw = d->rw;
		fd_format = d->format;
		tty_puts("UNIX: the diskettes by /boot/fd\n");
	} else if (got) {
		tty_puts("UNIX: /boot/fd is no driver - the ROM's way\n");
	}
}
