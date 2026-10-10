/* fx.h - fixed-point arithmetic for a processor without multiply or
 * divide: what a 3D picture on the MS 0515 is made of.
 *
 * C's `*` and `/` on this machine are the runtime's general helpers:
 * sixteen steps a 16-bit division, more for a long.  Here are the
 * shapes a projection or a ray needs, cut to their size and written out
 * (rt/fx.s), and the tables a turn needs (rt/fx.c).  Fixed point is 8.8
 * where it is said: 256 is one. */

#ifndef FX_H
#define FX_H

/* num / den, truncated toward zero, for a quotient that fits eight
 * bits: |num| < 256 * den, den > 0.  Eight steps written out, half a
 * general division.  A projection: sx = 160 + fx_div8(x << 7, z) for a
 * point in view, since x * 128 < 160 * z there. */
int fx_div8(int num, int den);

/* a * b / 256: the product of two 8.8 numbers, or of a number and an
 * 8.8 factor, as 8.8 - the full 32-bit product shifted, so nothing is
 * lost on the way; the result must fit an int. */
int fx_mul(int a, int b);

/* The sine and the cosine of an angle in 256ths of a turn, as 8.8:
 * fx_sin(64) is 256.  A table of a quarter turn, 65 entries. */
int fx_sin(int angle);
int fx_cos(int angle);

#endif
