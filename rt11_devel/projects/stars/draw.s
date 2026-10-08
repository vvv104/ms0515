/ draw.s - a star in view put on the screen.
/
/ void draw(struct star *s): the star's x and y projected - 160 + x*128/z
/ across, 100 - (y*128/z)*5/6 down, the 5/6 from the table aspect[] -
/ and its pixel lit in video memory, the byte's address from rowp[] and
/ the bit from bits[]; the byte and the bit are kept in the star for the
/ erasing (stars.c says why).  A star whose point falls off the screen is
/ left unlit.  The division is proj.s's eight steps, written out here
/ twice: a star in view has |x| * 128 < 160 * z, so the quotient fits
/ eight bits.
/
/ The star: x at 0, y at 2, z at 4, (zseen 6, zgone 8,) p at 10, bit at 12.

	.globl	_draw, _rowp, _aspect, _bits

	.text

/ DIVIDE: r0 = (r0 << 7) / r1, truncated toward zero; r2, r3 lost.
.macro	DIVIDE
	mov	r0, -(sp)		/ the sign, for the end
	bpl	1f
	neg	r0
1:	asl	r0			/ |x| << 7: the top eight bits are the first
	asl	r0			/   remainder, under z since the quotient fits
	asl	r0			/   eight bits; the low eight come in one by one
	asl	r0
	asl	r0
	asl	r0
	asl	r0
	mov	r0, r2
	swab	r2
	bic	$0377, r2		/ r2 = the low eight bits, up top
	swab	r0
	bic	$0177400, r0		/ r0 = the remainder: the top eight bits
	clr	r3			/ r3 = the quotient
.rept	8
	asl	r2
	rol	r0
	asl	r3
	cmp	r0, r1
	blo	2f
	sub	r1, r0
	inc	r3
2:
.endr
	mov	r3, r0
	tst	(sp)+
	bpl	3f
	neg	r0
3:
.endm

_draw:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)
	mov	012(sp), r5		/ the star
	mov	4(r5), r1		/ z
	mov	(r5), r0		/ x
	DIVIDE
	add	$160, r0
	cmp	r0, $319
	bhi	9f			/ off the screen either side (unsigned)
	mov	r0, r4			/ r4 = sx
	mov	2(r5), r0		/ y
	DIVIDE
	mov	r0, r3
	bpl	4f
	neg	r0
4:	movb	_aspect(r0), r0		/ the CRT's pixels are taller than wide
	tst	r3
	bmi	5f
	neg	r0			/ y up: the point above the centre
5:	add	$100, r0
	cmp	r0, $199
	bhi	9f
	asl	r0
	mov	_rowp(r0), r0		/ the row's first byte
	mov	r4, r1
	asr	r1
	asr	r1
	asr	r1
	asl	r1
	add	r1, r0			/ the pixel's byte
	bic	$0177770, r4
	movb	_bits(r4), r1		/ the pixel's bit
	bisb	r1, (r0)
	mov	r0, 012(r5)
	movb	r1, 014(r5)
9:	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc
