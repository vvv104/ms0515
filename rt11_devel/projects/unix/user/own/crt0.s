/ crt0.s - the first instructions of a program that owns the machine
/ (own.h): its loader, then its start.
/
/ The loader is the text's first bytes, inside the file's first block,
/ which the kernel has read into the process's slot (kernel/own.c): it is
/ entered at 040020, SP pointing at the ROM's unit, whether the volume is
/ both sides of the drive, the file's five extents (start, length), and
/ the command line - its count and 128 bytes of its strings, one after
/ another.  The blocks to read it takes from the header above it, the
/ header's 16 bytes and the text and the data, a reset when they would
/ come up to its own place.  It copies itself and the words to 157000,
/ the strings to 000400 - under the file's place, over the vectors' -
/ above
/ all the program may take - and from there reads the file block by block
/ through the ROM's 160010 to 000760 and up, the header under 001000 and
/ the text at it, a block from 800 on side 1 when the volume is both
/ sides; every bank primary first.  Then it jumps to the entry the header
/ gives.  It runs at two places, so it takes its own words by relative
/ addresses only; a block the ROM cannot read is a reset.
/
/ The start: the stack the top of the program's room, 157000 down; every
/ vector, 000 to 000376, pointed at an RTI at priority 7 - the kernel's
/ handlers are gone - the bss cleared, the volume the program came from
/ kept for own.h's reader (own_unit, own_both), main(argc, argv) run with
/ the command line the loader left, and the end own_exit.

	.text
	.globl	start, ___main, _own_exit, _own_unit, _own_both

LOADER	= 0157000		/ where the loader runs from the file's
LSTACK	= 0157600		/ its stack, under the ROM's 157614 and up
OWNBASE	= 0760			/ the header's place: the text at 001000
ENTRY	= OWNBASE + 10		/ the header's entry word
SIDE	= 800
DISPAT	= 0177400		/ the dispatcher
DISPCOPY = 0157700		/ the ROM's copy of it
ROMELM	= 0157730		/ the ROM's cell for the disk element's address
ROMREGA	= 0157720		/ the ROM's copy of register A, the motor flags
ROMDISK	= 0160010		/ a disk block by the element
ROMRESET = 0172000		/ the ROM's reset
INFOW	= 13			/ the kernel's words, the strings' 128 bytes after them
ARGS	= 0400			/ where the strings go

own:	mtps	$0340
	mov	pc, r1
1:	sub	$1b - own, r1		/ r1 -> own, where the kernel put it
	mov	-14(r1), r0		/ the header's text
	add	-12(r1), r0		/ and data
	add	$16 + 511, r0		/ and the header: blocks, rounded up
	clrb	r0
	swab	r0
	asr	r0
	cmp	r0, $110		/ (157000 - 760) over 512: the loader's place above
	bhi	lfail
	mov	$LOADER, r2
	mov	r1, r3
	add	$linfo - own, r3	/ r3 -> its end
2:	mov	(r1)+, (r2)+
	cmp	r1, r3
	blo	2b
	mov	$INFOW, r3		/ the kernel's words after it
3:	mov	(sp)+, (r2)+
	sob	r3, 3b
	mov	$ARGS, r2
	mov	$64, r3
4:	mov	(sp)+, (r2)+
	sob	r3, 4b
	mov	r0, *$LOADER + lblocks - own
	mov	$LSTACK, sp
	jmp	*$LOADER + lrun - own

lrun:	bis	$0177, *$DISPCOPY	/ every bank primary
	mov	*$DISPCOPY, *$DISPAT
	mov	$OWNBASE, r4		/ where the next block goes
	mov	pc, r5
1:	add	$lext - 1b, r5		/ r5 -> the extents
	mov	$5, r3
lnext:	mov	(r5)+, r1		/ an extent's first block
	mov	(r5)+, r2		/ and its length
lblock:	tst	lblocks
	beq	ldone
	tst	r2
	beq	lskip
	mov	r1, lelem
	movb	lunit, lelem + 3
	tst	lboth
	beq	2f
	cmp	r1, $SIDE		/ both sides: from 800 on side 1
	blo	2f
	sub	$SIDE, lelem
	bisb	$2, lelem + 3
2:	mov	r4, lelem + 4
	mov	$256, lelem + 6
	mov	pc, r0
3:	add	$lelem - 3b, r0
	mov	r0, *$ROMELM
	bis	$0100000, *$ROMREGA	/ the motor armed, as the ROM's boot does
	mov	r1, -(sp)
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	jsr	pc, *$ROMDISK
	mov	(sp)+, r5		/ MOV leaves C as the ROM set it
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	mov	(sp)+, r1
	bcs	lfail
	add	$512, r4
	inc	r1
	dec	r2
	dec	lblocks
	br	lblock
lskip:	sob	r3, lnext		/ the next extent
lfail:	jmp	*$ROMRESET
ldone:	mov	*$ENTRY, r0
	jmp	(r0)

lelem:	.word	0, 0, 0, 0		/ the disk element: block, unit, buffer, words
lblocks: .word	0			/ the blocks left to read
linfo:					/ the kernel's words, copied here:
lunit:	.word	0			/ the ROM's unit: drive, side 0
lboth:	.word	0			/ the volume both sides
lext:	.word	0, 0, 0, 0, 0, 0, 0, 0, 0, 0
largc:	.word	0

/ Where the loader's words are once it has run, at 157000 and up.
UNIT	= LOADER + lunit - own
BOTH	= LOADER + lboth - own
ARGC	= LOADER + largc - own

start:	mov	$0157000, sp
	clr	r0
1:	mov	$stub, (r0)+
	mov	$0340, (r0)+
	cmp	r0, $ARGS
	blo	1b
	mov	$_edata, r0
2:	cmp	r0, $_end
	bhis	3f
	clr	(r0)+
	br	2b
3:	mov	*$UNIT, _own_unit
	mov	*$BOTH, _own_both
	mov	$ARGS, r1		/ r1 -> the strings
	mov	*$ARGC, r2		/ argc
	mov	r2, r0			/ room for argv: argc + 1 pointers
	inc	r0
	asl	r0
	sub	r0, sp
	mov	sp, r4
	mov	r2, r3
	beq	6f
4:	mov	r1, (r4)+		/ argv[i]
5:	tstb	(r1)+
	bne	5b
	sob	r3, 4b
6:	clr	(r4)
	mov	sp, r0
	mov	r0, -(sp)		/ main(argc, argv)
	mov	r2, -(sp)
	jsr	pc, _main
	jmp	_own_exit

stub:	rti

/ GCC's main() calls __main first; there is nothing for it to do.
___main:
	rts	pc

	.bss
_own_unit: .=.+2			/ the ROM's unit of the program's volume
_own_both: .=.+2			/ that volume both sides of the drive
