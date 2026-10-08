/* STARS - a flight through a starfield, seen from the cockpit.
 *
 * The stars live in a cone ahead of the ship: x to the right, y up, z
 * forward, the ship at the origin looking down z, and |x| and |y| up to
 * 15/8 of z - half as wide again as the screen sees, so that a turn
 * finds stars waiting past its edges.  Every pass of the main loop,
 * two frames, every star comes nearer by the ship's speed, and the
 * picture is the projection of the ones in view - sx = 160 + x*128/z,
 * sy = 100 - y*128/z less a sixth for the CRT's pixels, which are
 * taller than wide, a field of view of some 100 degrees - so a star
 * born on the far wall slides outward, the faster the nearer.  A star
 * that passes the ship or flies out of the cone is born again on the
 * far wall.
 *
 * The keys turn the ship, which turns the cone of stars the other way
 * round the ship: Down pulls the nose up (the stars fall), Up pushes it
 * down; Left rolls the ship counter-clockwise (the stars turn
 * clockwise), Right clockwise.  A turn is a 16th of a radian a pass,
 * by shifts rounded to the nearest; the cosine is taken as one, which
 * lets a star grow a fifth of a percent a pass and nobody notice.  A
 * star a turn takes out of the cone comes in again at the opposite
 * edge, as a star of the sky beyond would: the field stays even
 * however the ship turns, and the stars out of view, which nobody
 * sees move, turn every other pass by twice the angle.  A star turned
 * past the far wall is born there anew.  Q ends the flight.
 *
 * The keyboard sends a key's code when it is pressed and again while it
 * is held, never a release (docs/programming.md): a held key is a timer
 * refreshed by its code - nine frames on the first code, which has to
 * outlast the keyboard's silence before the repeats, four after a
 * repeat, which come every two or three.
 *
 * The frame is 20 ms and the processor does an instruction in some three
 * microseconds, with no multiply or divide, so the arithmetic is kept
 * off the frame.  While no key is held a star's x and y stand still and
 * only z falls, so the depth at which it goes out of view and the depth
 * at which it leaves the cone are worked out once, when it is born or
 * turned, and a frame compares z with them.  The projection divides by
 * an eight-step division of its own (proj.s), since a star in view
 * projects within 160 pixels of the centre; the row's address, the
 * pixel's bit and the CRT's correction come from tables, and a star
 * remembers the byte and the bit it lit.  Everything is 16-bit: z runs
 * 8..200, so x*128 of a star in view stays under 160*z, within an int.
 *
 * The machine's part - the window on video memory, the frame counter,
 * the keyboard's ring - is machine.s. */

#include <stdio.h>
#include <stdlib.h>

void hw_begin(void);
void hw_end(void);
extern volatile unsigned frames;
extern volatile unsigned char kbring[16];
extern volatile unsigned kbhead;

#ifndef NSTARS
#define NSTARS 110
#endif
#define ZFAR 200
#define ZNEAR 8
#ifndef PACE
#define PACE 2			/* frames a pass takes: 25 a second */
#endif
#define SPEED 2			/* z a pass */
#define ROWS 200
#define COLUMNS 320
#define STRIDE 80

#define KEY_UP 0252
#define KEY_DOWN 0251
#define KEY_LEFT 0247
#define KEY_RIGHT 0250
#define KEY_Q 0303
#define HOLD_FIRST 9
#define HOLD_NEXT 4

enum { kUp, kDown, kLeft, kRight, kKeys };

struct star {
	int x, y, z;
	int zseen;		/* in view while z is above this */
	int zgone;		/* out of the cone once z falls under this */
	unsigned char *p;	/* the byte and the bit it lit; p == 0: none */
	unsigned char bit;
	unsigned char stale;	/* turned: zseen and zgone are to be settled */
};

/* The star put on the screen, in draw.s: its projection, and its pixel
 * lit by the tables below, which it reads. */
void draw(struct star *s);

static struct star stars[NSTARS];
static int hold[kKeys];
static unsigned kbtail;
static unsigned char *const vram = (unsigned char *)0100000;
unsigned char *rowp[ROWS];			/* a row's first byte */
unsigned char aspect[128];			/* t * 5/6: the CRT's pixels are taller than wide */
const unsigned char bits[8] = {0x80, 0x40, 0x20, 0x10, 8, 4, 2, 1};

/* A 16th, to the nearest: the turn of a pass.  A star out of view turns
 * every other pass, by twice that: an eighth. */
#define TURN16(v) (((v) + 8) >> 4)
#define TURN8(v) (((v) + 4) >> 3)

static int kb_get(void)
{
	int c;

	if (kbtail == kbhead)
		return -1;
	c = kbring[kbtail];
	kbtail = (kbtail + 1) & 15;
	return c;
}

/* The hold timers run down a frame; a code arriving winds its key's up.
 * Q is the way out. */
static int keys(void)
{
	int c, k;

	for (k = 0; k < kKeys; k++)
		if (hold[k])
			hold[k]--;
	while ((c = kb_get()) >= 0) {
		switch (c) {
		case KEY_UP: k = kUp; break;
		case KEY_DOWN: k = kDown; break;
		case KEY_LEFT: k = kLeft; break;
		case KEY_RIGHT: k = kRight; break;
		case KEY_Q: return 0;
		default: continue;
		}
		hold[k] = hold[k] ? HOLD_NEXT : HOLD_FIRST;
	}
	return 1;
}

