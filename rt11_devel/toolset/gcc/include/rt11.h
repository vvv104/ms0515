/* rt11.h - RT-11's programmed requests for a C program on the MS 0515.
 *
 * Each is a function in rt/emt.s with the request's own name: what it does
 * is the monitor's to say (the RT-11 Programmer's Reference Manual), what
 * the machine adds is docs/programming.md.  The C library (stdio and the
 * rest) is built over these; a program may use them directly. */

#ifndef RT11_H
#define RT11_H

/* .TTYOUT: the character out to the console, waiting for room in the
 * monitor's ring. */
void rt11_ttyout(int c);

/* .TTYIN: the next character typed, waited for; the low 7 bits. */
int rt11_ttyin(void);

/* .PRINT: the string out, up to a 0 - and a new line after it - or up to
 * a 0200 without one. */
void rt11_print(const char *s);

/* .SETTOP: the program's memory up to top, or as much as there is; the
 * new high limit comes back. */
void *rt11_settop(void *top);

/* .EXIT: back to the monitor.  exit() of the C library is this. */
void exit(int status) __attribute__((noreturn));

/* The first address above the program - its stack's top, where the
 * program started with SP - and so the heap's beginning: malloc takes
 * the memory from here up by .SETTOP. */
extern char *rt11_memtop;

#endif
