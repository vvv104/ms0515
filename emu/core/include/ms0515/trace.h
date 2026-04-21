/*
 * trace.h — Generic compile-time trace mechanism.
 *
 * Provides a lightweight callback-based trace context that can be embedded
 * in any struct.  When MS0515_TRACE is defined (trace builds), TRACE()
 * expands to a function call that formats and delivers the message via the
 * registered callback.  In release builds, all trace macros expand to
 * nothing — zero runtime overhead.
 *
 * This header has no knowledge of board, CPU, or any other emulator
 * structures.  It is a general-purpose utility.
 *
 * Usage:
 *   TRACE(&ctx->trace, "event x=%d", x);
 *   if (TRACE_ACTIVE(&ctx->trace)) { ... expensive formatting ... }
 */

#ifndef MS0515_TRACE_H
#define MS0515_TRACE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Trace callback: receives a null-terminated line without trailing newline. */
typedef void (*ms0515_trace_cb_t)(void *userdata, const char *line);

/* Generic trace context — embed in any struct that needs tracing. */
typedef struct ms0515_trace {
    ms0515_trace_cb_t cb;
    void             *userdata;
} ms0515_trace_t;

#ifdef MS0515_TRACE

/*
 * Format and emit a single trace line via the context's callback.
 * No-op if cb is NULL.
 */
void ms0515_trace_emit(const ms0515_trace_t *t, const char *fmt, ...);

static inline bool ms0515_trace_active(const ms0515_trace_t *t)
{
    return t->cb != NULL;
}

#define TRACE(t, ...)       ms0515_trace_emit((t), __VA_ARGS__)
#define TRACE_ACTIVE(t)     ms0515_trace_active(t)

#else /* !MS0515_TRACE */

#define TRACE(t, ...)       ((void)0)
#define TRACE_ACTIVE(t)     (false)

#endif /* MS0515_TRACE */

#ifdef __cplusplus
}
#endif

#endif /* MS0515_TRACE_H */
