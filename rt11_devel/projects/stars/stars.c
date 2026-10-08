/* STARS - a flight through a starfield, seen from the cockpit.
 *
 * The stars live in a box ahead of the ship: x to the right, y up, z
 * forward, the ship at the origin looking down z.  Each frame every
 * star comes nearer by the ship's speed, and the picture is its
 * projection on the screen - sx = 160 + x*32/z, sy = 100 - y*32/z,
 * less a sixth for the CRT's pixels, which are taller than wide - so a
 * star born on the far wall appears near the centre and slides outward,
 * the faster the nearer it is.  A star that passes the ship or leaves
 * the screen is born again on the far wall.
 *
 * The keys turn the ship, which turns the box the other way round the
 * ship: Down pulls the nose up (the stars fall), Up pushes it down;
 * Left rolls the ship counter-clockwise (the stars turn clockwise),
 * Right clockwise.  A turn is a 32nd of a radian a frame, by shifts;
 * the cosine is taken as one, which lets a star grow a twentieth of a
 * percent a frame and nobody notice.  Q ends the flight.
 *
 * The keyboard sends a key's code when it is pressed and again while it
 * is held, never a release (docs/programming.md): a held key is a timer
 * refreshed by its code - nine frames on the first code, which has to
 * outlast the keyboard's silence before the repeats, four after a
 * repeat, which come every two or three.
 *
 * The machine's part - the window on video memory, the frame counter,
 * the keyboard's ring - is machine.s. */

#include <stdlib.h>

void hw_begin(void);
void hw_end(void);
extern volatile unsigned frames;
extern volatile unsigned char kbring[16];
extern volatile unsigned kbhead;

#define NSTARS 48
#define ZFAR 1024
#define ZNEAR 16
#define SPEED 6
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
	int sx, sy;		/* where it is drawn; sx < 0: nowhere */
};

static struct star stars[NSTARS];
static int hold[kKeys];
static unsigned kbtail;
static unsigned char *const vram = (unsigned char *)0100000;

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

static void plot(int sx, int sy, int on)
{
	unsigned char *p = vram + sy * STRIDE + ((sx >> 3) << 1);
	unsigned char bit = (unsigned char)(0x80 >> (sx & 7));

	if (on)
		*p |= bit;
	else
		*p &= (unsigned char)~bit;
}

static void born(struct star *s, int z)
{
	s->x = (rand() & 2047) - 1024;
	s->y = (rand() & 2047) - 1024;
	s->z = z;
	s->sx = -1;
}

/* The box turned round the ship the other way from the ship's turn. */
static void turn(struct star *s)
{
	int t;

	if (hold[kDown]) {		/* the nose up: the stars fall */
		t = s->y;
		s->y = t - (s->z >> 5);
		s->z = s->z + (t >> 5);
	}
	if (hold[kUp]) {
		t = s->y;
		s->y = t + (s->z >> 5);
		s->z = s->z - (t >> 5);
	}
	if (hold[kLeft]) {		/* the ship counter-clockwise: the stars clockwise */
		t = s->x;
		s->x = t + (s->y >> 5);
		s->y = s->y - (t >> 5);
	}
	if (hold[kRight]) {
		t = s->x;
		s->x = t - (s->y >> 5);
		s->y = s->y + (t >> 5);
	}
}

static void fly(struct star *s)
{
	int sx, sy, t;

	if (s->sx >= 0)
		plot(s->sx, s->sy, 0);
	turn(s);
	s->z -= SPEED;
	if (s->z < ZNEAR) {
		born(s, ZFAR);
		return;
	}
	sx = 160 + (s->x << 5) / s->z;
	t = (s->y << 5) / s->z;
	sy = 100 - (t - (t >> 3) - (t >> 5));
	if (sx < 0 || sx >= COLUMNS || sy < 0 || sy >= ROWS) {
		born(s, ZFAR);
		return;
	}
	s->sx = sx;
	s->sy = sy;
	plot(sx, sy, 1);
}

static void black(void)
{
	unsigned *w = (unsigned *)vram;
	int n;

	for (n = ROWS * STRIDE / 2; n > 0; n--)
		*w++ = 0x4700;		/* no pixels, the ink bright white */
}

int main(void)
{
	unsigned last;
	int i;

	for (i = 0; i < NSTARS; i++)
		born(&stars[i], ZNEAR + (rand() & (ZFAR - 1)));
	hw_begin();
	black();
	last = frames;
	while (keys()) {
		while (frames == last)
			;
		last = frames;
		for (i = 0; i < NSTARS; i++)
			fly(&stars[i]);
	}
	hw_end();
	return 0;
}
