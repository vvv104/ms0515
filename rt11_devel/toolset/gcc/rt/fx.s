/ fx.s - fixed-point arithmetic cut to size (include/fx.h).
/
/ The arguments on the stack above the return address, the result in
/ r0, r0 and r1 free, r2..r5 kept - GCC's convention.

	.globl	_fx_div8, _fx_mul

	.text

/ int fx_div8(int num, int den): num / den, truncated toward zero, for a
/ quotient of eight bits (|num| < 256 * den, den > 0): the top eight
/ bits of |num| are the first remainder, under den, and the low eight
/ come in one by one over eight steps of shift-and-subtract, written out.
_fx_div8:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0		/ num
	mov	010(sp), r1		/ den
	mov	r0, -(sp)		/ the sign, for the end
	bpl	1f
	neg	r0
1:	mov	r0, r2
	swab	r2
	bic	$0377, r2		/ r2 = the low eight bits, up top, to come in one by one
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

/ int fx_mul(int a, int b): a * b / 256 - the full 32-bit product of the
/ magnitudes by shift-and-add, its middle sixteen bits taken, the sign
/ put back.
_fx_mul:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r5, -(sp)
	mov	010(sp), r2		/ a
	mov	012(sp), r3		/ b
	mov	r2, r5
	xor	r3, r5
	mov	r5, -(sp)		/ the product's sign: the two differ
	tst	r2
	bpl	1f
	neg	r2
1:	tst	r3
	bpl	2f
	neg	r3
2:	clr	r0			/ r0:r1 = r2 * r3, the multiplicand growing into r5:r2
	clr	r1
	clr	r5
3:	clc
	ror	r3
	bcc	4f
	add	r2, r1
	adc	r0
	add	r5, r0
4:	asl	r2
	rol	r5
	tst	r3
	bne	3b
	swab	r1			/ the middle sixteen: r0's low byte over r1's high
	bic	$0177400, r1
	swab	r0
	bic	$0377, r0
	bis	r1, r0
	tst	(sp)+
	bpl	5f
	neg	r0
5:	mov	(sp)+, r5
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc
