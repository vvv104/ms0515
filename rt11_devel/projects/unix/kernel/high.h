/* high.h - the kernel's tables in bank 6.
 *
 * Banks 0 and 1 hold the kernel's code; bank 6, 140000-157777, its data
 * (SPEC.md "Memory map"): the kernel is linked with its data at 140000,
 * start.s copies the data there from the image and clears the bss.  The
 * big tables are a part of the bss: the process table with each process's
 * kernel stack, the block buffers and the shadow bank's index of more,
 * the pipes, the inodes, the open files, the console's line and queue,
 * exec's arguments.  The data end below 155000, where the diskettes'
 * driver, /boot/fd, goes (kernel.h DRV_BASE).  The top of the bank, from
 * 157000, is left to the ROM: the stack the kernel calls it on,
 * 157000-157400 (rom.s), its cells at 1577xx and its own stack from 157614
 * down. */
#ifndef UNIX_HIGH_H
#define UNIX_HIGH_H

#include "kernel.h"
#include "proc.h"

#define NBUF 3				/* the buffers in hand */
#define NSHADOW 15			/* more in the shadow copy of bank 1 */
#define SCRATCH SHADOW_BLOCK(NSHADOW)	/* its last block: fork's bounce */
#define NPIPE 2
#define PIPESIZ 256

struct pipe {
	char buf[PIPESIZ];
	unsigned head, tail, count;	/* taken at tail, put at head */
	unsigned char readers, writers;
};

#define NMOUNT 3
#define NINODE 8
#define LINE 80				/* the console's line in hand */
#define QUEUE 256			/* the lines typed, not yet read */
#define ARGBYTES 256			/* exec's arguments' strings, all of them */

/* A block given up by the buffers, kept in the shadow bank's block of the
 * same index; used 0 is none. */
struct shadow {
	unsigned dev, block, used;
};

/* A volume in the tree: the root (the table's first), or one mounted on a
 * directory, which it covers while it is there. */
struct mount {
	unsigned dev;
	struct superblock sb;
	struct inode *on;		/* the directory covered, held */
	char used;
};

struct high {
	struct mount mounts[NMOUNT];
	struct proc procs[NPROC];
	struct buf bufs[NBUF];
	struct shadow shadow[NSHADOW];
	struct pipe pipes[NPIPE];
	struct inode itab[NINODE];
	struct file files[NFILE];
	char line[LINE];
	char queue[QUEUE];
	char argbuf[ARGBYTES];
};

extern struct high high;		/* proc.c's, in the bss */
#define HIGH (&high)

#endif
