/ rom.s - the kernel's one door into the ROM (rom.h).
/
/ Every call into the ROM goes through the table below: now it holds the
/ native ROM-B's entries, and an own ROM's table can take its place without
/ a change anywhere else (SPEC.md "The ROM").  The calls are made on the
/ kernel's stack in bank 0.

	.text
	.globl	_rom_putc

/ void rom_putc(int c): the character out through the ROM's console, which
/ keeps the registers.
_rom_putc:
	mov	2(sp), r0
	jsr	pc, *romtab+0
	rts	pc

	.data
romtab:	.word	0160000		/ the console: a character out of R0
	.word	0160004		/ the keyboard: a key into R0, C = none
	.word	0160010		/ a disk block by the queue element at 157730
