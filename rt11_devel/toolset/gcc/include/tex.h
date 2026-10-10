/* tex.h - textures and sprites drawn scaled: what a 3D view is painted
 * with (machine/tex.c).
 *
 * In a view cast by the grid, a wall never turns as a picture would:
 * the ray that hit it says which column of its texture is seen, and
 * the distance how tall the column stands on the screen.  So a texture
 * is drawn a column at a time, each stretched or squeezed to its
 * height - a texel every 256/height rows in 8.8, the texel's row the
 * high byte of a sum, no division in the row - and a sprite is the
 * same in both directions, with its empty texels left unwritten.
 *
 * A texture lies column by column, so that a column is a run of bytes:
 * pixels[cols][rows] the patterns of eight pixels, attributes[cols][rows]
 * the ink and paper of each; the two planes apart.  Rows up to 255. */

#ifndef TEX_H
#define TEX_H

struct tex {
	unsigned char cols, rows;
	const unsigned char *pixels;		/* cols * rows, column-major */
	const unsigned char *attributes;	/* the same way */
};

/* Column `col` of the texture on screen column c, stretched over rows
 * y0..y1 (both in): y0 may lie above the screen and y1 below it - the
 * part off the screen is skipped, the stretch kept - as a wall nearer
 * than the screen is tall does. */
void tex_column(const struct tex *t, int col, int c, int y0, int y1);

/* The same with the empty texels - a pattern of 0 - left unwritten: a
 * sprite's column, for a program that draws its sprites a column at a
 * time against the depth of the wall there. */
void tex_sprite_column(const struct tex *t, int col, int c, int y0, int y1);

/* The texture as a sprite: `cells` wide and `height` rows high on the
 * screen, its top left at cell column c and row y, its empty texels
 * left unwritten; what lies off the screen is skipped. */
void tex_sprite(const struct tex *t, int c, int y, int cells, int height);

/* The texel step in 8.8 for `rows` texels over `height` rows: a table
 * for the heights up to 255, remade when `rows` changes, a division
 * past that. */
unsigned tex_step(int rows, int height);

#endif
