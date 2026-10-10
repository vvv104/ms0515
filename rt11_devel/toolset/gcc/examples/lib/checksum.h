/* checksum.h - the examples' library: a block's sum, for FILES.  Here to
 * show rt11_c_library and LIBRARIES at work (../CMakeLists.txt). */

#ifndef CHECKSUM_H
#define CHECKSUM_H

/* The sum of n words, modulo 65536. */
unsigned checksum(const unsigned *words, int n);

#endif
