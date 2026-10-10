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
	.globl	_rt11_request, _rt11_request374

	.text

/ int rt11_request(void *area): EMT 375 with the request's block in r0 -
/ .LOOKUP, .ENTER, .READW and the rest (rt/files.c builds the blocks).
/ What the monitor leaves in r0 comes back, or -1 when it set the carry:
/ an error, its code in byte 052.
_rt11_request:
	mov	2(sp), r0
	emt	0375
	bcs	1f
	rts	pc
1:	mov	$-1, r0
	rts	pc

/ int rt11_request374(unsigned code_and_channel): EMT 374 with r0 = the
/ request's code in the high byte, the channel in the low - .WAIT (0),
/ .CLOSE (6); 0 back, or -1 on the carry.
_rt11_request374:
	mov	2(sp), r0
	emt	0374
	bcs	1f
	clr	r0
	rts	pc
1:	mov	$-1, r0
	rts	pc

/ void rt11_ttyout(int c): .TTYOUT - the character out, waiting for room.
/ The request itself is .TTOUTR, which comes back with the carry set when
/ the ring is full; the waiting is the macro's loop, and so ours.
_rt11_ttyout:
	mov	2(sp), r0
1:	emt	0341
	bcs	1b
	rts	pc

/ int rt11_ttyin(void): .TTYIN - the next character typed, waited for.
/ The request is .TTINR, which comes back with the carry set and r0 as
/ it was when nothing has been typed; the waiting is the macro's loop.
_rt11_ttyin:
1:	emt	0340
	bcs	1b
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
