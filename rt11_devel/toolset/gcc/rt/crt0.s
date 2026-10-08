/ crt0.s - where a GCC program on RT-11 starts and ends.
/
/ The monitor's RUN loads the .SAV and jumps to the start address in its
/ block 0 (aout2sav.py puts _start there) with the stack where the same
/ block says.  main() is called the way GCC calls any function - the
/ arguments on the stack, the result in r0 - and its return value is the
/ program's end: .EXIT (EMT 350) hands the machine back to KMON.
/
/ GCC's main() begins with a call to __main, which in libgcc runs the
/ global constructors and asks for atexit; a C program on this machine has
/ neither, so the one here does nothing.
/
/ GNU as syntax: $ immediate, *$ absolute (MACRO's @#), a leading 0 for
/ octal; every C symbol carries a leading underscore.

	.globl	_start, _main, ___main, _exit

	.text
_start:
	jsr	pc, _main
	/ falls into exit(main's result) - r0 is already the status
_exit:
	emt	0350			/ .EXIT: the status is not RT-11's business
___main:
	rts	pc
