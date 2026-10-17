/ rd.s - /boot/rd, the RAM disk's driver: the EX board's 512 KB, in pages
/ of 256 bytes, two a block (docs/hardware/ramdisk.md).
/
/ Put down flat at 156300 (kernel.h RD_BASE), 320 bytes at most, above
/ /boot/fd, its header first: the magic, the version of the door, its size
/ in bytes, init, rw.  The kernel loads it once the root is mounted and
/ calls init, which tells the blocks the device has - 0, no board - and
/ from then on sends every block of /dev/rd0 to rw:
/
/     unsigned init(void)
/     int rw(unsigned block, void *buf, int write)
/
/ the block 0..1023, 512 bytes into buf or out of it; 0.  A driver calls
/ nothing of the kernel's.
/
/ The board is not in the processor's address space: page p is set in its
/ PPI, the low byte in port A, the high bits with START in port B after a
/ START|RESET that sets the byte counter to 0, and then the page goes
/ byte by byte through the data port, the counter stepping on each.  A
/ page is moved with nothing let in: the board's DRAM wants each access
/ within 100 microseconds.
/
/ The board is found as EX.SYS finds it: two pages written with two
/ patterns and the first read back; a machine without the board answers
/ otherwise, or with a bus error, which a handler of the driver's own on
/ vector 4 counts for the time of the test.

	.text
	.globl	start

PORTA	= 0177530		/ the page's low byte
PORTB	= 0177532		/ its high bits, START, RESET
CTRL	= 0177536		/ the PPI's mode
DATA	= 0177550		/ the page's bytes, one after another
START	= 0200
RESET	= 040
PAGE	= 256
STEPS	= 64			/ a page four bytes a step
BLOCKS	= 1024			/ 512 KB, two pages a block

start:	.word	042122		/ "RD"
	.word	1		/ the door's version
	.word	end - start	/ the image's bytes
	.word	init
	.word	rw

/ unsigned init(void): page 0 written 5, 42, 79... and page 1 each byte
/ the other way, page 0 read back; the blocks if it holds, else 0.
init:	mov	r2, -(sp)
	mov	r3, -(sp)
	mfps	-(sp)
	mtps	$0340
	mov	*$4, -(sp)
	mov	*$6, -(sp)
	mov	$bus, *$4
	mov	$0340, *$6
	clr	nxm
	movb	$0211, *$CTRL		/ mode 0: A and B out, C in
	clr	r1
	mov	$5, r0
	mov	$37, r2
	jsr	pc, fill
	inc	r1
	mov	$0372, r0
	mov	$-37, r2
	jsr	pc, fill
	clr	r1
	jsr	pc, page
	mov	$5, r0
	mov	$PAGE, r3
1:	cmpb	*$DATA, r0
	bne	2f
	add	$37, r0
	sob	r3, 1b
2:	mov	(sp)+, *$6
	mov	(sp)+, *$4
	mtps	(sp)+
	clr	r0
	bis	nxm, r3			/ a byte not as written, or a bus error
	bne	3f
	mov	$BLOCKS, r0
3:	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ int rw(unsigned block, void *buf, int write): the block's two pages,
/ four bytes a step, the data port in r2.
rw:	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r1
	asl	r1			/ the block's first page
	mov	8(sp), r0
	mov	$DATA, r2
	mov	$2, -(sp)		/ the pages to move
1:	mfps	-(sp)
	mtps	$0340
	jsr	pc, page
	mov	$STEPS, r3
	tst	14(sp)			/ write
	bne	3f
2:	movb	(r2), (r0)+
	movb	(r2), (r0)+
	movb	(r2), (r0)+
	movb	(r2), (r0)+
	sob	r3, 2b
	br	4f
3:	movb	(r0)+, (r2)
	movb	(r0)+, (r2)
	movb	(r0)+, (r2)
	movb	(r0)+, (r2)
	sob	r3, 3b
4:	mtps	(sp)+
	inc	r1
	dec	(sp)
	bne	1b
	tst	(sp)+
	clr	r0
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ fill: page r1 written, its bytes from r0 on, r2 apart; r3 used.
fill:	jsr	pc, page
	mov	$PAGE, r3
1:	movb	r0, *$DATA
	add	r2, r0
	sob	r3, 1b
	rts	pc

/ page: page r1 set, its byte counter at 0.
page:	movb	r1, *$PORTA
	movb	$START+RESET, *$PORTB
	mov	r1, -(sp)
	swab	(sp)
	bic	$0177770, (sp)
	bis	$START, (sp)
	movb	(sp)+, *$PORTB
	rts	pc

/ A bus error while the board is looked for: counted, the instruction
/ that made it left behind.
bus:	inc	nxm
	rti

nxm:	.word	0		/ bus errors seen
end:
