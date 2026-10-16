/ fd.s - /boot/fd, the diskettes' driver: the VG93 (a WD1793) driven by
/ itself, the way our RT-11 DZ handler drives it
/ (rt11_devel/projects/rt11/handlers/dz/DZ.MAC), told again in the
/ driver's shape.
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
/ nothing of the kernel's.
/
/ What the diskettes hold it to, as DZ is: block n is on track
/ (n/10 + 1) mod 80 - cylinder 0 last - in sector
/ (ILV[n mod 10] + 2*track - 2) mod 10, counted from 1: the 2:1 interleave
/ and the skew of two sectors a track (src/disk/src/Layout.cpp).  Register
/ A is write only; the ROM keeps its copy at 157720, and the copy is
/ changed and pushed out, never the register alone.  The controller does
/ not interrupt on this machine: a block is served to its end, its bytes
/ by DRQ at priority 7, the rest - seeking, waiting for the sector - with
/ the clock and the keyboard let in.
/
/ Where DZ asks the controller at every request which track the head is
/ on (Read Address, a turn of the diskette), this asks only when the drive
/ changes, the motor was started, or a try failed: the track register is
/ the head's while one drive is used.

	.text
	.globl	start

CSR	= 0177640		/ command (write), status (read)
TRK	= 0177642		/ the track register
SEC	= 0177644		/ the sector register
DAT	= 0177646		/ the data register

REGA	= 0177600		/ System Register A, write only
REGAC	= 0157720		/ the ROM's copy of it
RA_DRV	= 3			/ bits 0-1: the drive
RA_MOT	= 4			/ bit 2: the motor, on when clear
RA_SID	= 010			/ bit 3: the side, side 0 when set
RA_USE	= 040000		/ the copy's bit 14: the drive just used

C_REST	= 3			/ restore: out to track 0
C_SEEK	= 020			/ seek to the track in the data register
C_RDAD	= 0300			/ read address: the next sector's header
C_READ	= 0200			/ read sector
C_WRIT	= 0240			/ write sector
S_BUSY	= 1
S_DRQ	= 2
F_SEEK	= 0220			/ not ready, seek error
F_READ	= 0234			/ not ready, not found, CRC, lost data
F_WRIT	= 0334			/ the same and write protected

SECTRK	= 10
TRACKS	= 80
BLKSIZ	= 512
RETRY	= 8

start:	.word	042106		/ "FD"
	.word	1		/ the door's version
	.word	end - start	/ the image's bytes
	.word	init
	.word	rw

/ int init(void): no drive's head is known yet.
init:	mov	$-1, lastdrv
	clr	r0
	rts	pc

/ int rw(unsigned unit, unsigned block, void *buf, int write)
rw:	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	mov	12(sp), block
	mov	14(sp), buffer
	mov	$C_READ, comand
	mov	$F_READ, fails
	tst	16(sp)
	beq	1f
	mov	$C_WRIT, comand
	mov	$F_WRIT, fails
1:	mov	10(sp), r0		/ the unit: bit 0 the drive, bit 1 the side
	mov	r0, r1
	bic	$0177776, r1
	bit	$2, r0
	bne	2f
	bis	$RA_SID, r1		/ side 0
2:	bic	$RA_DRV+RA_SID, *$REGAC
	bis	r1, *$REGAC
	bis	$RA_USE, *$REGAC
	bit	$RA_MOT, *$REGAC	/ the motor off?
	beq	3f
	bic	$RA_MOT, *$REGAC	/ started, and let come up to speed
	movb	*$REGAC, *$REGA
	jsr	pc, spinup
	mov	$-1, lastdrv		/ and its head looked for again
3:	movb	*$REGAC, *$REGA
	bic	$0177776, r0		/ the drive
	cmp	r0, lastdrv
	beq	4f
	mov	r0, lastdrv
	jsr	pc, where
4:	mov	$RETRY, tries
5:	jsr	pc, locate		/ r2 the track, r3 the sector
	jsr	pc, seek
	bcs	6f
	jsr	pc, move
	bcc	7f
