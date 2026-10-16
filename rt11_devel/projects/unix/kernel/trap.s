/ trap.s - the kernel's entries from the hardware, and the processor's
/ priority.
/
/ There are no modes on the T-11: a trap or an interrupt pushes the PC and
/ the PS onto whatever stack is in SP, the process's as well.  A system call
/ saves the process's registers there, as a struct trapframe (trap.h), and
/ goes over to the kernel's stack under 01000; an interrupt keeps the stack
/ it came in on, and its handler is short (the ROM's calls go on a stack of
/ their own, rom.s).

	.text
	.globl	_trap_entry, _clock_entry, _tty_entry
	.globl	_spl7, _splx, _idle, _enter

/ TRAP: the frame r0..r5, pc, ps on the process's stack; syscall(frame) on
/ the kernel's; back with what it left in the frame.
_trap_entry:
	mov	r5, -(sp)
	mov	r4, -(sp)
	mov	r3, -(sp)
	mov	r2, -(sp)
	mov	r1, -(sp)
	mov	r0, -(sp)
	mov	sp, r0
	mov	$01000, sp
	mov	r0, -(sp)
	jsr	pc, _syscall
	mov	(sp)+, sp
	mov	(sp)+, r0
	mov	(sp)+, r1
	mov	(sp)+, r2
	mov	(sp)+, r3
	mov	(sp)+, r4
	mov	(sp)+, r5
	rti

/ The interrupts: r0 and r1 kept (C keeps the rest), the handler called.
_clock_entry:
	mov	r0, -(sp)
	mov	r1, -(sp)
	jsr	pc, _clock_intr
	mov	(sp)+, r1
	mov	(sp)+, r0
	rti

_tty_entry:
	mov	r0, -(sp)
	mov	r1, -(sp)
	jsr	pc, _tty_intr
	mov	(sp)+, r1
	mov	(sp)+, r0
	rti

/ int spl7(void): the priority raised to 7, the PS it had returned.
_spl7:
	mfps	r0
	mtps	$0340
	rts	pc

/ void splx(int ps): the priority put back.
_splx:
	mtps	2(sp)
	rts	pc

/ void idle(void): interrupts let in, the processor waiting for the next;
/ it comes back at priority 0.
_idle:
	mtps	$0
	wait
	rts	pc

/ void enter(unsigned pc, unsigned sp): the process entered at pc with its
/ stack at sp, at priority 0.  It does not come back.
_enter:
	mov	2(sp), r0
	mov	4(sp), sp
	clr	-(sp)
	mov	r0, -(sp)
	rti
