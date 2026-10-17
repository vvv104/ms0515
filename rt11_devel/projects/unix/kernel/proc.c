/* proc.c - the processes: the table, the slots, switching, fork, exit
 * and wait.
 *
 * Two processes at once, one in each slot: banks 2-5 in their primary
 * copies (slot 0) or their extra ones (slot 1), switched by bits 2-5 of the
 * dispatcher through the ROM's copy of it.  There is no swap; but a fork
 * with both slots taken parks the parent - the shell, as a rule - when its
 * used part, the program up to brk and the stack from the frame up, fits
 * the shadow copy of bank 6, 8 KB: the child takes the parent's slot, and
 * the parent, runnable, comes back into the first slot that is free - the
 * shell has nothing to do meanwhile but wait for it.  One process is
 * parked at a time; otherwise the fork fails with EAGAIN.  A process that
 * has exited gives its slot back at once and stays in the table, a zombie,
 * until its parent waits; so the table has a place more than the slots.
 * Each process has its own kernel stack in bank 6 (high.h), where it
 * sleeps when it waits inside the kernel; swtch() goes from one stack to
 * the other. */
#include "high.h"

#define EAGAIN 11
#define ECHILD 10
#define SLOT_BITS 074			/* dispatcher bits 2-5: banks 2-5 primary when set */
#define PARK_BASE 0140000u		/* the shadow copy of bank 6 */
#define PARK_TOP  0160000u
#define CANARY 0122525			/* the kernel stack's bottom word, while it holds */
#define STACK_BOTTOM(p) (*(unsigned *)(p)->kstack)

#define procs (HIGH->procs)

struct high high;

struct proc *curproc;
unsigned kstack_top;
static int nextpid = 1;
static struct proc *slot_owner[NSLOT];
static struct proc *parked;

/* The dispatcher with slot `slot` in and the parking's bank on. */
static unsigned park_disp(int slot)
{
	return (DISP_COPY & ~(SLOT_BITS | BANK6_BIT)) | (slot == 0 ? SLOT_BITS : 0);
}

/* [from, to) of slot `slot` to the parking at `at` (out) or back, a block
 * at a time, the interrupts let in between. */
static void park_move(unsigned from, unsigned to, unsigned at, int slot, int out)
{
	unsigned n;

	while (from < to) {
		n = to - from > BSIZE ? BSIZE : to - from;
		if (out)
			xcopy((void *)at, (void *)from, (n + 1) / 2, park_disp(slot));
		else
			xcopy((void *)from, (void *)at, (n + 1) / 2, park_disp(slot));
		from += n;
		at += n;
	}
}

/* p's used part, [USER_BASE, brk) at the parking's bottom and [tf,
 * USER_TOP) at its top, out of slot `slot` or into it. */
static void park_image(const struct proc *p, int slot, int out)
{
	park_move(USER_BASE, p->brk, PARK_BASE, slot, out);
	park_move((unsigned)p->tf, USER_TOP, PARK_TOP - (USER_TOP - (unsigned)p->tf), slot, out);
}

static int park_fits(const struct proc *p)
{
	return p->brk - USER_BASE + (USER_TOP - (unsigned)p->tf) <= PARK_TOP - PARK_BASE;
}

void setslot(int slot)
{
	int s = spl7();

	DISP_COPY = (DISP_COPY & ~SLOT_BITS) | (slot == 0 ? SLOT_BITS : 0);
	DISPATCHER = DISP_COPY;
	splx(s);
}

/* The first process, pid 1, in slot 0: init, then the shell it becomes.
 * The tables are the bss, start.s has cleared them. */
void proc_init(void)
{
	struct proc *p = &procs[0];

	p->state = P_RUN;
	p->pid = nextpid++;
	p->ppid = 0;
	p->slot = 0;
	STACK_BOTTOM(p) = CANARY;
	slot_owner[0] = p;
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(0);
}

/* Another process to run, or the processor waits for an interrupt until
 * there is one; switched to, its slot in. */
static void sched(void)
{
	struct proc *p, *from = curproc;
	int i, s, free;

	if (STACK_BOTTOM(from) != CANARY)
		panic("a kernel stack overflowed");
	for (;;) {
		s = spl7();
		free = !slot_owner[0] ? 0 : !slot_owner[1] ? 1 : -1;
		for (i = 0, p = from; i < NPROC; i++) {	/* the one after from first, from last */
			if (++p == procs + NPROC)
				p = procs;
			if (p->state == P_RUN && (p->slot >= 0 || free >= 0))
				break;
		}
		if (p->state == P_RUN && (p->slot >= 0 || free >= 0))
			break;
		idle();
		splx(s);
	}
	if (p->slot < 0) {			/* the parked one, back into the free slot */
		park_image(p, free, 0);
		p->slot = free;
		slot_owner[free] = p;
		parked = 0;
	}
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(p->slot);
	if (p != from)
		swtch(from, p);
	splx(s);
}

/* The clock's slice over (clock.c resched): the other one runs, if it
 * can.  A process Ctrl+C has taken out (tty.c) exits here instead, from
 * its system call's end or the clock's, with 130 - 128 and the
 * interrupt's 2, as the shells of UNIX tell it. */
void preempt(struct trapframe *tf)
{
	if (curproc->killed)
		pexit(130);
	resched = 0;
	curproc->tf = tf;
	sched();
}

void sleep(const void *chan)
{
	int s = spl7();

	curproc->chan = chan;
	curproc->state = P_SLEEP;
	sched();
	splx(s);
}

