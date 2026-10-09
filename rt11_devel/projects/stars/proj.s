/ proj.s - the projection's division, cut to size.
/
/ int proj(int x, int z): (x << 7) / z, truncated toward zero, for a
/ star in view - |x| * 128 < 160 * z, so the quotient has eight bits -
/ by eight steps of shift-and-subtract, written out, instead of the
/ sixteen in a loop that C's `/` costs through the general helper.  x at
/ 2(sp), z at 4(sp); r0 back; r0 and r1 free, r2 and r3 saved.

	.globl	_proj

	.text
_proj:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0		/ x
	mov	010(sp), r1		/ z
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
3:	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc
