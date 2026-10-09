/* MACHINE - the machine library (ms0515.h) put to the test: the screen
 * in colour with a diagonal of pixels and one cell's attribute changed,
 * the clock counting while the program waits, the keyboard taken over
 * until Q, the speaker clicked once; then everything given back and a
 * line printed for the harness (../tests): "MACHINE: F frames, K codes",
 * F at least the frames waited, K the codes that came. */

#include <ms0515.h>
#include <stdio.h>

int main(void)
{
	int x, codes = 0, c;

	ms_screen_begin(MS_INK(7) | MS_PAPER(0));
	ms_border(1);
	for (x = 0; x < MS_COLUMNS; x++)
		ms_plot(x, x * 5 / 8);		/* corner to corner */
	ms_unplot(0, 0);
	ms_attribute(2, 3, MS_INK(2) | MS_BRIGHT);
	ms_clock_begin();
	ms_keys_begin();
	ms_speaker_flip();
	ms_wait_frames(25);
	for (;;) {
		c = ms_key();
		if (c < 0)
			continue;
		codes++;
		if (c == MS_KEY_Q)
			break;
	}
	ms_keys_end();
	ms_clock_end();
	ms_screen_end();
	printf("MACHINE: %u frames, %d codes\n", ms_frames, codes);
	return 0;
}
