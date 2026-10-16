/ tail.s - the end of /boot/hi's image (head.s): linked last, its label
/ after all the text and the data.  The bss after it the kernel clears.

	.data
	.globl	_hi_end
_hi_end:
