/* vol.h - volumes (vol.c). */
#ifndef UNIX_VOL_H
#define UNIX_VOL_H

/* An empty file system of `blocks` blocks on the device; 0, or -1 and
 * errno. */
int mkfs_volume(const char *device, unsigned blocks);

/* A decimal number at s. */
unsigned number(const char *s);

#endif
