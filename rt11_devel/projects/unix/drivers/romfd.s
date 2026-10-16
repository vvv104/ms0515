/ romfd.s - /boot/fd, the diskettes' driver as a file: the ROM's own 160010
/ behind the driver's door, the door tried before a driver of its own goes
/ through it.
/
/ A driver is put down flat at 155000 (kernel.h DRV_BASE), 1 KB at most,
/ and begins with its header: the magic, the version of the door, its size
/ in bytes, init, rw.  The kernel loads it once the root is mounted, calls
/ init - 0 is ready - and from then on sends every block of a diskette to
/ rw:
/
/     int rw(unsigned unit, unsigned block, void *buf, int write)
/
/ the unit the ROM's, a drive's side (side * 2 + drive), the block 0..799,
/ 512 bytes into buf or out of it; 0, or -1 for an error.  A driver calls
/ nothing of the kernel's.  This one makes the ROM's call as the kernel's
/ rom.s does: at priority 7, on the stack under the ROM's cells.

	.text
	.globl	start

ROMELM	= 0157730		/ the ROM's cell for the disk element's address
ROMREGA	= 0157720		/ the ROM's copy of register A, the motor flags
ROMSTK	= 0157400		/ the stack the ROM is called on (high.h)

start:	.word	042106		/ "FD"
	.word	1		/ the door's version
	.word	end - start	/ the image's bytes
	.word	init
	.word	rw

init:	clr	r0
	rts	pc

rw:	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	movb	10(sp), elem+3		/ the unit in the element's high byte
	mov	12(sp), elem
	mov	14(sp), elem+4
	mov	$256, elem+6		/ words: a read, or negative a write
	tst	16(sp)
	beq	1f
	neg	elem+6
1:	mfps	-(sp)
	mtps	$0340
	bis	$0100000, *$ROMREGA	/ the motor armed, as the ROM's boot does
	mov	$elem, *$ROMELM
	mov	sp, savesp
	mov	$ROMSTK, sp
	jsr	pc, *$0160010
	mov	savesp, sp		/ MOV leaves C as the ROM set it
	mov	$0, r0
	adc	r0
	neg	r0			/ -1 when it set C
	mtps	(sp)+
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

elem:	.word	0, 0, 0, 0	/ the disk element: block, unit, buffer, words
savesp:	.word	0
end:
