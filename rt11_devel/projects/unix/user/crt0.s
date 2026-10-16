/ crt0.s - a process's first instructions, at 040000, the start of its slot.
/
/ The kernel enters here with SP at the arguments' count, the pointers to
/ them above it (exec.c).  The bss is cleared, main(argc, argv) runs, and
/ what it returns goes to exit().

	.text
	.globl	start, ___main
start:
	mov	$_edata, r0
1:	cmp	r0, $_end
	bhis	2f
	clr	(r0)+
	br	1b
2:	mov	sp, r1
	mov	r1, r0
	tst	(r0)+			/ argv: the pointers above the count
	mov	r0, -(sp)
	mov	(r1), -(sp)		/ argc
	jsr	pc, _main
	mov	r0, -(sp)
	jsr	pc, _exit
3:	br	3b

/ GCC's main() calls __main first; there is nothing for it to do.
___main:
	rts	pc
