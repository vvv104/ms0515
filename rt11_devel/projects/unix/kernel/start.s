/ start.s - the kernel's first instructions, at its load address 01000.
/
/ The boot block jumps here with interrupts masked.  The stack goes under
/ the kernel, in bank 0 - below 040000, where the ROM opens the VRAM window
/ on its caller's stack (SPEC.md "Memory map") - the bss is cleared, and
/ main() runs; it does not come back, but should it, the kernel halts.

	.text
	.globl	start, ___main
start:
	mov	$01000, sp
	mov	$_edata, r0
1:	cmp	r0, $_end
	bhis	2f
	clr	(r0)+
	br	1b
2:	jsr	pc, _main
3:	halt
	br	3b

/ GCC's main() calls __main first; there is nothing for it to do.
___main:
	rts	pc
