/ sys.s - the system calls as C functions (unistd.h): TRAP n with the
/ arguments left on the stack where the caller put them.  An error comes
/ back with C set and its number in r0: errno takes it and the call
/ returns -1.

	.text
	.globl	_exit, _read, _write, _open, _close, _execv, _time, _errno

_exit:	trap	1
	br	_exit

_read:	trap	3
	br	done

_write:	trap	4
	br	done

_open:	trap	5
	br	done

_close:	trap	6
	br	done

/ int execv(const char *path, char *const argv[]): back only on an error.
_execv:	trap	11
	br	done

/ long time(void): the seconds since the boot, r0:r1.
_time:	trap	13
	br	done

done:	bcc	1f
	mov	r0, _errno
	mov	$-1, r0
1:	rts	pc

	.bss
_errno:	.=.+2
