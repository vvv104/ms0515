/ boot.s - block 0 of the system's diskette.
/
/ The native ROM reads block 0 to address 0 and jumps there, interrupts
/ masked (docs/hardware/filesystem.md, SPEC.md "The boot sequence").  The
/ boot block moves itself up into the process's space, out of the kernel's
/ way, reads the superblock (block 1) and, when it carries the system's
/ magic, the kernel's blocks it names to the address it names, and jumps to
/ the kernel's entry.  Every block goes through the ROM's 160010: an RT-11
/ queue element whose address is left at 157730.  Anything amiss - no magic,
/ a read the ROM fails - ends in HALT, the ROM's console monitor.
/
/ The superblock's words (tools/mkimage.py writes them): 0 the magic, 1 the
/ version, 2 the volume's blocks, 3 the kernel's first block, 4 its blocks,
/ 5 its load address, 6 its entry.

LOAD	= 070000		/ where the boot block runs, in bank 3
SB	= 071000		/ where the superblock is read
MAGIC	= 054125		/ 'U', 'X'
ROMDSK	= 0160010		/ the ROM's disk block routine
ROMELM	= 0157730		/ the ROM's cell for the queue element's address
ROMREGA	= 0157720		/ the ROM's copy of register A, the motor flags

	.text
	.globl	start
start:
	nop			/ a boot block begins with NOP
	clr	r0		/ moved up: absolute addresses only until the jump
	mov	$LOAD, r1
	mov	$256, r2
1:	mov	(r0)+, (r1)+
	dec	r2
	bne	1b
	jmp	*$go

go:	mov	$LOAD, sp
	mov	$1, r3			/ the superblock
	mov	$SB, r5
	jsr	pc, bread
	cmp	*$SB, $MAGIC
	bne	fail
	mov	*$SB+6, r3		/ the kernel's first block
	mov	*$SB+8, r4		/ its blocks
	mov	*$SB+10, r5		/ its load address
2:	tst	r4
	beq	3f
	jsr	pc, bread
	inc	r3
	add	$512, r5
	dec	r4
	br	2b
3:	mov	*$SB+12, r0		/ the kernel's entry
	jmp	(r0)

/ Block r3 into the buffer at r5, through the ROM; registers kept.  Blocks
/ 800 and on are side 1's, unit bit 1.
bread:	mov	r1, -(sp)
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	clrb	elem+3
	cmp	r3, $800
	blo	1f
	sub	$800, r3
	movb	$2, elem+3
1:	mov	r3, elem
	mov	r5, elem+4
	mov	$256, elem+6
	bis	$0100000, *$ROMREGA	/ the motor armed, as the ROM's own boot does
	mov	$elem, *$ROMELM
	jsr	pc, *$ROMDSK
	bcs	fail
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	mov	(sp)+, r1
	rts	pc

fail:	halt
	br	fail

/ The queue element: the block, the function and the unit, the buffer, the
/ word count.
elem:	.word	0, 0, 0, 0
