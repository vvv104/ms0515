/* kernel.h - what the parts of the kernel see of one another. */
#ifndef UNIX_KERNEL_H
#define UNIX_KERNEL_H

#include "fs.h"

/* The machine's write-only registers and the ROM's copies of them. */
#define DISPATCHER (*(volatile unsigned *)0177400)
#define DISP_COPY  (*(volatile unsigned *)0157700)	/* the ROM's copy */
#define BANK1_BIT 2			/* dispatcher bit 1: bank 1 primary when set */
#define BANK6_BIT 0100			/* bit 6: bank 6 primary; its shadow copy is the parking */
/* The shadow copy of bank 1, seen at 020000-037777 while xcopy has it on:
 * 16 blocks, the buffer cache's second level and the last fork's bounce. */
#define SHADOW_BLOCK(i) ((char *)020000 + (i) * 512)
void xcopy(void *to, const void *from, unsigned words, unsigned disp);
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
	EIO = 5, ENOSYS = 38, EEXIST = 17, ENOSPC = 28, ENOTBLK = 15, EBUSY = 16,
	ENODEV = 19, ENOTTY = 25, EXDEV = 18, EACCES = 13, EINTR = 4
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
void tty_tab(const char *s);

/* dev.c: the block devices */
#define DEV_FD0 0			/* the boot drive's side 0; fd1 its side 1 */
#define DEV_FD3 3			/* fd2, fd3: the second drive's sides */
#define DEV_FDX0 4			/* the boot drive, both sides as one */
#define DEV_FDX1 5			/* the second drive, both sides */
#define DEV_RD0 6			/* the RAM disk */
unsigned dev_blocks(unsigned dev);
void dev_canon(unsigned *dev, unsigned *block);
int dev_overlap(unsigned a, unsigned b);

/* The drivers as files, put down flat in bank 6 over the kernel's data
 * (high.h), each its header first: /boot/fd, the diskettes'
 * (drivers/fd.s), at DRV_BASE, and /boot/rd, the RAM disk's (drivers/rd.s),
 * at RD_BASE, up to DRV_TOP.  fd's rw's and format's unit is the ROM's, a
 * drive's side; rw's block 0..799, format's track the cylinder, 0..79, the
 * image WRITE TRACK's (ioctl.h).  rd's init tells its blocks, 0 with no
 * board. */
#define DRV_BASE 0155000u
#define RD_BASE 0156300u
#define DRV_TOP 0157000u
#define DRV_MAGIC 042106		/* "FD" */
#define DRV_VERSION 2			/* 2: with format */
#define RD_MAGIC 042122			/* "RD" */
#define RD_VERSION 1
struct fd_driver {
	unsigned magic, version;
	unsigned size;			/* the image's bytes, the header's too */
	int (*init)(void);
	int (*rw)(unsigned unit, unsigned block, void *buf, int write);
	int (*format)(unsigned unit, unsigned track, const void *image, unsigned len);
};
struct rd_driver {
	unsigned magic, version, size;
	unsigned (*init)(void);
	int (*rw)(unsigned block, void *buf, int write);
};
void drivers_load(void);
int dev_rw(unsigned dev, unsigned block, void *buf, int write);
#define dev_read(dev, block, buf) dev_rw(dev, block, buf, 0)
#define dev_write(dev, block, buf) dev_rw(dev, block, (void *)(buf), 1)

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
void bforget(unsigned dev);
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
int fs_mounted(unsigned dev);
int fs_link(const char *old, const char *new_name);
int devrw(struct inode *ip, unsigned long off, char *buf, unsigned n, int write);

/* exec.c */
int exec(const char *path, const char *const *argv);

/* sys.c */
void syscall(struct trapframe *tf);
void files_init(void);

/* main.c */
void panic(const char *why) __attribute__((noreturn));

#endif
