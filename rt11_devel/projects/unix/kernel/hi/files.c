/* files.c - /boot/hi: the calls on open files and names past read and
 * write - lseek. */
#include "../hi.h"

#define ESPIPE 29

/* lseek(fd, off, whence): the file's offset from its start (0), from where
 * it is (1) or from its end (2); the new one in r0:r1, the high word in r0
 * - a long as the compiler keeps it, high word first in arg[1]. */
int sys_lseek(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);
	long off = (long)((unsigned long)arg[1] << 16 | arg[2]);

	if (!f)
		return EBADF;
	if (f->kind != F_INODE)
		return ESPIPE;
	if (arg[3] == 1)
		off += (long)f->off;
	else if (arg[3] == 2)
		off += (long)f->ip->d.size;
	else if (arg[3] != 0)
		return EINVAL;
	if (off < 0)
		return EINVAL;
	f->off = (unsigned long)off;
	tf->r0 = (unsigned)(f->off >> 16);
	tf->r1 = (unsigned)f->off;
	return 0;
}