void wakeup(const void *chan)
{
	struct proc *p;

	for (p = procs; p < procs + NPROC; p++)
		if (p->state == P_SLEEP && p->chan == chan)
			p->state = P_RUN;
}

/* [from, to) of the current slot into the same addresses of slot `slot`,
 * a block at a time through the shadow bank's scratch block. */
static void copy_to_slot(unsigned from, unsigned to, int slot)
{
	unsigned n, there = (DISP_COPY & ~(SLOT_BITS | BANK1_BIT)) | (slot == 0 ? SLOT_BITS : 0);

	while (from < to) {
		n = to - from > BSIZE ? BSIZE : to - from;
		xcopy(SCRATCH, (void *)from, (n + 1) / 2, DISP_COPY & ~BANK1_BIT);
		xcopy((void *)from, SCRATCH, (n + 1) / 2, there);
		from += n;
	}
}

int fork(struct trapframe *tf)
{
	struct proc *c;
	unsigned *ks;
	int i, slot;

	for (slot = 0; slot < NSLOT && slot_owner[slot]; slot++)
		;
	for (c = procs; c < procs + NPROC && c->state != P_FREE; c++)
		;
	curproc->tf = tf;
	if (c == procs + NPROC || (slot == NSLOT && (parked || !park_fits(curproc))))
		return -EAGAIN;
	c->brk = curproc->brk;
	c->tf = tf;
	if (slot < NSLOT) {
		/* Into the free slot: the image, then the frame giving the child 0. */
		copy_to_slot(USER_BASE, curproc->brk, slot);
		copy_to_slot((unsigned)tf, USER_TOP, slot);
		setslot(slot);
		tf->r0 = 0;
		tf->ps &= ~1u;
		setslot(curproc->slot);
	} else {
		/* The parent parked, the child in its slot with the frame giving
		 * it 0: the parent's own goes back with it. */
		slot = curproc->slot;
		park_image(curproc, slot, 1);
		tf->r0 = 0;
		tf->ps &= ~1u;
		curproc->slot = -1;
		parked = curproc;
	}
	c->slot = slot;
	slot_owner[slot] = c;
	c->pid = nextpid++;
	c->ppid = curproc->pid;
	c->killed = 0;
	c->bg = curproc->bg;
	memcpy(c->name, curproc->name, PNAME);
	c->chan = 0;
	for (i = 0; i < NOFILE; i++)
		if ((c->ofile[i] = curproc->ofile[i]) != 0)
			c->ofile[i]->count++;
	if ((c->cwd = curproc->cwd) != 0)
		c->cwd->count++;
	/* Its kernel stack as swtch() leaves one: r5..r2 and a return to
	 * forkret, which takes the frame's address above it and goes back to
	 * the process through the frame. */
	STACK_BOTTOM(c) = CANARY;
	ks = (unsigned *)(c->kstack + KSTACK);
	*--ks = (unsigned)tf;
	*--ks = (unsigned)forkret;
	for (i = 0; i < 4; i++)
		*--ks = 0;
	c->ksp = (unsigned)ks;
	c->state = P_RUN;
	if (curproc->slot < 0)
		sched();			/* parked: back when a slot is free */
	return c->pid;
}

void pexit(int status)
{
	struct proc *p;
	int i;

	for (i = 0; i < NOFILE; i++)
		if (curproc->ofile[i]) {
			fclose(curproc->ofile[i]);
			curproc->ofile[i] = 0;
		}
	if (curproc->cwd) {
		iput(curproc->cwd);
		curproc->cwd = 0;
	}
	slot_owner[curproc->slot] = 0;
	for (i = 0; i < NPROC; i++) {
		p = &procs[i];
		if (p->state != P_FREE && p->ppid == curproc->pid)
			p->ppid = 1;		/* the orphans go to init */
		if (p->state != P_FREE && p->pid == curproc->ppid)
			wakeup(p);
	}
	curproc->status = status;
	curproc->state = P_ZOMBIE;
	spl7();
	sched();
	panic("a zombie ran");
}

/* A child gone: its pid and, at status_ptr in the slot, its status. */
int pwait(unsigned status_ptr)
{
	struct proc *p;
	int i, any, pid;

	for (;;) {
		any = 0;
		for (i = 0; i < NPROC; i++) {
			p = &procs[i];
			if (p->state == P_FREE || p->ppid != curproc->pid)
				continue;
			any = 1;
			if (p->state == P_ZOMBIE) {
				pid = p->pid;
				if (status_ptr)
					*(int *)status_ptr = p->status;
				p->state = P_FREE;
				return pid;
			}
		}
		if (!any)
			return -ECHILD;
		if (curproc->killed)
			return -EINTR;
		sleep(curproc);
	}
}

/* kill(pid): the process marked to exit as Ctrl+C marks it (preempt), one
 * asleep woken to see it - each sleep is in a loop that looks again; not
 * the session's shell.  0, or the error. */
int pkill(int pid)
{
	struct proc *p;

	for (p = procs; p < procs + NPROC; p++)
		if ((p->state == P_RUN || p->state == P_SLEEP) && p->pid == pid) {
			if (pid == 1)
				return EACCES;
			p->killed = 1;
			if (p->state == P_SLEEP)
				p->state = P_RUN;
			resched = 1;
			return 0;
		}
	return ESRCH;
}

/* ps's view of a process is its head as it is (pinfo.h). */
_Static_assert(sizeof(struct pinfo) == __builtin_offsetof(struct proc, kstack), "pinfo.h is not proc.h's head");
