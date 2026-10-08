/* console.c - the console as stdout and stdin.
 *
 * Text the C way on RT-11's console: '\n' out is CR LF, since the
 * monitor moves the cursor down for an LF and to the left for a CR and
 * does neither for the other; a line in ends in CR LF from .TTYIN, and
 * the CR is dropped so that the program sees the '\n' alone. */

#include <rt11.h>
#include <stdio.h>

int putchar(int c)
{
	if (c == '\n')
		rt11_ttyout('\r');
	rt11_ttyout(c);
	return (unsigned char)c;
}

int getchar(void)
{
	int c;

	do
		c = rt11_ttyin();
	while (c == '\r');
	return c;
}

int puts(const char *s)
{
	while (*s)
		putchar(*s++);
	putchar('\n');
	return 0;
}
