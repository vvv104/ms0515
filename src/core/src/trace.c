/*
 * trace.c — Binary event ring implementation.
 */

#include <ms0515/core/trace.h>

#include <stdlib.h>
#include <string.h>

void ms0515_event_ring_resize(ms0515_event_ring_t *r, size_t cap_events)
{
    if (cap_events == 0) {
        ms0515_event_ring_free(r);
        return;
    }
    /* Always start fresh on resize — easier than moving live entries. */
    ms0515_event_t *mem = (ms0515_event_t *)
        calloc(cap_events, sizeof(ms0515_event_t));
    if (!mem) {
        ms0515_event_ring_free(r);
        return;
    }
    free(r->events);
    r->events  = mem;
    r->cap     = cap_events;
    r->head    = 0;
    r->written = 0;
}

void ms0515_event_ring_free(ms0515_event_ring_t *r)
{
    free(r->events);
    r->events  = NULL;
    r->cap     = 0;
    r->head    = 0;
    r->written = 0;
}

void ms0515_event_ring_push(ms0515_event_ring_t *r,
                            uint64_t cycle, uint16_t pc,
                            uint8_t kind,
                            const void *data, size_t len)
{
    if (r->cap == 0) return;

    ms0515_event_t *e = &r->events[r->head];
    e->cycle    = cycle;
    e->pc       = pc;
    e->kind     = kind;
    e->data_len = (uint8_t)(len > sizeof e->data ? sizeof e->data : len);
    memset(e->data, 0, sizeof e->data);
    if (data && e->data_len)
        memcpy(e->data, data, e->data_len);

    r->head = (r->head + 1) % r->cap;
    r->written++;
}

void ms0515_event_ring_walk(const ms0515_event_ring_t *r,
                            ms0515_event_visitor_t cb, void *userdata)
{
    if (!r->cap || !r->written || !cb) return;

    size_t count = r->written < r->cap ? (size_t)r->written : r->cap;
    size_t start = r->written < r->cap ? 0 : r->head;  /* oldest index */
    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start + i) % r->cap;
        cb(userdata, &r->events[idx]);
    }
}
