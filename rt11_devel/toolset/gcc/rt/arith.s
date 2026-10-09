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
	.globl	___mulsi3, ___xorhi3

	.text

/ int __xorhi3(int a, int b): -m10 is the PDP-11/10, which had no XOR
/ either, so GCC calls for one; the T-11 has the instruction.
___xorhi3:
	mov	2(sp), r0
	mov	4(sp), r1
	xor	r1, r0
	rts	pc

/ long __mulsi3(long a, long b): the low 32 bits of the product, the same
/ for signed and unsigned.  A long lies high word first (a.hi at 2(sp),
/ a.lo at 4(sp)) and comes back in r0:r1, the high word in r0.  The low
/ words' full product, then each high word by the other's low word into
/ the high half.
___mulsi3:
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)		/ a.hi 012(sp) a.lo 014(sp) b.hi 016(sp) b.lo 020(sp)
	mov	014(sp), r2
	mov	020(sp), r3
	jsr	pc, mul32		/ r0:r1 = a.lo * b.lo
	mov	r0, r4
	mov	r1, -(sp)		/ the low word, kept; the arguments move up by 2
	mov	014(sp), r2
	mov	022(sp), r3
	jsr	pc, mul32		/ a.hi * b.lo
	add	r1, r4
	mov	016(sp), r2
	mov	020(sp), r3
	jsr	pc, mul32		/ a.lo * b.hi
	add	r1, r4
	mov	(sp)+, r1
	mov	r4, r0
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ The 32-bit divisions.  libgcc has them in C, but GCC 15.2 compiles a
/ signed comparison of longs wrongly when the high words are equal (the
/ low word is compared as if signed - README.md), and its __udivmodsi4
/ falls into that; these take their place, being linked ahead of libgcc.
/ a = r0:r1 and b = r4:r5 inside; the quotient comes back in r0:r1, the
/ remainder in r2:r3.  Division by zero: all ones and the dividend.
	.globl	___udivsi3, ___umodsi3, ___divsi3, ___modsi3

/ unsigned long __udivsi3(unsigned long a, unsigned long b)
___udivsi3:
	jsr	pc, args32
	jsr	pc, udiv32
	br	done32
/ unsigned long __umodsi3(unsigned long a, unsigned long b)
___umodsi3:
	jsr	pc, args32
	jsr	pc, udiv32
	mov	r2, r0
	mov	r3, r1
	br	done32
/ long __divsi3(long a, long b)
___divsi3:
	jsr	pc, args32
	jsr	pc, sdiv32
	br	done32
/ long __modsi3(long a, long b)
___modsi3:
	jsr	pc, args32
	jsr	pc, sdiv32
	mov	r2, r0
	mov	r3, r1
done32:
	mov	(sp)+, r5
	mov	(sp)+, r4
	mov	(sp)+, r3
	mov	(sp)+, r2
	rts	pc

/ args32: r2..r5 saved under the caller's return address, a into r0:r1
/ and b into r4:r5.  Called with the arguments at 4(sp) (the two return
/ addresses above them); it leaves its own return address on top of the
/ saved registers, which done32 pops after it.
args32:
	mov	(sp)+, r0		/ this call's return
	mov	r2, -(sp)
	mov	r3, -(sp)
	mov	r4, -(sp)
	mov	r5, -(sp)		/ a.hi 012(sp) a.lo 014(sp) b.hi 016(sp) b.lo 020(sp)
	mov	r0, -(sp)
	mov	014(sp), r0
	mov	016(sp), r1
	mov	020(sp), r4
	mov	022(sp), r5
	rts	pc

/ sdiv32: the signed division, the magnitudes through udiv32, the
/ quotient's sign the two signs' difference, the remainder's the dividend's.
sdiv32:
	mov	r0, -(sp)
	mov	r0, r2
	xor	r4, r2
	mov	r2, -(sp)
	tst	r0
	bpl	1f
	neg	r1
	adc	r0
	neg	r0
1:	tst	r4
	bpl	2f
	neg	r5
	adc	r4
	neg	r4
2:	jsr	pc, udiv32
	tst	(sp)+
	bpl	3f
	neg	r1
	adc	r0
	neg	r0
3:	tst	(sp)+
	bpl	4f
	neg	r3
	adc	r2
	neg	r2
4:	rts	pc

/ udiv32: r0:r1 / r4:r5 unsigned: 32 steps of shift-and-subtract, the
/ dividend shifted out of r0:r1 into the remainder r2:r3 and the
/ quotient's bits in behind it.  A bit shifted out of the remainder
/ (33 bits) means it is above any divisor.
udiv32:
	clr	r2
	clr	r3
	mov	$32, -(sp)
1:	asl	r1
	rol	r0
	rol	r3
	rol	r2
	bcs	3f
	cmp	r2, r4
	blo	2f
	bhi	3f
	cmp	r3, r5
	blo	2f
3:	sub	r5, r3
	sbc	r2
	sub	r4, r2
	inc	r1
2:	dec	(sp)
	bne	1b
	tst	(sp)+
	rts	pc

/ mul32: r0:r1 = r2 * r3, unsigned, the full 32 bits; r2, r3, r5 lost.
/ The multiplicand grows into r5:r2 as the multiplier's bits go by.
mul32:
	clr	r0
	clr	r1
	clr	r5
1:	clc
	ror	r3
	bcc	2f
	add	r2, r1
	adc	r0
	add	r5, r0
2:	asl	r2
	rol	r5
	tst	r3
	bne	1b
	rts	pc

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
