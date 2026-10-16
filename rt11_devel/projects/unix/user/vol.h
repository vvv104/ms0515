/* vol.h - volumes and swap files (vol.c). */
#ifndef UNIX_VOL_H
#define UNIX_VOL_H

/* An empty file system of `blocks` blocks on the device; 0, or -1 and errno. */
int mkfs_volume(const char *device, unsigned blocks);

/* A file of kb kilobytes of zeros, made or emptied; 0, or -1. */
int zero_file(const char *path, unsigned kb);

/* The file swap from now on, made first of kb kilobytes when kb is given and
 * it is not there; 0, or -1. */
int swap_file_on(const char *path, unsigned kb);

/* A decimal number at s. */
unsigned number(const char *s);

#endif
