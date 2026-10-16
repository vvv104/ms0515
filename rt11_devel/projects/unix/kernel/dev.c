/* dev.c - the block devices: 0 the diskette in the boot drive, through the
 * ROM's 160010 for now; 1 the RAM disk, 1024 blocks of two pages each. */
#include "kernel.h"
#include "ramdisk.h"
#include "rom.h"

unsigned dev_blocks(unsigned dev)
{
	if (dev == DEV_FD0)
		return 1600;
	if (dev == DEV_RD0 && rd_present)
		return RD_PAGES / 2;
	return 0;
}

int dev_read(unsigned dev, unsigned block, void *buf)
{
	if (block >= dev_blocks(dev))
		return -1;
	if (dev == DEV_FD0)
		return rom_bread(block, buf);
	rd_read(block * 2, buf, 2);
	return 0;
}

int dev_write(unsigned dev, unsigned block, const void *buf)
{
	if (block >= dev_blocks(dev))
		return -1;
	if (dev == DEV_FD0)
		return rom_bwrite(block, buf);
	rd_write(block * 2, buf, 2);
	return 0;
}
