/* checksum.c - the examples' library (checksum.h). */

#include "checksum.h"

unsigned checksum(const unsigned *words, int n)
{
	unsigned s = 0;

	while (n-- > 0)
		s += *words++;
	return s;
}
