/* proc.c - the processes: the table, the slots, switching, fork, exit and
 * wait.
 *
 * Two processes are resident, one in each slot: banks 2-5 in their
 * primary copies (slot 0) or their extra ones (slot 1), switched by bits
 * 2-5 of the dispatcher through the ROM's copy of it.  Each process has its
 * own kernel stack in bank 0, where it sleeps when it waits inside the
 * kernel; swtch() goes from one stack to the other.  There is no swap yet,
 * so there are never more than two processes alive: a fork with both slots
 * taken fails with EAGAIN. */
#include "kernel.h"
#include "proc.h"

#define EAGAIN 11
#define ECHILD 10
#define SLOT_BITS 074			/* dispatcher bits 2-5: banks 2-5 primary when set */

static struct proc procs[NPROC];
struct proc *curproc;
unsigned kstack_top;
static int nextpid = 1;
static char slot_used[NSLOT];
static char bounce[512];		/* fork's copy goes through it, in bank 0 */

void setslot(int slot)
{
	int s = spl7();

	DISP_COPY = (DISP_COPY & ~SLOT_BITS) | (slot == 0 ? SLOT_BITS : 0);
	DISPATCHER = DISP_COPY;
	splx(s);
}

/* The first process, pid 1, in slot 0: init, then the shell it becomes. */
void proc_init(void)
{
	struct proc *p = &procs[0];

	p->state = P_RUN;
	p->pid = nextpid++;
	p->ppid = 0;
	p->slot = 0;
	slot_used[0] = 1;
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	setslot(0);
}

/* Another process to run, or the processor waits for an interrupt until
 * there is one; then it is switched to. */
static void sched(void)
{
	struct proc *p, *from = curproc;
	int i, s;

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
	curproc = p;
	kstack_top = (unsigned)(p->kstack + KSTACK);
	if (p != from) {
		setslot(p->slot);
		swtch(from, p);
	}
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
	struct trapframe *ctf;
	unsigned *ks;
	int i, slot;

	for (slot = 0; slot < NSLOT && slot_used[slot]; slot++)
		;
	for (i = 0; i < NPROC && !c; i++)
		if (procs[i].state == P_FREE)
			c = &procs[i];
	if (slot == NSLOT || !c)
		return -EAGAIN;
	/* The image: the program and its data, and the stack from the frame up. */
	copy_to_slot(USER_BASE, curproc->brk, slot);
	copy_to_slot((unsigned)tf, USER_TOP, slot);
	/* The child's frame, at the same address in its slot: fork gives it 0. */
	setslot(slot);
	ctf = tf;
	ctf->r0 = 0;
	ctf->ps &= ~1u;
	setslot(curproc->slot);

	slot_used[slot] = 1;
	c->slot = slot;
	c->pid = nextpid++;
	c->ppid = curproc->pid;
	c->brk = curproc->brk;
	c->tf = tf;
	c->chan = 0;
	for (i = 0; i < NOFILE; i++)
		if ((c->ofile[i] = curproc->ofile[i]) != 0)
			c->ofile[i]->count++;
	/* Its kernel stack as swtch() leaves one: r5..r2 and a return to
	 * forkret, which takes the frame's address above it and goes back to
	 * the process through the frame. */
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
	slot_used[curproc->slot] = 0;
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
