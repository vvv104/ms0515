/* proc.c - the processes: the table, the slots, swap, switching, fork,
 * exit and wait.
 *
 * Two processes are resident, one in each slot: banks 2-5 in their
 * primary copies (slot 0) or their extra ones (slot 1), switched by bits
 * 2-5 of the dispatcher through the ROM's copy of it.  Each process has its
 * own kernel stack in bank 6 (high.h), where it sleeps when it waits inside
 * the kernel; swtch() goes from one stack to the other.
 *
 * With the RAM disk there may be more processes than slots: each has an
 * area of 32 KB on the disk, and a process to run that is not resident
 * comes in there, in the place of one that is - a sleeping one if there is
 * - which goes out.  Only the image's used part moves: the program with its
 * data and bss, up to brk, and the stack from the frame of the system call
 * it is in, both in whole pages.  Without the RAM disk a fork with both
 * slots taken fails with EAGAIN. */
#include "high.h"
#include "ramdisk.h"

#define EAGAIN 11
#define ECHILD 10
#define SLOT_BITS 074			/* dispatcher bits 2-5: banks 2-5 primary when set */
#define SWAP_PAGES ((USER_TOP - USER_BASE) / RD_PAGE)	/* a process's area: 128 pages */
#define CANARY 0122525			/* the kernel stack's bottom word, while it holds */
#define STACK_BOTTOM(p) (*(unsigned *)(p)->kstack)

#define procs (HIGH->procs)
#define bounce (HIGH->bounce)

/* The size of the bank-6 tables, for tools/mkfs.py's report. */
const unsigned high_size = sizeof(struct high);

struct proc *curproc;
unsigned kstack_top;
static int nextpid = 1;
static struct proc *slot_owner[NSLOT];

void setslot(int slot)
{
	int s = spl7();

	DISP_COPY = (DISP_COPY & ~SLOT_BITS) | (slot == 0 ? SLOT_BITS : 0);
	DISPATCHER = DISP_COPY;
	splx(s);
}

/* The bank-6 tables cleared, and the first process, pid 1, in slot 0:
 * init, then the shell it becomes. */
void proc_init(void)
{
	struct proc *p = &procs[0];

	memset(HIGH, 0, sizeof(struct high));
	p->state = P_RUN;
	p->pid = nextpid++;
	p->ppid = 0;
	p->slot = 0;
	p->resident = 1;
	STACK_BOTTOM(p) = CANARY;
	slot_owner[0] = p;
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(0);
}

/* The pages of [from, to) of the slot in hand, whole, to or from p's area. */
static void move_pages(const struct proc *p, unsigned from, unsigned to, int out)
{
	unsigned first, last;

	if (from >= to)
		return;
	first = (from - USER_BASE) / RD_PAGE;
	last = (to - USER_BASE + RD_PAGE - 1) / RD_PAGE;
	if (out)
		rd_write((unsigned)(p - procs) * SWAP_PAGES + first, (void *)(USER_BASE + first * RD_PAGE), last - first);
	else
		rd_read((unsigned)(p - procs) * SWAP_PAGES + first, (void *)(USER_BASE + first * RD_PAGE), last - first);
}

static void swap_out(struct proc *p)
{
	setslot(p->slot);
	p->stack_lo = (unsigned)p->tf;
	move_pages(p, USER_BASE, p->brk, 1);
	move_pages(p, p->stack_lo, USER_TOP, 1);
	slot_owner[p->slot] = 0;
	p->resident = 0;
}

static void swap_in(struct proc *p, int slot)
{
	setslot(slot);
	move_pages(p, USER_BASE, p->brk, 0);
	move_pages(p, p->stack_lo, USER_TOP, 0);
	p->slot = slot;
	p->resident = 1;
	slot_owner[slot] = p;
}

/* A slot for p: a free one, or a sleeping process's - else any other's -
 * swapped out. */
static void make_resident(struct proc *p)
{
	int slot;

	for (slot = 0; slot < NSLOT && slot_owner[slot]; slot++)
		;
	if (slot == NSLOT) {
		for (slot = 0; slot < NSLOT && slot_owner[slot]->state == P_RUN; slot++)
			;
		if (slot == NSLOT)
			slot = slot_owner[0] == curproc ? 1 : 0;
		swap_out(slot_owner[slot]);
	}
	swap_in(p, slot);
}

/* Another process to run, or the processor waits for an interrupt until
 * there is one; it is brought in when it is out, and switched to. */
static void sched(void)
{
	struct proc *p, *from = curproc;
	int i, s;

	if (STACK_BOTTOM(from) != CANARY)
		panic("a kernel stack overflowed");
	for (;;) {
		s = spl7();
		for (i = 1; i <= NPROC; i++) {
			p = &procs[(from - procs + i) % NPROC];
			if (p->state == P_RUN)
				break;
		}
		if (p->state == P_RUN)
			break;
		idle();
		splx(s);
	}
	if (!p->resident)
		make_resident(p);
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(p->slot);
	if (p != from)
		swtch(from, p);
	splx(s);
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
	int i;

	for (i = 0; i < NPROC; i++)
		if (procs[i].state == P_SLEEP && procs[i].chan == chan)
			procs[i].state = P_RUN;
}

/* [from, to) of the current slot into the same addresses of slot `slot`,
 * through the bounce buffer. */
static void copy_to_slot(unsigned from, unsigned to, int slot)
{
	unsigned n;

	while (from < to) {
		n = to - from > sizeof bounce ? sizeof bounce : to - from;
		memcpy(bounce, (void *)from, n);
		setslot(slot);
		memcpy((void *)from, bounce, n);
		setslot(curproc->slot);
		from += n;
	}
}

int fork(struct trapframe *tf)
{
	struct proc *c = 0;
	unsigned *ks, r0, ps;
	int i, slot;

	for (slot = 0; slot < NSLOT && slot_owner[slot]; slot++)
		;
	for (i = 0; i < NPROC && !c; i++)
		if (procs[i].state == P_FREE)
			c = &procs[i];
	if (!c || (slot == NSLOT && !rd_present))
		return -EAGAIN;
	c->brk = curproc->brk;
	c->tf = tf;
	c->stack_lo = (unsigned)tf;
	if (slot < NSLOT) {
		/* Into the free slot: the image, then the frame giving the child 0. */
		copy_to_slot(USER_BASE, curproc->brk, slot);
		copy_to_slot((unsigned)tf, USER_TOP, slot);
		setslot(slot);
		tf->r0 = 0;
		tf->ps &= ~1u;
		setslot(curproc->slot);
		c->slot = slot;
		c->resident = 1;
		slot_owner[slot] = c;
	} else {
		/* Straight to its area on the RAM disk, the frame the child's for
		 * the time of the copy. */
		r0 = tf->r0;
		ps = tf->ps;
		tf->r0 = 0;
		tf->ps &= ~1u;
		move_pages(c, USER_BASE, curproc->brk, 1);
		move_pages(c, (unsigned)tf, USER_TOP, 1);
		tf->r0 = r0;
		tf->ps = ps;
		c->resident = 0;
	}
	c->pid = nextpid++;
	c->ppid = curproc->pid;
	c->chan = 0;
	for (i = 0; i < NOFILE; i++)
		if ((c->ofile[i] = curproc->ofile[i]) != 0)
			c->ofile[i]->count++;
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
	slot_owner[curproc->slot] = 0;
	curproc->resident = 0;
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
		sleep(curproc);
	}
}
