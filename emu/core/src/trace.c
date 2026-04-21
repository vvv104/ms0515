/*
 * trace.c — Generic trace emit function.
 *
 * Formats a message and delivers it via the trace context's callback.
 * Compiled only when MS0515_TRACE is defined; in release builds the
 * TRACE() macro expands to nothing and this translation unit is empty.
 */

#ifdef MS0515_TRACE

#include <ms0515/trace.h>
#include <stdarg.h>
#include <stdio.h>

void ms0515_trace_emit(const ms0515_trace_t *t, const char *fmt, ...)
{
    if (!t->cb) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    t->cb(t->userdata, buf);
}

#else

/* Prevent C4206 "empty translation unit" on MSVC in release builds. */
typedef int ms0515_trace_unused;

#endif /* MS0515_TRACE */
