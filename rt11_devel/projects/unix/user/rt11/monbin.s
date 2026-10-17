/ monbin.s - the little RT-11 monitor's flat image (mon.c, mon.s, linked
/ for 140000) carried by /bin/rt11, which copies it there.

	.data
	.globl	_mon_image, _mon_end
	.even
_mon_image:
	.incbin	"rt11mon.bin"
	.even
_mon_end:
