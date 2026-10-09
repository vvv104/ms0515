/ clock.s - the frame interrupt's handler: a frame counted.
/ Assembler for the RTI; clock.c takes the vector and says why.

	.globl	_ms_frame_isr, _ms_frames

	.text
_ms_frame_isr:
	inc	_ms_frames
	rti
