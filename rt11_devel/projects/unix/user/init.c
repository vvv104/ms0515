/* init.c - the first process, stage 2: it asks for a name, greets it, and
 * then takes commands a line at a time: "t" prints the time since the
 * boot, anything else is answered with a question mark. */
#include "unistd.h"

static int length(const char *s)
{
	int n = 0;

	while (s[n])
		n++;
	return n;
}

static void say(const char *s)
{
	write(1, s, length(s));
}

static void number(unsigned n)
{
	char digits[6];
	int i = sizeof digits;

	do {
		digits[--i] = (char)('0' + n % 10);
		n /= 10;
	} while (n);
	write(1, digits + i, (int)sizeof digits - i);
}

/* A line read, its '\n' taken off. */
static void readline(char *buf, int size)
{
	int n = read(0, buf, size - 1);

	if (n < 0)
		n = 0;
	if (n > 0 && buf[n - 1] == '\n')
		n--;
	buf[n] = 0;
}

int main(void)
{
	char line[64];

	say("\n\nlogin: ");
	readline(line, sizeof line);
	say("Hello, ");
	say(line);
	say("\n");
	for (;;) {
		say("$ ");
		readline(line, sizeof line);
		if (line[0] == 0)
			continue;
		if (line[0] == 't' && line[1] == 0) {
			say("time ");
			number((unsigned)time());
			say("\n");
			continue;
		}
		say(line);
		say("?\n");
	}
}
