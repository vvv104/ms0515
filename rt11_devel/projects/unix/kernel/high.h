/* high.h - the kernel's tables in bank 6.
 *
 * Banks 0 and 1 hold the kernel's code and its small data; bank 6,
 * 140000-157777, is the kernel's too (SPEC.md "Memory map"), and the big
 * tables live there at fixed addresses: the process table with each
 * process's kernel stack, the block buffers, fork's bounce buffer, the
 * pipes.  They are no part of the kernel's image on the diskette - the
 * compiler puts even zeroed arrays in .data - and main() clears them.  The
 * top of the bank, from 157000, is left to the ROM: its cells at 1577xx and
 * its stack from 157614 down. */
#ifndef UNIX_HIGH_H
#define UNIX_HIGH_H

#include "kernel.h"
#include "proc.h"

#define NBUF 4
#define NPIPE 2
#define PIPESIZ 256

struct pipe {
	char buf[PIPESIZ];
	unsigned head, tail, count;	/* taken at tail, put at head */
	unsigned char readers, writers;
};

#define NMOUNT 3

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
	char bounce[512];
	struct pipe pipes[NPIPE];
};

#define HIGH_BASE 0140000u
#define HIGH_TOP  0157000u
#define HIGH ((struct high *)HIGH_BASE)

typedef char high_fits[sizeof(struct high) <= HIGH_TOP - HIGH_BASE ? 1 : -1];

#endif
