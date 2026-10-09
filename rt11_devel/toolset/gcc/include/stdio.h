/* stdio.h - the console as C's standard output and input.
 *
 * The console is RT-11's: a character out is .TTYOUT, one in is .TTYIN,
 * which hands over a line once it is typed, the monitor's own editing
 * done.  '\n' goes out as CR LF and comes in as the LF after the CR, so
 * a program writes and reads text the C way.  Files are not here yet. */

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)

int putchar(int c);
int getchar(void);
int puts(const char *s);

/* The conversions: d i u x X o c s %, with -, 0, a width, a precision for
 * s, and l for a long.  No floating point: the processor has none. */
int printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
int vprintf(const char *format, va_list args);
int sprintf(char *buffer, const char *format, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *buffer, size_t size, const char *format, ...)
	__attribute__((format(printf, 3, 4)));
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);

#endif
