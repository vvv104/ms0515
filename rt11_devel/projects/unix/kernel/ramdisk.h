/* ramdisk.h - the EX board's RAM disk (ramdisk.c). */
#ifndef UNIX_RAMDISK_H
#define UNIX_RAMDISK_H

#define RD_PAGE 256			/* bytes a page */
#define RD_PAGES 2048			/* 512 KB */

extern int rd_present;			/* found by rd_probe() */

int rd_probe(void);
void rd_write(unsigned page, const void *from, unsigned pages);
void rd_read(unsigned page, void *to, unsigned pages);

#endif
