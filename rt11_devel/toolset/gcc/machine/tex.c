/* tex.c - textures and sprites drawn scaled (include/tex.h).
 *
 * A column of texels stretched over a run of screen rows: the texel's
 * row is the high byte of a sum that grows by the step - rows * 256 /
 * height in 8.8 - once a screen row.  The sum fits sixteen bits since
 * a texture has at most 255 rows.  The steps for the heights up to 255
 * come from a table made when the rows change (a wall's columns all
 * share their texture's rows); a taller column, nearer than the screen
 * is high, costs a division.  Where the run begins above the screen,
 * the sum starts at the step times the rows skipped. */

#include <ms0515.h>
#include <tex.h>

static int stepRows;			/* the rows the table is for; 0: none */
static unsigned steps[256];

unsigned tex_step(int rows, int height)
{
	int h;

	if (height <= 0)
		return 0;
	if (height > 255)
		return (unsigned)(((unsigned long)rows << 8) / (unsigned)height);
	if (rows != stepRows) {
		stepRows = rows;
		for (h = 1; h < 256; h++)
			steps[h] = (unsigned)(((unsigned long)rows << 8) / (unsigned)h);
	}
	return steps[height];
}

/* The run clipped to the screen: y0 and y1 moved in, the sum started
 * where the first row on the screen is; 0 when nothing is on it. */
static int clip(int *y0, int *y1, unsigned step, unsigned *acc)
{
	if (*y1 >= MS_ROWS)
		*y1 = MS_ROWS - 1;
	if (*y0 < 0) {
		*acc = (unsigned)((unsigned long)step * (unsigned)(-*y0));
		*y0 = 0;
	} else {
		*acc = 0;
	}
	return *y0 <= *y1;
}

void tex_column(const struct tex *t, int col, int c, int y0, int y1)
{
	unsigned step = tex_step(t->rows, y1 - y0 + 1);
	const unsigned char *px = t->pixels + col * t->rows;
	const unsigned char *at = t->attributes + col * t->rows;
	unsigned char *p;
	unsigned acc, i;

	if (!clip(&y0, &y1, step, &acc))
		return;
	p = ms_row(y0) + (c << 1);
	for (; y0 <= y1; y0++, p += MS_STRIDE) {
		i = acc >> 8;
		p[0] = px[i];
		p[1] = at[i];
		acc += step;
	}
}

void tex_sprite_column(const struct tex *t, int col, int c, int y0, int y1)
{
	unsigned step = tex_step(t->rows, y1 - y0 + 1);
	const unsigned char *px = t->pixels + col * t->rows;
	const unsigned char *at = t->attributes + col * t->rows;
	unsigned char *p;
	unsigned acc, i;

	if (!clip(&y0, &y1, step, &acc))
		return;
	p = ms_row(y0) + (c << 1);
	for (; y0 <= y1; y0++, p += MS_STRIDE) {
		i = acc >> 8;
		if (px[i]) {
			p[0] = px[i];
			p[1] = at[i];
		}
		acc += step;
	}
}

void tex_sprite(const struct tex *t, int c, int y, int cells, int height)
{
	unsigned step, acc = 0;
	int k;

	if (cells <= 0 || height <= 0)
		return;
	step = ((unsigned)t->cols << 8) / (unsigned)cells;	/* once a sprite: no table */
	for (k = 0; k < cells; k++, c++, acc += step)
		if (c >= 0 && c < MS_STRIDE / 2)
			tex_sprite_column(t, (int)(acc >> 8), c, y, y + height - 1);
}
