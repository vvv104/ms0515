/ crt0.s - a process's first instructions, at 040000, the start of its slot.
/
/ The kernel enters here with the stack at the slot's top.  The bss is
/ cleared, main() runs, and what it returns goes to exit().

	.text
	.globl	start, ___main
start:
	mov	$_edata, r0
1:	cmp	r0, $_end
	bhis	2f
	clr	(r0)+
	br	1b
2:	jsr	pc, _main
	mov	r0, -(sp)
	jsr	pc, _exit
3:	br	3b

/ GCC's main() calls __main first; there is nothing for it to do.
___main:
	rts	pc
