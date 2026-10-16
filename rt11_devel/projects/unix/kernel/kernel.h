/* kernel.h - what the parts of the kernel see of one another. */
#ifndef UNIX_KERNEL_H
#define UNIX_KERNEL_H

#include "fs.h"

/* The machine's write-only registers and the ROM's copies of them. */
#define DISPATCHER (*(volatile unsigned *)0177400)
#define DISP_COPY  (*(volatile unsigned *)0157700)	/* the ROM's copy */
#define DISP_TIMER 01000				/* bit 9: the frame interrupt */
#define REG_B      (*(volatile unsigned *)0177602)
#define KBD_ROWS   (*(volatile unsigned char *)0177540)	/* the MS 7007's rows */

/* The process's space: slot A, banks 2-5. */
#define USER_BASE 040000u
#define USER_TOP  0140000u

/* The errors, V6's numbers. */
enum {
	ENOENT = 2, E2BIG = 7, ENOEXEC = 8, EBADF = 9, ENOMEM = 12, EFAULT = 14,
	ENOTDIR = 20, EISDIR = 21, EINVAL = 22, ENFILE = 23, EMFILE = 24, EROFS = 30,
	EIO = 5, ENOSYS = 38, EEXIST = 17, ENOSPC = 28, ENOTBLK = 15, EBUSY = 16
};

/* The vectors' page (start.s): vector v is vectors[v / 2] (the PC) and
 * vectors[v / 2 + 1] (the PS). */
extern volatile unsigned vectors[];
#define VECTOR(v, handler, ps) \
	(vectors[(v) / 2] = (unsigned)(handler), vectors[(v) / 2 + 1] = (ps))

/* trap.s */
struct trapframe {
	unsigned r0, r1, r2, r3, r4, r5, pc, ps;
};
void trap_entry(void), clock_entry(void), tty_entry(void);
int spl7(void);
void splx(int ps);
void idle(void);
void enter(unsigned pc, unsigned sp);

/* lib.c */
void *memcpy(void *to, const void *from, unsigned n);
void *memset(void *p, int c, unsigned n);

/* clock.c */
void clock_start(void);
unsigned long clock_seconds(void);

/* tty.c */
int tty_read(char *buf, int n);
int tty_write(const char *buf, int n);
void tty_puts(const char *s);

/* dev.c: the block devices */
#define DEV_FD0 0			/* the diskette in the boot drive */
#define DEV_RD0 1			/* the RAM disk */
unsigned dev_blocks(unsigned dev);
int dev_read(unsigned dev, unsigned block, void *buf);
int dev_write(unsigned dev, unsigned block, const void *buf);

/* bio.c */
struct buf {
	char data[BSIZE];		/* first: word-aligned, as the ROM writes it */
	unsigned dev, block;
	unsigned used;			/* when last used, for the least recent */
	char valid, dirty;
};
struct buf *bread(unsigned dev, unsigned block);
struct buf *getblk(unsigned dev, unsigned block);
int bwrite(struct buf *b);
void binval(unsigned dev, unsigned start, unsigned n);
void bdirty(struct buf *b);
void bflush(void);

/* fs.c */
struct inode {
	unsigned dev, ino, count;
	struct dinode d;
};
int fs_mount(void);
struct inode *iget(unsigned dev, unsigned n);
void iput(struct inode *ip);
int iupdate(struct inode *ip);
void itrunc(struct inode *ip);
int readi(const struct inode *ip, unsigned long off, char *buf, unsigned n);
int writei(struct inode *ip, unsigned long off, const char *buf, unsigned n);
struct inode *namei(const char *path, int *error);
struct inode *fs_creat(const char *path, int *error);
int fs_mkdir(const char *path);
int fs_unlink(const char *path);
int fs_mount_on(const char *device, const char *dir);
int fs_umount(const char *device);
int devrw(struct inode *ip, unsigned long off, char *buf, unsigned n, int write);

/* exec.c */
int exec(const char *path, const char *const *argv);

/* sys.c */
void syscall(struct trapframe *tf);
void files_init(void);

/* main.c */
void panic(const char *why) __attribute__((noreturn));

#endif
