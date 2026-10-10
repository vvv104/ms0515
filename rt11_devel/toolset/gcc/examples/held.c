/* HELD - the keyboard's held keys put to the test: the clock and the
 * keyboard taken, forty frames in which whatever is typed is thrown away
 * by ms_keys_flush, then 160 frames of watching Left through ms_held -
 * a tick a frame, the codes wound in - and counting Right's codes.  The
 * frames at which Left's hold began and ended are printed, then Right's
 * count: "HELD t0 t1 t2 t3 / RIGHT n".  The harness taps the keys at
 * frames it knows. */

#include <ms0515.h>
#include <stdio.h>

int main(void)
{
	struct ms_held left = {MS_KEY_LEFT, 0};
	unsigned turns[8];
	int nturns = 0, right = 0, was = 0, c, i;
	unsigned last;

	ms_clock_begin();
	ms_keys_begin();
	ms_wait_frames(40);
	ms_keys_flush();
	last = ms_frames;
	while (ms_frames < 200) {
		while (ms_frames == last)
			;
		last = ms_frames;
		ms_held_tick(&left, 1);
		while ((c = ms_key()) >= 0) {
			if (c == MS_KEY_RIGHT)
				right++;
			ms_held_code(&left, 1, c);
		}
		if (ms_is_held(left) != was) {
			was = ms_is_held(left);
			if (nturns < 8)
				turns[nturns++] = last;
		}
	}
	ms_keys_end();
	ms_clock_end();
	printf("HELD");
	for (i = 0; i < nturns; i++)
		printf(" %u", turns[i]);
	printf(" / RIGHT %d\n", right);
	return 0;
}