6:	jsr	pc, home		/ failed: the head found again
	dec	tries			/ and the try made anew
	bne	5b
	mov	$-1, lastdrv
	mov	$-1, r0
	br	8f
7:	clr	r0
8:	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ locate: the track of block into r2, its sector into r3; r0, r1 used.
/ No divide on this machine: ten into 799 is eighty rounds at the worst.
locate:	mov	block, r0
	clr	r2
1:	cmp	r0, $SECTRK
	blo	2f
	sub	$SECTRK, r0
	inc	r2
	br	1b
2:	inc	r2			/ cylinder 0 comes last
	cmp	r2, $TRACKS
	blo	3f
	clr	r2
3:	movb	ilv(r0), r3
	mov	r2, r1
	asl	r1			/ two sectors of skew a track
	add	r1, r3
	add	$SECTRK-2, r3		/ the -2 kept from going negative
4:	cmp	r3, $SECTRK
	blo	5f
	sub	$SECTRK, r3
	br	4b
5:	inc	r3			/ the controller counts from 1
	rts	pc

/ where: the track the selected drive's head is on, into the track
/ register - read off the next sector header (Read Address leaves it in
/ the sector register); with none to read, the head brought home.
where:	movb	$C_RDAD, *$CSR
	jsr	pc, idle
	bitb	$F_SEEK, *$CSR
	bne	home
	movb	*$SEC, *$TRK
	rts	pc

/ home: the head out to track 0, where it and the register agree.
home:	movb	$C_REST, *$CSR
	jsr	pc, idle
	rts	pc

/ seek: the head to the track in r2; C set if it failed.
seek:	cmpb	r2, *$TRK
	beq	1f
	movb	r2, *$DAT
	movb	$C_SEEK, *$CSR
	jsr	pc, idle
	bitb	$F_SEEK, *$CSR
	beq	1f
	sec
	rts	pc
1:	clc
	rts	pc

/ move: the sector in r3 read into buffer or written from it, its 512
/ bytes by DRQ with nothing let in - a byte not taken in time is lost; C
/ set if it failed.
move:	mov	$CSR, r0
	movb	r3, *$SEC
	mov	buffer, r1
	mov	$BLKSIZ, r2
	mfps	-(sp)
	mtps	$0340
	movb	comand, (r0)
	cmpb	comand, $C_WRIT
	beq	3f
1:	bitb	$S_DRQ, (r0)		/ reading
	bne	2f
	bitb	$S_BUSY, (r0)
	bne	1b
	br	6f
2:	movb	*$DAT, r4
	tst	r2			/ past the block: dropped
	beq	1b
	movb	r4, (r1)+
	dec	r2
	br	1b
3:	bitb	$S_DRQ, (r0)		/ writing
	bne	4f
	bitb	$S_BUSY, (r0)
	bne	3b
	br	6f
4:	tst	r2
	beq	5f
	movb	(r1)+, *$DAT
	dec	r2
	br	3b
5:	clrb	*$DAT			/ past the block: zeros
	br	3b
6:	mtps	(sp)+
	bitb	fails, (r0)
	beq	7f
	sec
	rts	pc
7:	clc
	rts	pc

/ idle: the controller done with what it was told.
idle:	bitb	$S_BUSY, *$CSR
	bne	idle
	rts	pc

/ spinup: the time a stopped motor takes to reach its speed.
spinup:	mov	$3, r1
1:	clr	r0
2:	sob	r0, 2b
	sob	r1, 1b
	rts	pc

ilv:	.byte	0, 2, 4, 6, 8, 1, 3, 5, 7, 9	/ 2:1
	.even
block:	.word	0		/ the block being moved
buffer:	.word	0		/ where in memory
comand:	.word	0		/ C_READ or C_WRIT
fails:	.word	0		/ the status bits that fail it
tries:	.word	0		/ tries left for the block
lastdrv: .word	-1		/ the drive whose head the track register holds
end:
