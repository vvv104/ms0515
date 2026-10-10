/* files.c - RT-11's files, a block at a time.
 *
 * docs/programming.md, "Memory and the monitor": .LOOKUP needs the USR,
 * so a file is opened once at the start; .READW does not, so a program
 * reads its data a piece at a time during play.  The requests are EMT
 * 375 with a block of words the monitor reads (the RT-11 Programmer's
 * Reference Manual): the channel in the low byte of the first word and
 * the request's code in the high, then its arguments; .WAIT and .CLOSE
 * are EMT 374 with the code and the channel in r0.  A file name goes
 * to the monitor in RAD50: three characters a word, the device, two
 * words of name, the type.
 *
 * A request reaches the monitor, which lies behind the VRAM window: a
 * program that has the window open (ms0515.h) closes it round the
 * request.  The non-wait .READ is finished by the disk's handler on its
 * interrupt while the program goes on; .WAIT then says how it went. */

#include <rt11.h>

#define LOOKUP 1
#define ENTER 2
#define READ 010
#define WRITE 011
#define WAIT 0
#define CLOSE 6

/* A character's RAD50 code: space, A-Z, $, ., (unused), 0-9. */
static unsigned rad50(int c)
{
	if (c >= 'a' && c <= 'z')
		c -= 'a' - 'A';
	if (c >= 'A' && c <= 'Z')
		return (unsigned)(c - 'A' + 1);
	if (c >= '0' && c <= '9')
		return (unsigned)(c - '0' + 30);
	if (c == '$')
		return 27;
	if (c == '.')
		return 28;
	return 0;
}

/* Three characters of s, or fewer up to `stop`, as one RAD50 word;
 * s moves past them. */
static unsigned word(const char **s, const char *stop)
{
	unsigned w = 0;
	int n;

	for (n = 0; n < 3; n++) {
		w = w * 40;
		if (*s < stop) {
			w += rad50(**s);
			(*s)++;
		}
	}
	return w;
}

int rt11_filespec(unsigned spec[4], const char *name)
{
	const char *colon = name, *dot, *end;

	while (*colon && *colon != ':')
		colon++;
	if (*colon == ':') {
		spec[0] = word(&name, colon);
		name = colon + 1;
	} else {
		const char *dk = "DK";
		spec[0] = word(&dk, dk + 2);
	}
	dot = name;
	while (*dot && *dot != '.')
		dot++;
	spec[1] = word(&name, dot);
	spec[2] = word(&name, dot);
	if (*dot == '.') {
		dot++;
		end = dot;
		while (*end)
			end++;
		spec[3] = word(&dot, end);
	} else {
		spec[3] = 0;
	}
	return 0;
}

int rt11_lookup(int channel, const unsigned spec[4])
{
	unsigned area[3];

	area[0] = (unsigned)channel | (LOOKUP << 8);
	area[1] = (unsigned)spec;
	area[2] = 0;
	return rt11_request(area);
}

int rt11_enter(int channel, const unsigned spec[4], int blocks)
{
	unsigned area[4];

	area[0] = (unsigned)channel | (ENTER << 8);
	area[1] = (unsigned)spec;
	area[2] = (unsigned)blocks;
	area[3] = 0;
	return rt11_request(area);
}

static int transfer(int code, int channel, unsigned block, void *buffer, unsigned words, int nowait)
{
	unsigned area[5];

	area[0] = (unsigned)channel | (unsigned)(code << 8);
	area[1] = block;
	area[2] = (unsigned)buffer;
	area[3] = words;
	area[4] = (unsigned)nowait;
	return rt11_request(area);
}

int rt11_readw(int channel, unsigned block, void *buffer, unsigned words)
{
	return transfer(READ, channel, block, buffer, words, 0);
}

int rt11_read(int channel, unsigned block, void *buffer, unsigned words)
{
	return transfer(READ, channel, block, buffer, words, 1);
}

int rt11_writew(int channel, unsigned block, const void *buffer, unsigned words)
{
	return transfer(WRITE, channel, block, (void *)buffer, words, 0);
}

int rt11_wait(int channel)
{
	return rt11_request374((unsigned)channel | (WAIT << 8));
}

int rt11_close(int channel)
{
	return rt11_request374((unsigned)channel | (CLOSE << 8));
}

int rt11_error(void)
{
	volatile unsigned char *code = (volatile unsigned char *)052;

	__asm__("" : "+r"(code));	/* GCC takes a constant address for a thing of no size */
	return *code;
}
