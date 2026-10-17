/* proc.c - the processes: the table, the slots, swap, switching, fork,
 * exit and wait.
 *
 * Two processes are resident, one in each slot: banks 2-5 in their
 * primary copies (slot 0) or their extra ones (slot 1), switched by bits
 * 2-5 of the dispatcher through the ROM's copy of it.  Each process has its
 * own kernel stack in bank 6 (high.h), where it sleeps when it waits inside
 * the kernel; swtch() goes from one stack to the other.
 *
 * With swap on there may be more processes than slots.  Swap is a file in
 * one extent (swapon), cut into areas of 32 KB: a process to run that is
 * not resident comes in, in the place of one that is - a sleeping one if
 * there is - which goes out to an area of its own, given back when it comes
 * in again.  Only the image's used part moves, block by block between the
 * slot and the device: the program with its data and bss, up to brk, and
 * the stack from the frame of the system call it is in.  One area is kept
 * for the exchange, so there are at most as many processes as slots and
 * areas less one; without swap a fork with both slots taken fails with
 * EAGAIN. */
#include "high.h"

#define EAGAIN 11
#define ECHILD 10
#define SLOT_BITS 074			/* dispatcher bits 2-5: banks 2-5 primary when set */
#define AREA_BLOCKS ((USER_TOP - USER_BASE) / BSIZE)	/* a process's area: 64 blocks */
#define MAXAREA (NPROC - NSLOT + 1)	/* the ones out of the slots, and one for the exchange */
#define CANARY 0122525			/* the kernel stack's bottom word, while it holds */
#define STACK_BOTTOM(p) (*(unsigned *)(p)->kstack)

#define procs (HIGH->procs)

struct high high;

struct proc *curproc;
unsigned kstack_top;
static struct inode *swapip;		/* the swap file, held while swap is on */
static unsigned swapdev, swapstart, nareas;
static char area_used[MAXAREA];
static int nextpid = 1;
static struct proc *slot_owner[NSLOT];

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
	p->resident = 1;
	p->area = -1;
	STACK_BOTTOM(p) = CANARY;
	slot_owner[0] = p;
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(0);
}

/* swapon(file): the file, one extent of an area at least, is swap from
 * now on; held, so that it is neither removed nor its volume taken out. */
int swap_on(struct inode *ip)
{
	unsigned len = ip->d.ext[0].len;

	if (swapip)
		return EBUSY;
	if ((ip->d.mode & S_IFMT) != S_IFREG || ip->d.nextents != 1 || len < 2 * AREA_BLOCKS)
		return EINVAL;
	swapip = ip;
	swapdev = ip->dev;
	swapstart = ip->d.ext[0].start;
	nareas = len / AREA_BLOCKS > MAXAREA ? MAXAREA : len / AREA_BLOCKS;
	binval(swapdev, swapstart, len);	/* the cache knows its blocks no more */
	return 0;
}

int swap_file(const struct inode *ip)
{
	return ip == swapip;
}

/* The processes there may be: the slots', and the areas' but the one kept. */
static int room(void)
{
	int i, live = 0;

	for (i = 0; i < NPROC; i++)
		live += procs[i].state != P_FREE;
	return live < NSLOT + (swapip ? (int)nareas - 1 : 0);
}

static int area_get(void)
{
	unsigned i;

	for (i = 0; i < nareas; i++)
		if (!area_used[i]) {
			area_used[i] = 1;
			return (int)i;
		}
	panic("no swap area");
}

static void area_put(struct proc *p)
{
	if (p->area >= 0)
		area_used[p->area] = 0;
	p->area = -1;
}

/* The blocks of [from, to) of the slot in hand, whole, to or from p's area. */
static void move_blocks(const struct proc *p, unsigned from, unsigned to, int out)
{
	unsigned b, last, at;

	if (from >= to)
		return;
	last = (to - USER_BASE + BSIZE - 1) / BSIZE;
	for (b = (from - USER_BASE) / BSIZE; b < last; b++) {
		at = swapstart + (unsigned)p->area * AREA_BLOCKS + b;
		if ((out ? dev_write(swapdev, at, (void *)(USER_BASE + b * BSIZE))
			 : dev_read(swapdev, at, (void *)(USER_BASE + b * BSIZE))) != 0)
			panic("swap: a block lost");
	}
}

static void swap_out(struct proc *p)
{
	setslot(p->slot);
	p->stack_lo = (unsigned)p->tf;
	p->area = (signed char)area_get();
	move_blocks(p, USER_BASE, p->brk, 1);
	move_blocks(p, p->stack_lo, USER_TOP, 1);
	slot_owner[p->slot] = 0;
	p->resident = 0;
}

static void swap_in(struct proc *p, int slot)
{
	setslot(slot);
	move_blocks(p, USER_BASE, p->brk, 0);
	move_blocks(p, p->stack_lo, USER_TOP, 0);
	area_put(p);
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
		for (i = 0, p = from; i < NPROC; i++) {	/* the one after from first, from last */
			if (++p == procs + NPROC)
				p = procs;
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
	struct proc *c = 0;
	unsigned *ks, r0, ps;
	int i, slot;

	for (slot = 0; slot < NSLOT && slot_owner[slot]; slot++)
		;
	for (c = procs; c < procs + NPROC && c->state != P_FREE; c++)
		;
	if (c == procs + NPROC)
		c = 0;
	if (!c || !room())
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
		c->area = -1;
		slot_owner[slot] = c;
	} else {
		/* Straight to an area of its own in swap, the frame the child's
		 * for the time of the copy. */
		r0 = tf->r0;
		ps = tf->ps;
		tf->r0 = 0;
		tf->ps &= ~1u;
		c->area = (signed char)area_get();
		move_blocks(c, USER_BASE, curproc->brk, 1);
		move_blocks(c, (unsigned)tf, USER_TOP, 1);
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
	curproc->resident = 0;
	area_put(curproc);
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
