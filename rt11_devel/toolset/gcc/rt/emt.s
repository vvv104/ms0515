/ emt.s - RT-11's programmed requests as C functions (include/rt11.h).
/
/ A programmed request is an EMT with its arguments in r0, or in a block
/ r0 points at, and the answer in r0 and the carry bit.  Here each is a
/ function with GCC's convention - the arguments on the stack above the
/ return address, the result in r0, r0 and r1 free - so that a C program
/ asks the monitor the way a MACRO-11 one does, by its own name for the
/ request.  What each one does is the RT-11 Programmer's Reference Manual's
/ to say; what the machine adds is docs/programming.md.

	.globl	_rt11_ttyout, _rt11_ttyin, _rt11_print, _rt11_settop

	.text

/ void rt11_ttyout(int c): .TTYOUT - the character out, waiting for room.
_rt11_ttyout:
	mov	2(sp), r0
	emt	0341
	rts	pc

/ int rt11_ttyin(void): .TTYIN - the next character typed, waited for.
_rt11_ttyin:
	emt	0340
	bic	$0177400, r0
	rts	pc

/ void rt11_print(const char *s): .PRINT - the string up to a 0, with a
/ new line after it, or up to a 0200 without one.
_rt11_print:
	mov	2(sp), r0
	emt	0351
	rts	pc

/ void *rt11_settop(void *top): .SETTOP - the program's memory up to top
/ (as much as there is, when less); the new high limit comes back.
_rt11_settop:
	mov	2(sp), r0
	emt	0354
	rts	pc
