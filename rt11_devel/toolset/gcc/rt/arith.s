/ arith.s - the 16-bit arithmetic the processor has no instructions for.
/
/ The KR1807VM1 (a T-11) has no EIS: no MUL, DIV, ASH, ASHC.  GCC with
/ -m10 calls these helpers instead - libgcc has them for 32 and 64 bits
/ only, the 16-bit ones are the target's to provide.  Calling convention:
/ the arguments on the stack above the return address, the result in r0,
/ r0 and r1 scratch, r2..r5 preserved.
/
/ Signed division truncates toward zero and the remainder takes the sign
/ of the dividend, as C requires.  Division by zero is not caught: the
/ quotient comes out as all ones and the remainder as the dividend, and
/ the program goes on.

	.globl	___mulhi3, ___divhi3, ___modhi3, ___udivhi3, ___umodhi3

	.text

/ int __mulhi3(int a, int b): the low 16 bits of the product, the same for
/ signed and unsigned.  Shift-and-add, the multiplier shifted logically
/ (clc; ror) so that a negative one ends too.
___mulhi3:
	mov	r2, -(sp)
	mov	4(sp), r1
	mov	6(sp), r2
	clr	r0
1:	clc
	ror	r2
	bcc	2f
	add	r1, r0
2:	asl	r1
	tst	r2
	bne	1b
	mov	(sp)+, r2
	rts	pc

/ int __divhi3(int a, int b)
___divhi3:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0
	mov	010(sp), r1
	jsr	pc, sdiv
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ int __modhi3(int a, int b)
___modhi3:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0
	mov	010(sp), r1
	jsr	pc, sdiv
	mov	r1, r0
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ unsigned __udivhi3(unsigned a, unsigned b)
___udivhi3:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0
	mov	010(sp), r1
	jsr	pc, udiv
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ unsigned __umodhi3(unsigned a, unsigned b)
___umodhi3:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	6(sp), r0
	mov	010(sp), r1
	jsr	pc, udiv
	mov	r1, r0
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ sdiv: r0 = r0 / r1 and r1 = r0 % r1, signed; r2, r3 lost.  The signs
/ are kept on the stack while udiv works on the magnitudes.
sdiv:
	mov	r0, -(sp)		/ the remainder's sign: the dividend's
	mov	r0, r2
	xor	r1, r2
	mov	r2, -(sp)		/ the quotient's sign: the two differ
	tst	r0
	bpl	1f
	neg	r0
1:	tst	r1
	bpl	2f
	neg	r1
2:	jsr	pc, udiv
	tst	(sp)+
	bpl	3f
	neg	r0
3:	tst	(sp)+
	bpl	4f
	neg	r1
4:	rts	pc

/ udiv: r0 = r0 / r1 and r1 = r0 % r1, unsigned; r2, r3 lost.  Sixteen
/ steps of shift-and-subtract.
udiv:
	mov	r1, r2
	clr	r1
	mov	$16, r3
1:	asl	r0
	rol	r1
	cmp	r1, r2
	blo	2f
	sub	r2, r1
	inc	r0
2:	sob	r3, 1b
	rts	pc
