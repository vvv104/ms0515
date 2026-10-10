/* fx.c - the sine and the cosine, from a table of a quarter turn
 * (include/fx.h).  An angle is in 256ths of a turn; the values are 8.8,
 * 256 for one, rounded. */

#include <fx.h>

static const short quarter[65] = {
	  0,   6,  13,  19,  25,  31,  38,  44,  50,  56,  62,  68,  74,
	 80,  86,  92,  98, 104, 109, 115, 121, 126, 132, 137, 142, 147,
	152, 157, 162, 167, 172, 177, 181, 185, 190, 194, 198, 202, 206,
	209, 213, 216, 220, 223, 226, 229, 231, 234, 237, 239, 241, 243,
	245, 247, 248, 250, 251, 252, 253, 254, 255, 255, 256, 256, 256,
};

int fx_sin(int angle)
{
	int a = angle & 255;
	int v = (a & 64) ? quarter[128 - (a & 127)] : quarter[a & 63];

	return (a & 128) ? -v : v;
}

int fx_cos(int angle)
{
	return fx_sin(angle + 64);
}
