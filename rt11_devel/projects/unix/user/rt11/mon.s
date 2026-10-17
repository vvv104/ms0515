/ mon.s - the little RT-11 monitor's way in and out (mon.c): linked flat
/ at 140000, where RT-11's RMON is, and put there by /bin/rt11.
/
/ Its first word is its entry, called as void entry(unsigned unit, unsigned
/ both, const char *path) - the program's volume and the .SAV's path: the
/ monitor's own stack, under 157000, its bss cleared, mon_main.  An EMT
/ comes in on the program's stack: its registers go there under the PC and
/ the PS, and the C half runs on the monitor's stack with the frame - r5
/ first, then r4..r0, the PC, the PS - which it may change: the program's
/ registers and its C bit go back as the frame has them.

	.text
	.globl	_emt_entry, _mon_go, _mon_main, _emt, _mem, _mon_rti, _mon_trap4, _mon_trap10, _trap

MSTACK	= 0157000		/ the monitor's stack, down from here

/ The memory as C sees it (mon.c): an array at address 0.
_mem	= 0

	.word	entry

entry:	mov	2(sp), r0
	mov	4(sp), r1
	mov	6(sp), r2
	mov	$MSTACK, sp
	mov	$_edata, r3
1:	cmp	r3, $_end
	bhis	2f
	clr	(r3)+
	br	1b
2:	mov	r2, -(sp)
	mov	r1, -(sp)
	mov	r0, -(sp)
	jsr	pc, _mon_main		/ does not come back: mon_go enters the program
	jmp	*$0172000

/ void mon_go(unsigned pc, unsigned sp): the program entered at priority 0.
_mon_go:
	mov	2(sp), r0
	mov	4(sp), sp
	clr	-(sp)
	mov	r0, -(sp)
	rti

/ Every vector the program does not set: an interrupt let go.
_mon_rti:
	rti

/ A bus error or an instruction the processor has not: the program's PC
/ told and its end (mon.c trap), on the monitor's stack.
_mon_trap4:
	mov	$4, r0
	br	1f
_mon_trap10:
	mov	$010, r0
1:	mov	(sp), r1
	mov	$MSTACK, sp
	mov	r1, -(sp)
	mov	r0, -(sp)
	jsr	pc, _trap		/ does not come back

_emt_entry:
	mov	r0, -(sp)
	mov	r1, -(sp)
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	mov	sp, r0
	mov	$MSTACK, sp
	mov	r0, -(sp)		/ the program's SP, kept
	mov	r0, -(sp)		/ the frame
	jsr	pc, _emt
	tst	(sp)+
	mov	(sp)+, sp
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	mov	(sp)+, r1
	mov	(sp)+, r0
	rti
