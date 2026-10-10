/* STREAM - a file read while the screen is on, the way a game streams
 * its world: the window closed round the requests, since the monitor
 * lies behind it, and opened again to draw what came.  A bar drawn
 * first; STREAM.DAT (the harness writes it) opened and its block 0 read;
 * a second bar as long as the block's first word says; then Q, the
 * screen off and the word printed. */

#include <ms0515.h>
#include <rt11.h>
#include <stdio.h>

static unsigned block[256];

int main(void)
{
	unsigned spec[4];
	int n = -1;

	ms_screen_begin(MS_INK(7));
	ms_fill(0, 10, 9, 12, 0xFF, MS_INK(7));

	ms_window(0);
	rt11_filespec(spec, "STREAM.DAT");
	if (rt11_lookup(1, spec) >= 0) {
		n = rt11_readw(1, 0, block, 256);
		rt11_close(1);
	}
	ms_window(1);

	if (n == 256 && block[0] > 0 && block[0] <= 40)
		ms_fill(0, 50, (int)block[0] - 1, 52, 0xFF, MS_INK(4));
	ms_keys_begin();
	while (ms_key() != MS_KEY_Q)
		;
	ms_keys_end();
	ms_screen_end();
	printf("STREAM %d %u\n", n, block[0]);
	return 0;
}
