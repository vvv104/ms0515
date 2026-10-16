/* sys.c - the system calls and the open files.
 *
 * A process calls with TRAP n, n the call's number (V6's numbers), the
 * arguments on its stack as C passed them to the library's stub, which
 * called the TRAP: above the frame lie the stub's return address, then the
 * arguments.  The result comes back in r0 (a long in r0:r1, the high word
 * in r0), an error as C set in the PS and the error's number in r0.
 *
 * The open files are the system's, shared by the descriptors that point at
 * them - a fork's child gets its parent's - each with its offset; the
 * console is one of them, the first process's 0, 1 and 2.  When pid 1
 * exits, init runs again in its place. */
#include "kernel.h"
#include "proc.h"

enum {
	SYS_EXIT = 1, SYS_FORK = 2, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6,
	SYS_WAIT = 7, SYS_EXEC = 11, SYS_TIME = 13, SYS_GETPID = 20, SYS_GETPPID = 64
};

#define PATHMAX 64

static struct file files[NFILE];

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

struct file *falloc(void)
{
	int i;

	for (i = 0; i < NFILE; i++)
		if (files[i].count == 0) {
			files[i].count = 1;
			files[i].off = 0;
			return &files[i];
		}
	return 0;
}

void fclose(struct file *f)
{
	if (--f->count == 0) {
		if (f->kind == F_INODE)
			iput(f->ip);
		f->kind = F_NONE;
	}
}

/* The console for the first process: 0, 1 and 2. */
void files_init(void)
{
	struct file *tty = falloc();
	int i;

	if (!tty)
		panic("no file for the console");
	tty->kind = F_TTY;
	tty->count = 3;
	for (i = 0; i < 3; i++)
		curproc->ofile[i] = tty;
}

static struct file *file_of(unsigned fd)
{
	return fd < NOFILE ? curproc->ofile[fd] : 0;
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
	struct file *f;
	int error = 0;
	unsigned fd;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if (arg[1] != 0)			/* only reading, for now */
		return EROFS;
	for (fd = 0; fd < NOFILE && curproc->ofile[fd]; fd++)
		;
	if (fd == NOFILE)
		return EMFILE;
	if (!(ip = namei((const char *)arg[0], &error)))
		return error;
	if (!(f = falloc())) {
		iput(ip);
		return ENFILE;
	}
	f->kind = F_INODE;
	f->ip = ip;
	curproc->ofile[fd] = f;
	tf->r0 = fd;
	return 0;
}

static int sys_close(const unsigned *arg)
{
	struct file *f = file_of(arg[0]);

	if (!f)
		return EBADF;
	fclose(f);
	curproc->ofile[arg[0]] = 0;
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

static int sys_fork(struct trapframe *tf)
{
	int pid = fork(tf);

	if (pid < 0)
		return -pid;
	tf->r0 = (unsigned)pid;
	return 0;
}

static int sys_wait(struct trapframe *tf, const unsigned *arg)
{
	int pid;

	if (arg[0] && !inside(arg[0], 2))
		return EFAULT;
	pid = pwait(arg[0]);
	if (pid < 0)
		return -pid;
	tf->r0 = (unsigned)pid;
	return 0;
}

/* The process gone; pid 1's place goes to init again, its files closed. */
static void sys_exit(const unsigned *arg)
{
	static const char *const argv[] = {"init", 0};
	int i;

	if (curproc->pid != 1)
		pexit((int)arg[0]);
	for (i = 3; i < NOFILE; i++)
		if (curproc->ofile[i]) {
			fclose(curproc->ofile[i]);
			curproc->ofile[i] = 0;
		}
	exec("/etc/init", argv);
	panic("no /etc/init");
}

void syscall(struct trapframe *tf)
{
	const unsigned *arg = (const unsigned *)(tf + 1) + 1;
	int error = 0;

	curproc->tf = tf;
	switch (*(unsigned *)(tf->pc - 2) & 0377) {
	case SYS_EXIT:
		sys_exit(arg);
		return;
	case SYS_FORK:
		error = sys_fork(tf);
		break;
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
	case SYS_WAIT:
		error = sys_wait(tf, arg);
		break;
	case SYS_EXEC:
		error = sys_exec(arg);
		break;
	case SYS_TIME:
		error = sys_time(tf);
		break;
	case SYS_GETPID:
		tf->r0 = (unsigned)curproc->pid;
		break;
	case SYS_GETPPID:
		tf->r0 = (unsigned)curproc->ppid;
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
