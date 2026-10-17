/* ioctl.c - ioctl(fd, cmd, arg) (ioctl.h): FD_WTRACK on a diskette's
 * device open to write, a track written anew by /boot/fd; TTY_TAB on the
 * console. */
#include "sys.h"
#include "ioctl.h"

#define SIDE 800

/* The diskette device's track written anew from the image, by /boot/fd;
 * 0 or the error.  The side's blocks on it are forgotten by the cache:
 * track t holds blocks (t - 1) * 10.., track 0 the last ten. */
static int dev_format(unsigned dev, unsigned track, const void *image, unsigned len)
{
	unsigned side = dev;

	if (dev > DEV_FDX1)
		return ENOTTY;
	if (!fd_format)
		return ENODEV;
	if (fs_mounted(dev))
		return EBUSY;
	if ((dev_place[dev] & 3) == 3) {
		side = (dev_place[dev] >> 1 & 2) + (track & 1);
		track >>= 1;
	}
	if (track >= SIDE / 10)
		return EINVAL;
	if (fd_format((side & 1) << 1 | side >> 1, track, image, len) != 0)
		return EIO;
	binval(side, track ? (track - 1) * 10 : SIDE - 10, 10);
	return 0;
}

int sys_ioctl(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);
	const struct fd_track *t = (const struct fd_track *)arg[2];
	int error;

	if (f && f->kind == F_TTY) {
		if (arg[1] == TTY_TAB) {
			if ((error = user_string(arg[2], LINE)) != 0)
				return error;
			tty_tab((const char *)arg[2]);
		} else if (arg[1] == TTY_BG) {
			curproc->bg = 1;
		} else if (arg[1] == TTY_RAW) {
			tty_raw(arg[2] != 0);
		} else {
			return ENOTTY;
		}
		tf->r0 = 0;
		return 0;
	}
	if (!f || f->kind != F_INODE || !f->writable)
		return EBADF;
	if ((f->ip->d.mode & S_IFMT) != S_IFBLK || arg[1] != FD_WTRACK)
		return ENOTTY;
	if (!inside(arg[2], sizeof *t) || !inside((unsigned)t->image, t->len))
		return EFAULT;
	if ((error = dev_format(f->ip->d.ext[0].start, t->track, t->image, t->len)) != 0)
		return error;
	tf->r0 = 0;
	return 0;
}