/* The cone's half-width at depth z: 15/8 of z, half as wide again as
 * the screen sees (5/4 of z). */
static int cone(int z)
{
	return z + z - (z >> 3);
}

/* A random number in -lim..lim. */
static int spread(int lim)
{
	return (int)((unsigned)rand() % (unsigned)(lim + lim + 1)) - lim;
}

/* The depths the star's x and y decide, z falling: in view while
 * |x|*4 < 5z and |y|*16 < 15z, taken a little early as 13|x|/16 and
 * 17|y|/16, so that x*128 is never asked of a star beyond 160*z; in
 * the cone while |x| and |y| are under 15z/8, taken as 17/32 of them. */
static void settle(struct star *s)
{
	int x = s->x < 0 ? -s->x : s->x;
	int y = s->y < 0 ? -s->y : s->y;
	int m = x > y ? x : y;
	int seen = x - (x >> 3) - (x >> 4);
	int yseen = y + (y >> 4) + 1;

	if (seen < yseen)
		seen = yseen;
	s->zseen = seen;
	s->zgone = (m >> 1) + (m >> 5) + 1;
	if (s->zgone < ZNEAR)
		s->zgone = ZNEAR;
}

static void born(struct star *s, int z)
{
	int lim = cone(z);

	s->x = spread(lim);
	s->y = spread(lim);
	s->z = z;
	s->p = 0;
	s->stale = 0;
	settle(s);
}

/* The turned star's place: one turned behind the ship or past the far
 * wall is born again; one the turn took past the cone's edge comes in
 * at the other.  Whether it is in view is decided here and now - the
 * full settling waits for the turn to end (fly). */
static void turned(struct star *s)
{
	int lim, ax, ay;

	if (s->z < ZNEAR || s->z > ZFAR) {
		born(s, ZFAR);
		return;
	}
	lim = cone(s->z);
	if (s->x > lim)
		s->x = -lim;
	else if (s->x < -lim)
		s->x = lim;
	if (s->y > lim)
		s->y = -lim;
	else if (s->y < -lim)
		s->y = lim;
	ax = s->x < 0 ? -s->x : s->x;
	ay = s->y < 0 ? -s->y : s->y;
	s->zseen = (ax << 2) < s->z * 5 && (ay << 4) < s->z * 15 ? ZNEAR - 1 : ZFAR + 1;
	s->zgone = ZNEAR;
	s->stale = 1;
}

/* The cone turned round the ship the other way from the ship's turn:
 * a 16th of a radian, or an eighth for a star out of view, which turns
 * every other pass. */
#define TURNED_BY(name, TURN)						\
static void name(struct star *s)					\
{									\
	int t;								\
									\
	if (hold[kDown]) {		/* the nose up: the stars fall */	\
		t = s->y;						\
		s->y = t - TURN(s->z);					\
		s->z = s->z + TURN(t);					\
	}								\
	if (hold[kUp]) {						\
		t = s->y;						\
		s->y = t + TURN(s->z);					\
		s->z = s->z - TURN(t);					\
	}								\
	if (hold[kLeft]) {		/* the ship counter-clockwise: the stars clockwise */ \
		t = s->x;						\
		s->x = t + TURN(s->y);					\
		s->y = s->y - TURN(t);					\
	}								\
	if (hold[kRight]) {						\
		t = s->x;						\
		s->x = t - TURN(s->y);					\
		s->y = s->y + TURN(t);					\
	}								\
	turned(s);							\
}
TURNED_BY(turn, TURN16)
TURNED_BY(turn_twice, TURN8)

/* turning: 0 for none, 1 on a pass that turns the stars in view, 2 on
 * one that turns the ones out of view as well, by two steps. */
static void fly(struct star *s, int turning)
{
	if (s->p) {
		*s->p &= (unsigned char)~s->bit;
		s->p = 0;
	}
	if (turning) {
		if (s->z > s->zseen)
			turn(s);
		else if (turning == 2)
			turn_twice(s);
	} else if (s->stale) {
		s->stale = 0;
		settle(s);
	}
	s->z -= SPEED;
	if (s->z < s->zgone) {
		born(s, ZFAR);		/* passed, or flown out of the cone */
		return;
	}
	if (s->z > s->zseen)
		draw(s);
}

/* The screen black with the ink bright white, and the tables. */
static void prepare(void)
{
	unsigned *w = (unsigned *)vram;
	int n;

	for (n = ROWS * STRIDE / 2; n > 0; n--)
		*w++ = 0x4700;
	for (n = 0; n < ROWS; n++)
		rowp[n] = vram + n * STRIDE;
	for (n = 0; n < (int)sizeof aspect; n++)
		aspect[n] = (unsigned char)(n * 5 / 6);
}

int main(void)
{
	unsigned last, passes = 0;
	int i, turning;

	for (i = 0; i < NSTARS; i++)
		born(&stars[i], ZNEAR + (int)((unsigned)rand() % (ZFAR - ZNEAR)));
	hw_begin();
	prepare();
	last = frames;
	while (keys()) {
		while (frames - last < PACE)
			;
		last = frames;
		turning = hold[kUp] | hold[kDown] | hold[kLeft] | hold[kRight];
		if (turning)
			turning = 1 + (passes & 1);
		for (i = 0; i < NSTARS; i++)
			fly(&stars[i], turning);
		passes++;
	}
	hw_end();
	/* How the machine kept up: a pass every PACE frames is the full pace. */
	printf("STARS: %u passes in %u frames\n", passes, frames);
	return 0;
}
