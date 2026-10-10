/* screen.c - the screen: register C, the window, the pixels.
 *
 * docs/programming.md, "The screen": the console leaves 640x200; a
 * colour program clears register C bit 3 and puts 010 back on exit, the
 * register is write-only so a shadow is kept, and the screen is cleared
 * once at the start since the console's text is still there.  "The
 * dispatcher, the VRAM window": the window at 0100000 hides RT-11, so no
 * monitor call while it is open. */

#include <ms0515.h>
#include "internal.h"

unsigned char ms_regc = 010;
unsigned ms_dispatcher = MS_DISPATCHER_ROM;

static const unsigned char bits[8] = {0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1};

void ms_dispatcher_set(unsigned b)
{
	ms_dispatcher |= b;
	*MS_DISPATCHER = ms_dispatcher;
}

void ms_dispatcher_clear(unsigned b)
{
	ms_dispatcher &= ~b;
	*MS_DISPATCHER = ms_dispatcher;
}

void ms_screen_begin(unsigned char attribute)
{
	ms_regc = 040;			/* 320x200 colour, black border, speaker low */
	*MS_REGISTER_C = ms_regc;
	ms_dispatcher_set(MS_DISPATCHER_VRAM);
	ms_screen_clear(attribute);
}

void ms_screen_end(void)
{
	/* In 640x200 both bytes of a word are pixels: the attributes left
	 * behind would show as stripes, the stars as specks. */
	ms_screen_clear(0);
	ms_dispatcher_clear(MS_DISPATCHER_VRAM);
	ms_regc = 010;			/* the console's 640x200, black border */
	*MS_REGISTER_C = ms_regc;
}

void ms_window(int open)
{
	if (open)
		ms_dispatcher_set(MS_DISPATCHER_VRAM);
	else
		ms_dispatcher_clear(MS_DISPATCHER_VRAM);
}

void ms_border(unsigned char colour)
{
	ms_regc = (unsigned char)((ms_regc & ~7) | (colour & 7));
	*MS_REGISTER_C = ms_regc;
}

void ms_screen_clear(unsigned char attribute)
{
	unsigned *w = (unsigned *)MS_VRAM;
	unsigned word = (unsigned)attribute << 8;
	int n;

	for (n = MS_ROWS * MS_STRIDE / 2; n > 0; n--)
		*w++ = word;
}

void ms_plot(int x, int y)
{
	ms_row(y)[(x >> 3) << 1] |= bits[x & 7];
}

void ms_unplot(int x, int y)
{
	ms_row(y)[(x >> 3) << 1] &= (unsigned char)~bits[x & 7];
}

void ms_attribute(int c, int y, unsigned char attribute)
{
	ms_row(y)[(c << 1) + 1] = attribute;
}
