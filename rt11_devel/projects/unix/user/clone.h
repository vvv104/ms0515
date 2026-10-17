/* clone.h - a volume copied by its blocks, for ramroot and mkboot
 * (clone.c). */
#ifndef UNIX_CLONE_H
#define UNIX_CLONE_H

/* The block device of number dev (kernel.h DEV_FD0..DEV_RD0) as a path,
 * 0 for none: what stat(...).dev of a volume's file names. */
const char *device_path(unsigned dev);

/* The blocks the device open at fd has - 1600, 1024 or 800, its last
 * block read to tell; 0 when none of them reads. */
unsigned device_blocks(int fd);

/* The volume on from copied to to, a device of `blocks`: the blocks below
 * its data area and those its bitmap has taken, each where it was, so the
 * boot block finds the kernel where the superblock says; with `system`
 * the system alone - /home and /tmp left empty, their files neither read
 * nor kept.  The copy's superblock is told its size, the label and a
 * volume number of its own.  The blocks the copy holds, or -1 with errno -
 * ENOSPC when a block taken lies past the end. */
int clone_volume(int from, int to, unsigned blocks, const char *label, int system);

#endif
