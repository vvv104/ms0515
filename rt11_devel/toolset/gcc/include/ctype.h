/* ctype.h - the classes of ASCII characters.  The console's KOI-8
 * letters (bit 7 set) are none of them. */

#ifndef CTYPE_H
#define CTYPE_H

static inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c) { return c >= 'a' && c <= 'z'; }
static inline int isalpha(int c) { return isupper(c) || islower(c); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isxdigit(int c)
{ return isdigit(c) || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
static inline int isspace(int c)
{ return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static inline int isprint(int c) { return c >= ' ' && c < 0177; }
static inline int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
static inline int iscntrl(int c) { return (c >= 0 && c < ' ') || c == 0177; }
static inline int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
static inline int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }

#endif
