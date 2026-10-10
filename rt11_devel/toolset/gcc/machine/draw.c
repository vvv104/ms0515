/* draw.c - columns, boxes, images and text on the screen.
 *
 * The screen as ms0515.h describes it: 200 rows of 40 words, a word the
 * attribute byte over the pixel byte, bit 7 the leftmost of a cell's
 * eight pixels.  A column of cells filled word by word down the rows is
 * what a raycaster draws; a box is a HUD's ground; an image is a sprite
 * or a picture, its bytes row by row; text is the ROM's own font.
 *
 * The font: ROM-A and ROM-B keep the same glyphs at different places in
 * the half of the ROM a program sees (0160000..0177377), eight bytes a
 * glyph, bit 7 leftmost - the console draws its text from them.  The
 * main table holds KOI-8 040..0177 in order, the second the Cyrillic
 * 0300..0377; each is found the first time by the shape of one glyph,
 * as the emulator's own terminal decoder finds them, so the program
 * does not care which ROM it runs on.  The pseudographics 0200..0277
 * are in the other half of ROM-B, out of reach: no glyph. */

#include <ms0515.h>

#define ROM_FROM ((const unsigned char *)0160000)
#define ROM_TO   ((const unsigned char *)0177400)

static const unsigned char *mainFont;	/* the glyph of 040 */
static const unsigned char *altFont;	/* the glyph of 0300 */
static int fontLooked;

static const unsigned char zero[8] = {0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00};	/* '0', main index 16 */
static const unsigned char cyrA[8] = {0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00};	/* 'A' of 0341, alt index 33 */

/* Where the eight bytes of `shape` lie in the ROM, or 0. */
static const unsigned char *shapeAt(const unsigned char *shape)
{
	const unsigned char *at;
	int i;

	for (at = ROM_FROM; at < ROM_TO - 8; at++) {
		for (i = 0; i < 8 && at[i] == shape[i]; i++)
			;
		if (i == 8)
			return at;
	}
	return 0;
}

static void findFont(void)
{
	const unsigned char *at;

	fontLooked = 1;
	at = shapeAt(zero);
	mainFont = at ? at - 16 * 8 : 0;
	at = shapeAt(cyrA);
	altFont = at ? at - 33 * 8 : 0;
}

const unsigned char *ms_glyph(int koi8)
{
	int c = koi8 & 0377;

	if (!fontLooked)
		findFont();
	if (c >= 040 && c < 0200)
		return mainFont ? mainFont + (c - 040) * 8 : 0;
	if (c >= 0300)
		return altFont ? altFont + (c - 0300) * 8 : 0;
	return 0;
}

void ms_vfill(int c, int y0, int y1, unsigned char pixels, unsigned char attribute)
{
	unsigned char *p = ms_row(y0) + (c << 1);
	unsigned word = ((unsigned)attribute << 8) | pixels;

	for (; y0 <= y1; y0++, p += MS_STRIDE)
		*(unsigned *)p = word;
}

void ms_fill(int c0, int y0, int c1, int y1, unsigned char pixels, unsigned char attribute)
{
	unsigned char *row = ms_row(y0) + (c0 << 1);
	unsigned word = ((unsigned)attribute << 8) | pixels;
	int n = c1 - c0 + 1;
	unsigned *p;
	int i;

	for (; y0 <= y1; y0++, row += MS_STRIDE)
		for (p = (unsigned *)row, i = n; i > 0; i--)
			*p++ = word;
}

void ms_blit(const struct ms_image *image, int c, int y)
{
	unsigned char *row = ms_row(y) + (c << 1);
	const unsigned char *pixels = image->pixels;
	const unsigned char *attributes = image->attributes;
	int r, i;
	unsigned char *p;

	for (r = image->rows; r > 0; r--, row += MS_STRIDE) {
		p = row;
		for (i = image->cells; i > 0; i--, p += 2) {
			p[0] = *pixels++;
			if (attributes)
				p[1] = *attributes++;
		}
	}
}

void ms_text(int c, int y, const char *s, unsigned char attribute)
{
	unsigned char *cell = ms_row(y) + (c << 1);
	const unsigned char *glyph;
	unsigned char *p;
	int r;

	for (; *s; s++, cell += 2) {
		glyph = ms_glyph((unsigned char)*s);
		for (r = 0, p = cell; r < 8; r++, p += MS_STRIDE) {
			p[0] = glyph ? glyph[r] : 0;
			p[1] = attribute;
		}
	}
}
