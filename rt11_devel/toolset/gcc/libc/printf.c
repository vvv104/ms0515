/* printf.c - formatted output to the console or into a buffer.
 *
 * One formatter writes through a sink: the console's putchar, or a
 * buffer with a size, counting what would have been written as snprintf
 * says.  Conversions d i u x X o c s %, the flags - and 0, a width, a
 * precision for s, and l for a long.  A 16-bit number is divided with
 * the 16-bit helper, a long with libgcc's: the longs cost more, so an
 * int that fits is printed as one. */

#include <stdio.h>
#include <string.h>

struct sink {
	char *buffer;		/* 0: the console */
	size_t room;		/* what the buffer can take beyond its last 0 */
	int count;
};

static void put(struct sink *s, int c)
{
	if (!s->buffer)
		putchar(c);
	else if (s->room) {
		*s->buffer++ = (char)c;
		s->room--;
	}
	s->count++;
}

struct spec {
	int left, zero, width, precision, isLong;
};

/* The digits of value in base, least significant first, into end and
 * backwards; where they start comes back. */
static char *digits(unsigned long value, int base, int upper, int isLong, char *end)
{
	const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	*--end = 0;
	if (!isLong) {
		unsigned v = (unsigned)value;
		do {
			*--end = alphabet[v % base];
			v /= base;
		} while (v);
	} else {
		do {
			*--end = alphabet[value % base];
			value /= base;
		} while (value);
	}
	return end;
}

static void padded(struct sink *s, const struct spec *sp, const char *sign, const char *text, int length)
{
	int pad = sp->width - length - (int)strlen(sign);

	if (!sp->left && !sp->zero)
		while (pad-- > 0) put(s, ' ');
	while (*sign)
		put(s, *sign++);
	if (!sp->left && sp->zero)
		while (pad-- > 0) put(s, '0');
	while (length-- > 0)
		put(s, *text++);
	if (sp->left)
		while (pad-- > 0) put(s, ' ');
}

static void number(struct sink *s, const struct spec *sp, int conversion, va_list *args)
{
	char buffer[12];
	const char *sign = "";
	unsigned long value;
	int base = 10, upper = 0;
	char *text;

	if (conversion == 'd' || conversion == 'i') {
		long v = sp->isLong ? va_arg(*args, long) : (long)va_arg(*args, int);
		if (v < 0) {
			sign = "-";
			v = -v;
		}
		value = (unsigned long)v;
	} else {
		value = sp->isLong ? va_arg(*args, unsigned long) : (unsigned long)va_arg(*args, unsigned);
		if (conversion == 'o') base = 8;
		if (conversion == 'x' || conversion == 'X') base = 16;
		upper = conversion == 'X';
	}
	text = digits(value, base, upper, sp->isLong || value > 0xFFFFUL, buffer + sizeof buffer);
	padded(s, sp, sign, text, (int)strlen(text));
}

static const char *conversion(struct sink *s, const char *f, va_list *args)
{
	struct spec sp = {0, 0, 0, -1, 0};

	for (;; f++) {
		if (*f == '-') sp.left = 1;
		else if (*f == '0') sp.zero = 1;
		else break;
	}
	while (*f >= '0' && *f <= '9')
		sp.width = sp.width * 10 + (*f++ - '0');
	if (*f == '.') {
		sp.precision = 0;
		for (f++; *f >= '0' && *f <= '9'; f++)
			sp.precision = sp.precision * 10 + (*f - '0');
	}
	if (*f == 'l') { sp.isLong = 1; f++; }
	else if (*f == 'h') f++;

	switch (*f) {
	case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
		number(s, &sp, *f, args);
		break;
	case 'c': {
		char c = (char)va_arg(*args, int);
		sp.zero = 0;
		padded(s, &sp, "", &c, 1);
		break;
	}
	case 's': {
		const char *text = va_arg(*args, const char *);
		int length = (int)strlen(text);
		if (sp.precision >= 0 && sp.precision < length)
			length = sp.precision;
		sp.zero = 0;
		padded(s, &sp, "", text, length);
		break;
	}
	case '%':
		put(s, '%');
		break;
	default:		/* not a conversion: shown as it stands */
		put(s, '%');
		if (*f) put(s, *f);
		break;
	}
	return *f ? f + 1 : f;
}

static int format(struct sink *s, const char *f, va_list args)
{
	va_list a;

	va_copy(a, args);
	while (*f) {
		if (*f == '%')
			f = conversion(s, f + 1, &a);
		else
			put(s, *f++);
	}
	va_end(a);
	if (s->buffer)
		*s->buffer = 0;
	return s->count;
}

int vprintf(const char *f, va_list args)
{
	struct sink s = {0, 0, 0};
	return format(&s, f, args);
}

int printf(const char *f, ...)
{
	va_list args;
	int n;

	va_start(args, f);
	n = vprintf(f, args);
	va_end(args);
	return n;
}

int vsnprintf(char *buffer, size_t size, const char *f, va_list args)
{
	char none[1];
	struct sink s = {size ? buffer : none, size ? size - 1 : 0, 0};
	return format(&s, f, args);
}

int snprintf(char *buffer, size_t size, const char *f, ...)
{
	va_list args;
	int n;

	va_start(args, f);
	n = vsnprintf(buffer, size, f, args);
	va_end(args);
	return n;
}

int sprintf(char *buffer, const char *f, ...)
{
	va_list args;
	int n;

	va_start(args, f);
	n = vsnprintf(buffer, (size_t)-1, f, args);
	va_end(args);
	return n;
}
