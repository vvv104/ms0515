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
 * console is one of them, the first process's 0, 1 and 2.  A pipe is two of
 * them, its reading end and its writing end, over a buffer in bank 6: the
 * reader sleeps while it is empty and gets the end of the file once no
 * writer is left; the writer sleeps while it is full and gets EPIPE once no
 * reader is.  When pid 1 exits, init runs again in its place. */
#include "high.h"

enum {
	SYS_EXIT = 1, SYS_FORK = 2, SYS_READ = 3, SYS_WRITE = 4, SYS_OPEN = 5, SYS_CLOSE = 6,
	SYS_WAIT = 7, SYS_CREAT = 8, SYS_UNLINK = 10, SYS_EXEC = 11, SYS_TIME = 13, SYS_GETPID = 20,
	SYS_MOUNT = 21, SYS_UMOUNT = 22,
	SYS_SYNC = 36, SYS_DUP = 41, SYS_PIPE = 42, SYS_GETPPID = 64, SYS_SWAPON = 85, SYS_MKDIR = 136
};
enum { EPIPE = 32 };

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
		if (f->kind == F_PIPE_R)
			f->pp->readers--;
		if (f->kind == F_PIPE_W)
			f->pp->writers--;
		if (f->kind == F_PIPE_R || f->kind == F_PIPE_W)
			wakeup(f->pp);
		f->kind = F_NONE;
	}
}

static int pipe_read(struct pipe *pp, char *buf, unsigned n)
{
	unsigned got = 0;
	int s = spl7();

	while (pp->count == 0 && pp->writers > 0)
		sleep(pp);
	while (got < n && pp->count > 0) {
		buf[got++] = pp->buf[pp->tail];
		pp->tail = (pp->tail + 1) % PIPESIZ;
		pp->count--;
	}
	wakeup(pp);
	splx(s);
	return (int)got;
}

static int pipe_write(struct pipe *pp, const char *buf, unsigned n)
{
	unsigned put = 0;
	int s = spl7();

	while (put < n) {
		if (pp->readers == 0) {
			splx(s);
			return put ? (int)put : -EPIPE;
		}
		if (pp->count == PIPESIZ) {
			wakeup(pp);
			sleep(pp);
			continue;
		}
		pp->buf[pp->head] = buf[put++];
		pp->head = (pp->head + 1) % PIPESIZ;
		pp->count++;
	}
	wakeup(pp);
	splx(s);
	return (int)put;
}

/* The console for the first process: 0, 1 and 2. */
void files_init(void)
{
	struct file *tty = falloc();
	int i;

	if (!tty)
		panic("no file for the console");
	tty->kind = F_TTY;
	tty->readable = tty->writable = 1;
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
	if (!f->readable)
		return EBADF;
	if (f->kind == F_PIPE_R) {
		tf->r0 = (unsigned)pipe_read(f->pp, (char *)arg[1], arg[2]);
		return 0;
	}
	if ((f->ip->d.mode & S_IFMT) == S_IFBLK)
		n = devrw(f->ip, f->off, (char *)arg[1], arg[2], 0);
	else
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
	int n;

	if (!f)
		return EBADF;
	if (!inside(arg[1], arg[2]))
		return EFAULT;
	if (f->kind == F_TTY) {
		tf->r0 = (unsigned)tty_write((const char *)arg[1], (int)arg[2]);
		return 0;
	}
	if (!f->writable)
		return EBADF;
	if (f->kind == F_INODE) {
		if ((f->ip->d.mode & S_IFMT) == S_IFBLK)
			n = devrw(f->ip, f->off, (char *)arg[1], arg[2], 1);
		else
			n = writei(f->ip, f->off, (const char *)arg[1], arg[2]);
		if (n < 0)
			return -n;
		f->off += (unsigned)n;
		tf->r0 = (unsigned)n;
		return 0;
	}
	n = pipe_write(f->pp, (const char *)arg[1], arg[2]);
	if (n < 0)
		return -n;
	tf->r0 = (unsigned)n;
	return 0;
}

/* The lowest descriptor free, or -1. */
static int fd_free(void)
{
	int fd;

	for (fd = 0; fd < NOFILE; fd++)
		if (!curproc->ofile[fd])
			return fd;
	return -1;
}

static int sys_dup(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);
	int fd;

	if (!f)
		return EBADF;
	if ((fd = fd_free()) < 0)
		return EMFILE;
	f->count++;
	curproc->ofile[fd] = f;
	tf->r0 = (unsigned)fd;
	return 0;
}

/* pipe(int fd[2]): the reading end in fd[0], the writing one in fd[1]. */
static int sys_pipe(struct trapframe *tf, const unsigned *arg)
{
	struct pipe *pp = 0;
	struct file *r, *w;
	int i, rfd, wfd;

	if (!inside(arg[0], 4))
		return EFAULT;
	for (i = 0; i < NPIPE && !pp; i++)
		if (!HIGH->pipes[i].readers && !HIGH->pipes[i].writers)
			pp = &HIGH->pipes[i];
	if (!pp)
		return ENFILE;
	if ((rfd = fd_free()) < 0)
		return EMFILE;
	if (!(r = falloc()))
		return ENFILE;
	curproc->ofile[rfd] = r;
	if ((wfd = fd_free()) < 0 || !(w = falloc())) {
		curproc->ofile[rfd] = 0;
		r->count = 0;
		return wfd < 0 ? EMFILE : ENFILE;
	}
	pp->head = pp->tail = pp->count = 0;
	pp->readers = pp->writers = 1;
	r->kind = F_PIPE_R;
	r->readable = 1;
	r->writable = 0;
	r->pp = pp;
	w->kind = F_PIPE_W;
	w->readable = 0;
	w->writable = 1;
	w->pp = pp;
	curproc->ofile[wfd] = w;
	((int *)arg[0])[0] = rfd;
	((int *)arg[0])[1] = wfd;
	tf->r0 = 0;
	return 0;
}

