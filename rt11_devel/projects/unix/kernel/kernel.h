/* kernel.h - what the parts of the kernel see of one another. */
#ifndef UNIX_KERNEL_H
#define UNIX_KERNEL_H

/* The machine's write-only registers and the ROM's copies of them. */
#define DISPATCHER (*(volatile unsigned *)0177400)
#define DISP_COPY  (*(volatile unsigned *)0157700)	/* the ROM's copy */
#define DISP_TIMER 01000				/* bit 9: the frame interrupt */
#define REG_B      (*(volatile unsigned *)0177602)
#define KBD_ROWS   (*(volatile unsigned char *)0177540)	/* the MS 7007's rows */

/* The process's space: slot A, banks 2-5. */
#define USER_BASE 040000u
#define USER_TOP  0140000u

/* trap.s */
struct trapframe {
	unsigned r0, r1, r2, r3, r4, r5, pc, ps;
};
void trap_entry(void), clock_entry(void), tty_entry(void);
int spl7(void);
void splx(int ps);
void idle(void);
void enter(unsigned pc, unsigned sp);

/* clock.c */
void clock_start(void);
unsigned long clock_seconds(void);

/* tty.c */
int tty_read(char *buf, int n);
int tty_write(const char *buf, int n);
void tty_puts(const char *s);

/* sys.c */
void syscall(struct trapframe *tf);

#endif
