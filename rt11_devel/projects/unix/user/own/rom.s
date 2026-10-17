/ rom.s - the ROM's services for a program that owns the machine (own.h),
/ called straight: the program's stack is its own, above the VRAM window
/ ROM-B opens over 040000..077777 on its caller's stack.  The ROM may
/ spoil any register; each call keeps r2..r5 for C.

	.text
	.globl	_own_putc, _own_getc, _own_block, _own_exit

ROMELM	= 0157730		/ the ROM's cell for the disk element's address
ROMREGA	= 0157720		/ the ROM's copy of register A, the motor flags

/ void own_putc(int c)
_own_putc:
	mov	2(sp), r0
	mov	$0160000, r1
	br	call

/ int own_getc(void): the key's byte, -1 when there is none.
_own_getc:
	mov	$0160004, r1
	jsr	pc, call
	bcs	1f
	bic	$0177400, r0
	rts	pc
1:	mov	$-1, r0
	rts	pc

/ int own_block(unsigned unit, unsigned block, void *buf, int write)
_own_block:
	movb	2(sp), elem + 3		/ the unit in the element's high byte
	mov	4(sp), elem
	mov	6(sp), elem + 4
	mov	$256, elem + 6
	tst	8(sp)
	beq	1f
	neg	elem + 6		/ a write: the word count negative
1:	bis	$0100000, *$ROMREGA	/ the motor armed, as the ROM's boot does
	mov	$elem, *$ROMELM
	mov	$0160010, r1
	jsr	pc, call
	mov	$0, r0
	sbc	r0			/ -1 when the ROM set C
	rts	pc

/ void own_exit(void): the ROM's reset.
_own_exit:
	jmp	*$0172000

/ call: the ROM's entry in r1, its argument in r0, r2..r5 kept round it;
/ back with the ROM's r0 and C.
call:	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	jsr	pc, (r1)
	mov	(sp)+, r5		/ MOV leaves C as the ROM set it
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

	.data
elem:	.word	0, 0, 0, 0	/ the disk element: block, unit, buffer, words
