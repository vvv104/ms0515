/ keys.s - the keyboard interrupt's handler: the byte into the ring.
/ Assembler for the RTI; keys.c takes the vector and says why.  Reading
/ the USART's data register (177440) takes the interrupt's request away.

	.globl	_ms_key_isr, _ms_keyring, _ms_keyhead

	.text
_ms_key_isr:
	mov	r0, -(sp)
	mov	r1, -(sp)
	mov	_ms_keyhead, r1
	movb	*$0177440, _ms_keyring(r1)
	inc	r1
	bic	$0177760, r1		/ sixteen bytes round
	mov	r1, _ms_keyhead
	mov	(sp)+, r1
	mov	(sp)+, r0
	rti
