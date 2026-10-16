/* hi.h - the kernel's second part, /boot/hi, and what of the first it
 * takes.
 *
 * The low banks hold the kernel's code up to 040000 and are full; bank 6
 * has room above the tables (high.h) and below the diskettes' driver.  The
 * calls the boot does not need until the root is mounted go there: a
 * program that owns the machine (own.c), ioctl - a diskette's track
 * written - mount and umount, lseek.  /boot/hi is linked at HI_BASE against
 * the kernel's own symbols (kernel/hi/, the build's hi target) and put
 * down flat, its header first: the magic, the version, its size, the key
 * of the kernel it was linked against, the entries.  The kernel loads it
 * once the root is mounted (part.c) and calls it through the header; with
 * no /boot/hi, or one of another kernel, those calls are ENOSYS. */
#ifndef UNIX_HI_H
#define UNIX_HI_H

#include "high.h"
#include "proc.h"

#define HI_BASE HIGH_TOP		/* 151600, over the tables */
#define HI_TOP DRV_BASE			/* the driver's place above */
#define HI_MAGIC 044111			/* "IH" */
#define HI_VERSION 1
#define PATHMAX 64			/* a path a call is given, its NUL too */
#define PATHMAX 64			/* a path a call is given, its NUL too */
#define PATHMAX 64			/* a path a call is given, its NUL too */

struct hi_part {
	unsigned magic, version;
	unsigned size;			/* the image's bytes, the header's too */
	unsigned key;			/* &kernel_key of the kernel it was linked against */
	int (*own_exec)(struct inode *ip, unsigned size, unsigned bss, unsigned entry,
			unsigned argc, unsigned used);
	int (*ioctl)(struct trapframe *tf, const unsigned *arg);
	int (*mount)(struct trapframe *tf, const unsigned *arg);
	int (*umount)(struct trapframe *tf, const unsigned *arg);
	int (*lseek)(struct trapframe *tf, const unsigned *arg);
};

extern const struct hi_part *hi;	/* 0 until /boot/hi is in */
extern unsigned kernel_key;
void hi_load(void);

/* A part of the system as a file, put down flat at base, room bytes at
 * most: 1 when its header holds - the magic, the version, its size the
 * file's - 0 when there is no such file, -1 when it is not right. */
int part_load(const char *path, unsigned base, unsigned room, unsigned magic, unsigned version);

/* The first part's own, for the second (sys.c, fs.c, dev.c). */
int inside(unsigned p, unsigned n);
int user_string(unsigned p, unsigned max);
struct file *file_of(unsigned fd);
struct mount *mount_of(unsigned dev);
struct mount *covering(const struct inode *ip);
int read_super(unsigned dev, struct superblock *sb);
extern const unsigned char dev_place[];
extern int (*fd_format)(unsigned unit, unsigned track, const void *image, unsigned len);

#endif
