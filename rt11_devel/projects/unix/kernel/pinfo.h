/* pinfo.h - what getproc(n, &info) tells of the process table's nth place,
 * for ps: the head of the kernel's struct proc (proc.h) as it is, copied
 * out whole - the kernel checks at its build that the two agree. */
#ifndef UNIX_PINFO_H
#define UNIX_PINFO_H

#define PNAME 8				/* a process's name: argv[0]'s last part, its NUL too */
#define NOFILE 8			/* a process's open files */

/* state: 0 free, 1 running, 2 asleep, 3 gone and not waited for. */
struct pinfo {
	unsigned ksp;
	int state;
	int pid, ppid;
	int slot;			/* 0 or 1, -1 parked */
	const void *chan;
	int status;
	unsigned brk;
	void *tf;
	void *ofile[NOFILE];
	void *cwd;
	char killed;			/* Ctrl+C or kill: to exit */
	char bg;			/* put in the background (TTY_BG) */
	char name[PNAME];
};

#endif
