/* vol.h - volumes (vol.c). */
#ifndef UNIX_VOL_H
#define UNIX_VOL_H

/* An empty file system of `blocks` blocks on the device, with a swap file
 * /swap of swap_kb kilobytes in it when that is not 0 - its blocks taken,
 * nothing written into them; 0, or -1 and errno. */
int mkfs_volume(const char *device, unsigned blocks, unsigned swap_kb);

/* A decimal number at s. */
unsigned number(const char *s);

#endif
