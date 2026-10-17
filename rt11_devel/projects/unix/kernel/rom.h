/* rom.h - the ROM's services as the kernel calls them (rom.s): each at
 * priority 7, on a stack of its own. */
#ifndef UNIX_ROM_H
#define UNIX_ROM_H

/* A character out through the ROM's console: 80 x 25 in 640 x 200, the VT52
 * commands of the native ROM-B. */
void rom_putc(int c);

/* The next byte of the keyboard, -1 when there is none.  The ROM turns keys
 * into KOI-8: a Latin letter bare is upper case, a Russian one lower. */
int rom_getc(void);

/* A step of the cursor's blink. */
void rom_blink(void);

/* Block `block` (0..799) of the ROM's unit - a drive's side, side * 2 +
 * drive - read into buf (512 bytes), or written from it; 0 or -1.  The
 * driver's rw by the ROM's own way (kernel.h struct fd_driver). */
int rom_rw(unsigned unit, unsigned block, void *buf, int write);

#endif
