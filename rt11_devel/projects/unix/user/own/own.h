/* own.h - the little library of a program that owns the machine (SPEC.md
 * stage 7, kernel/own.c): the ROM's console, keyboard and diskette, and
 * the end.
 *
 * Such a program is an a.out of magic 0401 linked at 001000, built by the
 * project's own_program(); it has 001000-157000, SP starting at 157000,
 * every bank primary and the extra copies by the dispatcher.  crt0 points
 * every vector at an RTI and runs main at priority 7; whatever main
 * returns, the end is own_exit. */
#ifndef UNIX_OWN_H
#define UNIX_OWN_H

/* A character out through the ROM's console (VT52 commands, KOI-8). */
void own_putc(int c);
void own_puts(const char *s);
void own_putoct(unsigned n);		/* in octal, no leading zeros */

/* The next key's byte, -1 when there is none. */
int own_getc(void);

/* Block `block` (0..799) of the ROM's unit - a drive's side, side * 2 +
 * drive - read into buf (512 bytes), or written from it; 0 or -1. */
int own_block(unsigned unit, unsigned block, void *buf, int write);

/* The end: the ROM's reset, which boots the diskette again. */
void own_exit(void) __attribute__((noreturn));

#endif
