/* PORTS - the joystick, the border, the speaker and the memory banks
 * put to the test.  The screen on with the border blue, the clock on;
 * for 80 frames every joystick line seen pressed is gathered (the
 * harness holds some for a while), the speaker flipped once at frame 20
 * (the harness watches its level); then a word written into bank 3's
 * primary half, another into its extended half, and both read back
 * with the bank switched each way.  The screen off, the lines printed:
 * "JOY n", "BANK x y". */

#include <ms0515.h>
#include <stdio.h>

int main(void)
{
	volatile unsigned *bank3 = (volatile unsigned *)0060000;	/* its first word */
	unsigned joy = 0, primary, extended;
	unsigned last;

	__asm__("" : "+r"(bank3));	/* a constant address is no array to GCC */
	ms_screen_begin(MS_INK(7));
	ms_border(1);
	ms_clock_begin();
	last = ms_frames;
	while (ms_frames < 80) {
		while (ms_frames == last)
			;
		last = ms_frames;
		joy |= ms_joystick();
		if (last == 20)
			ms_speaker_flip();
	}
	ms_clock_end();

	*bank3 = 0x1234;
	ms_bank(3, 1);
	*bank3 = 0x5678;
	extended = *bank3;
	ms_bank(3, 0);
	primary = *bank3;

	ms_screen_end();
	printf("JOY %u\n", joy);
	printf("BANK %x %x\n", primary, extended);
	return 0;
}
