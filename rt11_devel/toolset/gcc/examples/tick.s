/ tick.s - the benchmark's clock: the 50 Hz frame interrupt.
/
/ RT-11 keeps no clock on this machine (docs/programming.md): the frame
/ interrupt on vector 100 fires only while dispatcher bit 9 is set.
/ tic0() takes the vector and sets the bit; tic1() puts both back and
/ returns the ticks counted.  The same TICK the four-language benchmark
/ of 2026-09-25 timed MACRO-11, DECUS C, PAS1 and FORTRAN with.

	.globl	_tic0, _tic1

	.text
_tic0:
	mov	*$0100, oldv
	mov	*$0102, oldv+2
	mov	$isr, *$0100
	mov	$0340, *$0102
	clr	ticks
	bis	$01000, *$0157700	/ the dispatcher's shadow
	mov	*$0157700, *$0177400	/ the dispatcher
	rts	pc

_tic1:
	bic	$01000, *$0157700
	mov	*$0157700, *$0177400
	mov	oldv, *$0100
	mov	oldv+2, *$0102
	mov	ticks, r0
	rts	pc

isr:
	inc	ticks
	rti

	.data
ticks:	.word	0
oldv:	.word	0, 0
