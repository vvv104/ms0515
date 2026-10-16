/* sys.c - the system calls.
 *
 * A process calls with TRAP n, n the call's number (V6's numbers), the
 * arguments on its stack as C passed them to the library's stub, which
 * called the TRAP: above the frame lie the stub's return address, then the
 * arguments.  The result comes back in r0 (a long in r0:r1, the high word
 * in r0), an error as C set in the PS and the error's number in r0. */
#include "kernel.h"

enum { SYS_EXIT = 1, SYS_READ = 3, SYS_WRITE = 4, SYS_TIME = 13 };
enum { EBADF = 9, EFAULT = 14, ENOSYS = 38 };

/* Whether [p, p + n) lies in the process's space. */
static int inside(unsigned p, unsigned n)
{
	return p >= USER_BASE && p <= USER_TOP && n <= USER_TOP - p;
}

static int sys_read(struct trapframe *tf, const unsigned *arg)
{
	if (arg[0] != 0)
		return EBADF;
	if (!inside(arg[1], arg[2]))
		return EFAULT;
	tf->r0 = (unsigned)tty_read((char *)arg[1], (int)arg[2]);
	return 0;
}

static int sys_write(struct trapframe *tf, const unsigned *arg)
{
	if (arg[0] != 1 && arg[0] != 2)
		return EBADF;
	if (!inside(arg[1], arg[2]))
		return EFAULT;
	tf->r0 = (unsigned)tty_write((const char *)arg[1], (int)arg[2]);
	return 0;
}

static int sys_time(struct trapframe *tf)
{
	unsigned long t = clock_seconds();

	tf->r0 = (unsigned)(t >> 16);
	tf->r1 = (unsigned)t;
	return 0;
}

void syscall(struct trapframe *tf)
{
	const unsigned *arg = (const unsigned *)(tf + 1) + 1;
	int error;

	switch (*(unsigned *)(tf->pc - 2) & 0377) {
	case SYS_EXIT:
		tty_puts("\ninit exited\n");
		for (;;)
			idle();
	case SYS_READ:
		error = sys_read(tf, arg);
		break;
	case SYS_WRITE:
		error = sys_write(tf, arg);
		break;
	case SYS_TIME:
		error = sys_time(tf);
		break;
	default:
		error = ENOSYS;
	}
	if (error) {
		tf->r0 = (unsigned)error;
		tf->ps |= 1;
	} else {
		tf->ps &= ~1u;
	}
}
