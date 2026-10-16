/ start.s - the kernel's first instructions, at its load address 01000.
/
/ The boot block jumps here with interrupts masked.  The stack goes under
/ the kernel, in bank 0 - below 040000, where the ROM opens the VRAM window
/ on its caller's stack (SPEC.md "Memory map") - the bss is cleared, and
/ main() runs; it does not come back, but should it, the kernel halts.

	.text
	.globl	start, ___main, _vectors, _xcopy

/ The vectors' page as C sees it (kernel.h): an array at address 0, which
/ the compiler cannot take for a null pointer's target.
_vectors = 0
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

/ void xcopy(void *to, const void *from, unsigned words, unsigned disp):
/ words copied with the dispatcher set to disp - the shadow copy of bank 1
/ on, another slot - and then back as the ROM's copy of it has it.  It is
/ here, in bank 0, for code switching bank 1 must not lie in it (SPEC.md
/ "Memory map"); at priority 7, the handlers being in bank 1 too, and not a
/ word pushed while the banks are switched.
_xcopy:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	8(sp), r0
	mov	10(sp), r1
	mov	12(sp), r2
	mfps	r3
	mtps	$0340
	mov	*$0157700, r4
	mov	14(sp), *$0177400
1:	mov	(r1)+, (r0)+
	dec	r2
	bne	1b
	mov	r4, *$0177400
	mtps	r3
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc
