/* own.c - a program that owns the machine (SPEC.md stage 7).
 *
 * An a.out of magic 0401, linked at 001000 as an RT-11 program is; it
 * brings its own loader at the start of its text (user/own/crt0.s).  exec
 * of one writes the cache out, reads the file's first block - the header
 * and the loader - into the process's slot at 040000 and enters the
 * loader at 040020, SP pointing at what it needs to find the rest: the
 * ROM's unit, whether the volume is both sides of the drive (a block from
 * 800 on side 1), the file's five extents - and the command line, the
 * count and the strings, OWN_ARGS bytes at most.  The loader takes the
 * blocks to read from the header and lays the file over everything
 * through the ROM's 160010, the kernel too, and the system is gone; so the
 * file is to be on a diskette.  Whatever can be the program's is: the
 * sizes are the loader's to check - a reset if they do not fit - and
 * tools/own_aout.py's when the program is built; the kernel only checks
 * the device and the command line and hands over. */
#include "../hi.h"

#define HEADER 16
#define OWN_ARGS 128			/* what the loader's room takes of exec's strings */

/* ip's program, argc arguments in exec's buffer, used bytes: back only
 * with the error that kept it from being run. */
int own_exec(struct inode *ip, unsigned argc, unsigned used)
{
	unsigned dev = ip->dev, *info;

	if (dev > DEV_FDX1)
		return ENOEXEC;			/* the ROM reads diskettes only */
	if (used > OWN_ARGS)
		return E2BIG;
	if (readi(ip, 0, (char *)USER_BASE, BSIZE) <= HEADER)
		return ENOEXEC;
	bflush();
	info = (unsigned *)(USER_BASE + BSIZE);
	info[0] = dev >= DEV_FDX0 ? dev - DEV_FDX0 : (dev & 1) << 1 | dev >> 1;
	info[1] = dev >= DEV_FDX0;
	memcpy(info + 2, ip->d.ext, sizeof ip->d.ext);
	info[12] = argc;
	memcpy(info + 13, HIGH->argbuf, used);
	enter(USER_BASE + HEADER, (unsigned)info);
	return 0;				/* not come to */
}