/* A descriptor for the inode, held already: mode 0 to read, 1 to write,
 * 2 both, as V6 has it.  The inode is let go on an error. */
static int fd_of_inode(struct trapframe *tf, struct inode *ip, unsigned mode)
{
	struct file *f;
	unsigned fd;

	for (fd = 0; fd < NOFILE && curproc->ofile[fd]; fd++)
		;
	if (fd == NOFILE || !(f = falloc())) {
		iput(ip);
		return fd == NOFILE ? EMFILE : ENFILE;
	}
	f->kind = F_INODE;
	f->ip = ip;
	f->readable = mode != 1;
	f->writable = mode != 0;
	curproc->ofile[fd] = f;
	tf->r0 = fd;
	return 0;
}

static int sys_open(struct trapframe *tf, const unsigned *arg)
{
	struct inode *ip;
	int error = 0;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if (arg[1] > 2)
		return EINVAL;
	if (!(ip = namei((const char *)arg[0], &error)))
		return error;
	if (arg[1] != 0 && (ip->d.mode & S_IFMT) == S_IFDIR) {
		iput(ip);
		return EISDIR;
	}
	if (arg[1] != 0 && (ip->d.mode & S_IFMT) == S_IFBLK && fs_mounted(ip->d.ext[0].start)) {
		iput(ip);
		return EBUSY;			/* not to be written under its volume */
	}
	return fd_of_inode(tf, ip, arg[1]);
}

/* creat(path, mode): the file made or emptied, open to write. */
static int sys_creat(struct trapframe *tf, const unsigned *arg)
{
	struct inode *ip;
	int error = 0;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if (!(ip = fs_creat((const char *)arg[0], &error)))
		return error;
	return fd_of_inode(tf, ip, 1);
}

/* unlink(path) and mkdir(path): the path checked, the work fs.c's. */
static int sys_path(struct trapframe *tf, const unsigned *arg, int (*op)(const char *))
{
	int error;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if ((error = op((const char *)arg[0])) != 0)
		return error;
	tf->r0 = 0;
	return 0;
}

static int sys_close(struct trapframe *tf, const unsigned *arg)
{
	struct file *f = file_of(arg[0]);

	if (!f)
		return EBADF;
	fclose(f);
	curproc->ofile[arg[0]] = 0;
	tf->r0 = 0;
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

/* mount(device, dir), umount(device): the paths checked, the work fs.c's. */
static int sys_mount(struct trapframe *tf, const unsigned *arg)
{
	int error;

	if ((error = user_string(arg[0], PATHMAX)) != 0 || (error = user_string(arg[1], PATHMAX)) != 0)
		return error;
	if ((error = fs_mount_on((const char *)arg[0], (const char *)arg[1])) != 0)
		return error;
	tf->r0 = 0;
	return 0;
}

/* swapon(file): the file, held, becomes swap (proc.c). */
static int sys_swapon(struct trapframe *tf, const unsigned *arg)
{
	struct inode *ip;
	int error = 0;

	if ((error = user_string(arg[0], PATHMAX)) != 0)
		return error;
	if (!(ip = namei((const char *)arg[0], &error)))
		return error;
	if ((error = swap_on(ip)) != 0) {
		iput(ip);
		return error;
	}
	tf->r0 = 0;
	return 0;
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

/* The process gone; pid 1's place goes to getty again - not init, /etc/rc
 * is the boot's - its files closed. */
static void sys_exit(const unsigned *arg)
{
	static const char *const argv[] = {"getty", 0};
	int i;

	if (curproc->pid != 1)
		pexit((int)arg[0]);
	for (i = 3; i < NOFILE; i++)
		if (curproc->ofile[i]) {
			fclose(curproc->ofile[i]);
			curproc->ofile[i] = 0;
		}
	exec("/etc/getty", argv);
	panic("no /etc/getty");
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
		error = sys_close(tf, arg);
		break;
	case SYS_CREAT:
		error = sys_creat(tf, arg);
		break;
	case SYS_UNLINK:
		error = sys_path(tf, arg, fs_unlink);
		break;
	case SYS_MKDIR:
		error = sys_path(tf, arg, fs_mkdir);
		break;
	case SYS_MOUNT:
		error = sys_mount(tf, arg);
		break;
	case SYS_UMOUNT:
		error = sys_path(tf, arg, fs_umount);
		break;
	case SYS_SWAPON:
		error = sys_swapon(tf, arg);
		break;
	case SYS_SYNC:				/* every write goes through at once */
		tf->r0 = 0;
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
	case SYS_DUP:
		error = sys_dup(tf, arg);
		break;
	case SYS_PIPE:
		error = sys_pipe(tf, arg);
		break;
	case SYS_GETPPID:
		tf->r0 = (unsigned)curproc->ppid;
		break;
	default:
		error = ENOSYS;
	}
	bflush();				/* what the call changed is on the disk */
	if (error) {
		tf->r0 = (unsigned)error;
		tf->ps |= 1;
	} else {
		tf->ps &= ~1u;
	}
}
