/ rom.s - the kernel's one door into the ROM (rom.h).
/
/ Every call into the ROM goes through the table below: now it holds the
/ native ROM-B's entries, and an own ROM's table can take its place without
/ a change anywhere else (SPEC.md "The ROM").  A call is made at priority 7
/ and on a stack of its own, 157000-157400 below the ROM's cells (high.h),
/ out of the VRAM window's way: ROM-B opens the window over
/ 040000..077777 on the stack it is entered with, and its character out is
/ not to be entered again from an interrupt in the middle of a character
/ (the DEC monitor's lesson, rt11_devel/projects/rt11/monitor/OMCONS.MAC).

	.text
	.globl	_rom_putc, _rom_getc, _rom_blink, _rom_bread, _rom_bwrite

ROMELM	= 0157730		/ the ROM's cell for the disk element's address
ROMREGA	= 0157720		/ the ROM's copy of register A, the motor flags
ROMFLAG	= 0157760		/ the ROM's console flags, bit 2 RUS

/ The ROM's entry at offset r1 in romtab, r0 its argument; r2..r5 kept.
/ Back with the ROM's r0, and r1 = its carry: 1 when it set C.
romcall:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	mfps	r2
	mtps	$0340
	mov	sp, romsp
	mov	$romtop, sp
	jsr	pc, *romtab(r1)
	mov	romsp, sp		/ MOV leaves C as the ROM set it
	mov	$0, r1
	adc	r1
	mtps	r2
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ void rom_putc(int c): the character out through the console.  ROM-B's bit
/ 2 of 157760 is RUS/LAT's state, the keyboard's and the console's at once:
/ set, it draws the codes 0100..0177 with the Cyrillic glyphs, as KOI-7
/ would.  The console here is eight-bit KOI-8 - the Russian letters are
/ 0300..0377, drawn Cyrillic whatever the bit - so the bit is taken off for
/ the character and put back for the keyboard after it.  No interrupt
/ comes between: the call is made at priority 7.
_rom_putc:
	mov	r2, -(sp)
	mfps	r2
	mtps	$0340
	mov	*$ROMFLAG, -(sp)
	bic	$4, *$ROMFLAG
	mov	6(sp), r0
	clr	r1
	jsr	pc, romcall
	bit	$4, (sp)+
	beq	1f
	bis	$4, *$ROMFLAG
1:	mtps	r2
	mov	(sp)+, r2
	rts	pc

/ int rom_getc(void): the next key's byte, -1 when there is none.
_rom_getc:
	mov	$2, r1
	jsr	pc, romcall
	tst	r1
	bne	1f
	bic	$0177400, r0
	rts	pc
1:	mov	$-1, r0
	rts	pc

/ void rom_blink(void): the cursor's blink, a step.
_rom_blink:
	mov	$6, r1
	jsr	pc, romcall
	rts	pc

/ int rom_bread(unsigned unit, unsigned block, void *buf): the block of
/ the unit - a drive's side, side * 2 + drive - read into buf, 0 or -1.
_rom_bread:
	mov	$256, wcount
	br	romblock

/ int rom_bwrite(unsigned unit, unsigned block, const void *buf): the same
/ written - the
/ element's word count negative, as RT-11 asks for a write.
_rom_bwrite:
	mov	$-256, wcount

romblock:
	movb	2(sp), elem+3		/ the unit in the element's high byte
	mov	4(sp), elem
	mov	6(sp), elem+4
	mov	wcount, elem+6
	bis	$0100000, *$ROMREGA	/ the motor armed, as the ROM's boot does
	mov	$elem, *$ROMELM
	mov	$4, r1
	jsr	pc, romcall
	mov	r1, r0
	neg	r0
	rts	pc

	.data
romtab:	.word	0160000		/ the console: a character out of R0
	.word	0160004		/ the keyboard: a key into R0, C = none
	.word	0160010		/ a disk block by the queue element at 157730
	.word	0160014		/ the cursor's blink
elem:	.word	0, 0, 0, 0	/ the disk element: block, unit, buffer, words
wcount:	.word	0		/ 256 to read, -256 to write

	.bss
romsp:	.=.+2

romtop	= 0157400
