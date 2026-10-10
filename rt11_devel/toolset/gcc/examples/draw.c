/* DRAW - the drawing of ms0515.h put to the test: a column, a box, an
 * image and two lines of text - ASCII and Cyrillic in the ROM's font -
 * on a screen the harness (../tests) reads cell by cell, then Q. */

#include <ms0515.h>

static const unsigned char pixels[6] = {0x81, 0x18, 0xFF, 0x00, 0x3C, 0xC3};
static const unsigned char attributes[6] = {1, 2, 3, 4, 5, 6};
static const struct ms_image image = {2, 3, pixels, attributes};
static const struct ms_image bare = {2, 3, pixels, 0};

int main(void)
{
	ms_screen_begin(MS_INK(7));
	ms_vfill(5, 10, 20, 0xFF, MS_INK(2));
	ms_fill(10, 30, 12, 32, 0xAA, MS_PAPER(1) | MS_BRIGHT);
	ms_blit(&image, 20, 50);
	ms_blit(&bare, 24, 50);				/* the attributes left as they are */
	ms_text(0, 100, "Hi!", MS_INK(6));
	ms_text(10, 100, "\304\301 \001", MS_INK(5));	/* KOI-8 "Da", then a code with no glyph */
	ms_keys_begin();
	while (ms_key() != MS_KEY_Q)
		;
	ms_keys_end();
	ms_screen_end();
	return 0;
}
