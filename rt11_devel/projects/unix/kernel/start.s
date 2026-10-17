/ start.s - the kernel's first instructions, at its load address 01000.
/
/ The boot block jumps here with interrupts masked.  The stack goes under
/ the kernel, in bank 0 - below 040000, where the ROM opens the VRAM window
/ on its caller's stack (SPEC.md "Memory map").  The kernel's code is
/ linked at 001000, its data at 140000 in bank 6 (high.h); the image holds
/ the data's first values right after the code, where the boot block has
/ put them - into bank 2 when the code ends near 040000, free at the boot -
/ so they are copied up, the bss cleared, and main() runs; it does not
/ come back, but should it, the kernel halts.

	.text
	.globl	start, ___main, _vectors, _xcopy, _etext, _edata, _end

/ The vectors' page as C sees it (kernel.h): an array at address 0, which
/ the compiler cannot take for a null pointer's target.
_vectors = 0
start:
	mov	$01000, sp
	mov	$_etext, r1		/ the data's values, after the code
	mov	$data, r0		/ their place
4:	cmp	r0, $_edata
	bhis	5f
	mov	(r1)+, (r0)+
	br	4b
5:	mov	$_edata, r0
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
/ or of bank 6 on, another slot - and then back as the ROM's copy of it has
/ it.  It is here, in bank 0's code, for code switching bank 1 or bank 6
/ must not lie in it (SPEC.md "Memory map"); at priority 7, the handlers
/ being in bank 1 too, and not a word pushed while the banks are switched:
/ the kernel's stacks are in bank 6.
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

/ The data's first word: start.s is linked first.  Nothing of the text
/ after this - the data lie in bank 6, and code switching banks must not.
	.data
data:
