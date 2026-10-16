/* sys.c - the system calls and the open files.
 *
 * A process calls with TRAP n, n the call's number (V6's numbers), the
 * arguments on its stack as C passed them to the library's stub, which
 * called the TRAP: above the frame lie the stub's return address, then the
 * arguments.  The result comes back in r0 (a long in r0:r1, the high word
 * in r0), an error as C set in the PS and the error's number in r0.
 *
 * There is one process yet: its files are 0, 1 and 2 the console, the rest
 * opened files of the file system, read only.  When it exits, its files
 * are closed and /etc/init runs again. */
#include "kernel.h"

enum {
	SYS_EXIT = 1, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6,
	SYS_EXEC = 11, SYS_TIME = 13
};

#define NFILE 8
#define PATHMAX 64

static struct file {
	enum { F_NONE, F_TTY, F_INODE } kind;
	struct inode *ip;
	unsigned long off;
} files[NFILE] = {{F_TTY, 0, 0}, {F_TTY, 0, 0}, {F_TTY, 0, 0}};

/* Whether [p, p + n) lies in the process's space. */
static int inside(unsigned p, unsigned n)
{
	return p >= USER_BASE && p <= USER_TOP && n <= USER_TOP - p;
}

/* The process's string at p, whole inside its space and at most max bytes
 * with its NUL: 0, or the error. */
static int user_string(unsigned p, unsigned max)
{
	unsigned n;

	for (n = 0; n < max; n++, p++) {
		if (!inside(p, 1))
			return EFAULT;
		if (*(const char *)p == 0)
			return 0;
	}
	return E2BIG;
}

static struct file *file_of(unsigned fd)
{
	return fd < NFILE && files[fd].kind != F_NONE ? &files[fd] : 0;
}

void files_close_all(void)
{
	int i;

	for (i = 3; i < NFILE; i++) {
		if (files[i].kind == F_INODE)
			iput(files[i].ip);
		files[i].kind = F_NONE;
	}
}

static int sys_read(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);
	int n;

	if (!f)
		return EBADF;
	if (!inside(arg[1], arg[2]))
		return EFAULT;
	if (f->kind == F_TTY) {
		tf->r0 = (unsigned)tty_read((char *)arg[1], (int)arg[2]);
		return 0;
	}
	n = readi(f->ip, f->off, (char *)arg[1], arg[2]);
	if (n < 0)
		return -n;
	f->off += (unsigned)n;
	tf->r0 = (unsigned)n;
	return 0;
}

static int sys_write(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);

	if (!f)
		return EBADF;
	if (f->kind != F_TTY)
		return EROFS;
	if (!inside(arg[1], arg[2]))
		return EFAULT;
	tf->r0 = (unsigned)tty_write((const char *)arg[1], (int)arg[2]);
	return 0;
}

static int sys_open(struct trapframe *tf, const unsigned *arg)
{
	struct inode *ip;
	int error = 0;
	unsigned fd;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if (arg[1] != 0)			/* only reading, for now */
		return EROFS;
	for (fd = 0; fd < NFILE && files[fd].kind != F_NONE; fd++)
		;
	if (fd == NFILE)
		return EMFILE;
	if (!(ip = namei((const char *)arg[0], &error)))
		return error;
	files[fd].kind = F_INODE;
	files[fd].ip = ip;
	files[fd].off = 0;
	tf->r0 = fd;
	return 0;
}

static int sys_close(const unsigned *arg)
{
	struct file *f = file_of(arg[0]);

	if (!f)
		return EBADF;
	if (f->kind == F_INODE)
		iput(f->ip);
	f->kind = F_NONE;
	return 0;
}

static int sys_exec(const unsigned *arg)
{
	const unsigned *argv = (const unsigned *)arg[1];
	int error, i;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	for (i = 0; argv; i++) {		/* the vector and its strings in the slot */
		if (!inside((unsigned)&argv[i], 2))
			return EFAULT;
		if (!argv[i])
			break;
		if ((error = user_string(argv[i], 256)) != 0)
			return error;
	}
	return exec((const char *)arg[0], (const char *const *)argv);
}

static int sys_time(struct trapframe *tf)
{
	unsigned long t = clock_seconds();

	tf->r0 = (unsigned)(t >> 16);
	tf->r1 = (unsigned)t;
	return 0;
}

/* The process gone: its files closed, init again. */
static void sys_exit(void)
{
	static const char *const argv[] = {"init", 0};

	files_close_all();
	exec("/etc/init", argv);
	panic("no /etc/init");
}

void syscall(struct trapframe *tf)
{
	const unsigned *arg = (const unsigned *)(tf + 1) + 1;
	int error;

	switch (*(unsigned *)(tf->pc - 2) & 0377) {
	case SYS_EXIT:
		sys_exit();
		return;
	case SYS_READ:
		error = sys_read(tf, arg);
		break;
	case SYS_WRITE:
		error = sys_write(tf, arg);
		break;
	case SYS_OPEN:
		error = sys_open(tf, arg);
		break;
	case SYS_CLOSE:
		error = sys_close(arg);
		break;
	case SYS_EXEC:
		error = sys_exec(arg);
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
