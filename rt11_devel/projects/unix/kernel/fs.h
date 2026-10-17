/* fs.h - the file system on the disk (SPEC.md "The file system and the
 * diskette format"); tools/mkfs.py writes the same.
 *
 * Words are the PDP-11's, the low byte first; a long is two words, the high
 * word first, as the compiler keeps it. */
#ifndef UNIX_FS_H
#define UNIX_FS_H

#define BSIZE 512			/* a block, a sector */
#define FS_MAGIC 054125			/* 'U', 'X' */
#define FS_VERSION 2
#define ROOT_INO 1

/* Block 1. */
struct superblock {
	unsigned magic, version;
	unsigned blocks;		/* the volume's */
	unsigned kstart, kblocks;	/* /boot/kernel's blocks, for the boot block */
	unsigned kload, kentry;		/* where it goes, where it starts */
	unsigned bitmap;		/* the bitmap of the blocks taken, a bit a block */
	unsigned itable, iblocks;	/* the inode table */
	unsigned swap, swapblocks;	/* the swap area, none on a diskette of data */
	unsigned data;			/* the first block of the data area */
	unsigned inodes;		/* how many */
	unsigned long volume;		/* the volume's number, at random */
	char label[16];
};

#define NEXTENT 5

/* An inode, 32 bytes, 16 a block; inode n is the table's (n - 1)th. */
struct dinode {
	unsigned mode;			/* the type and the one rwx (S_R...) */
	unsigned char nlink;
	unsigned char nextents;
	unsigned long size;		/* bytes */
	unsigned long mtime;		/* seconds since the epoch */
	struct extent {
		unsigned start, len;	/* blocks */
	} ext[NEXTENT];
};

#define S_IFMT  0170000
#define S_IFDIR 0040000
#define S_IFBLK 0060000			/* a block device: its number in ext[0].start */
#define S_IFREG 0100000
/* A file's rights: one rwx, there being no users.  r to read it, w to
 * write it, empty it or remove it, x to run it; a directory's are not
 * looked at.  A new file is rw-, a directory rwx. */
#define S_R 4
#define S_W 2
#define S_X 1
#define S_RIGHTS 7

/* A directory entry, 16 bytes, 32 a block; inode 0 is a free entry. */
#define DIRSIZ 14
struct direct {
	unsigned ino;
	char name[DIRSIZ];		/* NUL-padded, not NUL-ended when 14 long */
};

#endif
