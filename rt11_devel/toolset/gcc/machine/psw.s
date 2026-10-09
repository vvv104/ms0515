/ psw.s - the processor's priority, for the few instructions an
/ interrupt must not split.  MFPS and MTPS are the T-11's; the priority
/ is bits 5-7 of the PSW, 0340 the top.

	.globl	_ms_interrupts_off, _ms_interrupts_restore

	.text

/ unsigned ms_interrupts_off(void): the PSW before, back in r0
_ms_interrupts_off:
	mfps	r0
	bic	$0177400, r0
	mtps	$0340
	rts	pc

/ void ms_interrupts_restore(unsigned psw)
_ms_interrupts_restore:
	mtps	2(sp)
	rts	pc
