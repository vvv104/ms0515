/* proc.h - the processes and the open files. */
#ifndef UNIX_PROC_H
#define UNIX_PROC_H

#include "pinfo.h"

#define NPROC 4				/* the process table: a slot each, the parked one, a zombie */
#define NSLOT 2				/* slots: banks 2-5, primary and extra */
#define NFILE 16			/* the open files of all */
#define KSTACK 320			/* a process's kernel stack: 242 the deepest measured */

/* An open file, shared by the processes a fork made, its offset with it. */
struct file {
	enum { F_NONE, F_TTY, F_INODE } kind;
	unsigned char readable, writable;
	unsigned count;			/* the descriptors that hold it */
	struct inode *ip;
	unsigned long off;
};

enum pstate { P_FREE, P_RUN, P_SLEEP, P_ZOMBIE };

struct proc {
	unsigned ksp;			/* the saved kernel SP: first, swtch() knows it at 0 */
	enum pstate state;
	int pid, ppid;
	int slot;			/* 0: banks 2-5 primary, 1: extra, -1 parked */
	const void *chan;		/* what it sleeps on */
	int status;			/* the exit status, while a zombie */
	unsigned brk;			/* the end of its text, data and bss */
	struct trapframe *tf;		/* its frame in the last system call */
	struct file *ofile[NOFILE];
	struct inode *cwd;		/* its own directory, held; 0 the root */
	char killed;			/* Ctrl+C: to exit at its next way out of the kernel */
	char bg;			/* put in the background: Ctrl+C leaves it (TTY_BG) */
	char name[PNAME];		/* argv[0]'s last part, for ps */
	char kstack[KSTACK];
};

extern struct proc *curproc;
extern unsigned kstack_top;		/* trap.s takes it for the kernel's SP */

/* proc.c */
void proc_init(void);
void sleep(const void *chan);
void preempt(struct trapframe *tf);
extern volatile int resched;		/* clock.c: the slice is over */
void wakeup(const void *chan);
void setslot(int slot);
int fork(struct trapframe *tf);
void pexit(int status);
int pwait(unsigned status_ptr);
int pkill(int pid);

/* trap.s */
void swtch(struct proc *from, struct proc *to);
void forkret(void);

/* sys.c */
struct file *falloc(void);
void fclose(struct file *f);

#endif
