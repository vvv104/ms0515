/ sys.s - the system calls as C functions (unistd.h): TRAP n with the
/ arguments left on the stack where the caller put them.  An error comes
/ back with C set and its number in r0: errno takes it and the call
/ returns -1.

	.text
	.globl	_exit, _fork, _read, _write, _open, _close, _wait, _execv
	.globl	_time, _getpid, _getppid, _dup, _creat, _unlink, _mkdir, _rmdir, _sync
	.globl	_mount, _umount, _ioctl, _lseek, _chdir, _stat, _fstat, _link, _chmod
	.globl	_errno

_exit:	trap	1
	br	_exit

/ int fork(void): the child's pid to the parent, 0 to the child.
_fork:	trap	2
	br	done

_read:	trap	3
	br	done

_write:	trap	4
	br	done

_open:	trap	5
	br	done

_close:	trap	6
	br	done

/ int wait(int *status): a child gone, its pid; its status at *status.
_wait:	trap	7
	br	done

/ int execv(const char *path, char *const argv[]): back only on an error.
_execv:	trap	11
	br	done

/ long time(void): the seconds since the boot, r0:r1.
_time:	trap	13
	br	done

_getpid:
	trap	20
	br	done

_getppid:
	trap	64
	br	done

/ int dup(int fd): another descriptor of the same open file, the lowest free.
_dup:	trap	41
	br	done

/ int creat(const char *path): the file made or emptied, open to write, rw-.
_creat:	trap	8
	br	done

_unlink:
	trap	10
	br	done

_mkdir:	trap	136
	br	done

_rmdir:	trap	137
	br	done

_sync:	trap	36
	br	done

/ int mount(const char *device, const char *dir), int umount(const char *device)
_mount:	trap	21
	br	done

_umount:
	trap	22
	br	done

/ int chdir(const char *dir), stat(const char *path, struct stat *st),
/ fstat(int fd, struct stat *st) (kernel/stat.h)
_chdir:	trap	12
	br	done

/ int chmod(const char *path, int rights): the file's one rwx.
_chmod:	trap	15
	br	done

/ int link(const char *old, const char *new): the file named new too.
_link:	trap	9
	br	done

_stat:	trap	18
	br	done

_fstat:	trap	28
	br	done

/ long lseek(int fd, long off, int whence): the new offset, r0:r1.
_lseek:	trap	19
	br	done

/ int ioctl(int fd, int cmd, void *arg): kernel/ioctl.h says what for.
_ioctl:	trap	54
	br	done

done:	bcc	1f
	mov	r0, _errno
	mov	$-1, r0
1:	rts	pc

	.bss
_errno:	.=.+2
