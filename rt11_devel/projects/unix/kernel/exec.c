/* exec.c - a program into the process's slot.
 *
 * A program is an a.out of the 0407 kind, as V6's: a header of eight words
 * (the magic, the text's bytes, the data's, the bss's, the symbols', the
 * entry, two unused) and the text and the data after it, linked for
 * 040000.  They go to 040000, the bss after them is cleared, and the stack
 * at the slot's top gets the arguments as V6 laid them: the strings at the
 * top, under them the pointers to them ending in 0, under those their count
 * - SP points at the count.  The arguments are copied out of the old image
 * first, which the new one overwrites.  The open files stay open, as Unix
 * has it.  An a.out of magic 0401 is a program that owns the machine: own.c
 * runs it over everything. */
#include "sys.h"
#include "proc.h"

#define AOUT_MAGIC 0407
#define OWN_MAGIC 0401
#define MAXARGS 16
#define STACK_MIN 512			/* room the program is left for its stack */

struct exec_header {
	unsigned magic, text, data, bss, syms, entry, unused, flag;
};

#define argbuf (HIGH->argbuf)

int exec(const char *path, const char *const *argv)
{
	struct exec_header h;
	struct inode *ip;
	unsigned argc, used = 0, len, size, sp, *vec, i;
	char *s;
	int error = 0;

	for (argc = 0; argv && argv[argc]; argc++) {	/* the strings, before the image goes */
		if (argc == MAXARGS)
			return E2BIG;
		for (len = 0; argv[argc][len]; len++)
			;
		if (used + len + 1 > ARGBYTES)
			return E2BIG;
		memcpy(argbuf + used, argv[argc], len + 1);
		used += len + 1;
	}
	ip = namei(path, &error);
	if (!ip)
		return error;
	if ((ip->d.mode & S_IFMT) != S_IFREG || !(ip->d.mode & S_X)) {
		iput(ip);
		return (ip->d.mode & S_IFMT) != S_IFREG ? EISDIR : EACCES;
	}
	if (readi(ip, 0, (char *)&h, sizeof h) != sizeof h ||
	    (h.magic != AOUT_MAGIC && h.magic != OWN_MAGIC)) {
		iput(ip);
		return ENOEXEC;
	}
	if (h.magic == OWN_MAGIC) {
		error = own_exec(ip, argc, used);
		iput(ip);
		return error;
	}
	size = h.text + h.data;
	if (size < h.text || size + h.bss < size ||
	    size + h.bss > USER_TOP - USER_BASE - STACK_MIN - used - 2 * (MAXARGS + 2) ||
	    h.entry < USER_BASE || h.entry >= USER_BASE + size) {
		iput(ip);
		return ENOMEM;
	}
	if (readi(ip, sizeof h, (char *)USER_BASE, size) != (int)size) {
		iput(ip);
		panic("exec: a program lost");	/* the old image is gone already */
	}
	iput(ip);
	memset((char *)USER_BASE + size, 0, h.bss);
	curproc->brk = USER_BASE + size + h.bss;

	sp = (USER_TOP - ((used + 1) & ~1u));		/* the strings, at the top */
	memcpy((char *)sp, argbuf, used);
	vec = (unsigned *)sp - (argc + 1);		/* the pointers, ending in 0 */
	for (s = (char *)sp, i = 0; i < argc; i++) {
		vec[i] = (unsigned)s;
		while (*s++)
			;
	}
	vec[argc] = 0;
	*--vec = argc;
	enter(h.entry, (unsigned)vec);
	return 0;
}
