/ head.s - /boot/hi's header (kernel/hi.h), the first words of its image:
/ the magic, the version, its size, the key of the kernel it was linked
/ against, the entries.  tail.s ends the image.

	.text
	.globl	_own_exec, _sys_ioctl, _sys_mount, _sys_umount, _kernel_key, _hi_end

HI_BASE = 0151600			/ where it is linked (kernel/hi.h)

start:	.word	044111			/ "IH"
	.word	1			/ the version
	.word	_hi_end - HI_BASE		/ the image's bytes
	.word	_kernel_key		/ the kernel's: its address
	.word	_own_exec
	.word	_sys_ioctl
	.word	_sys_mount
	.word	_sys_umount
