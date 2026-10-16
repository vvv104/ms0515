/* rom.h - the ROM's services as the kernel calls them (rom.s). */
#ifndef UNIX_ROM_H
#define UNIX_ROM_H

/* A character out through the ROM's console: 80 x 25 in 640 x 200, the VT52
 * commands of the native ROM-B. */
void rom_putc(int c);

#endif
