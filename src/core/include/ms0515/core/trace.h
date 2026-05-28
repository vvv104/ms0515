/*
 * trace.h — Binary event ring for post-mortem debugging.
 *
 * The ring records structured I/O events (reg_a writes, dispatcher
 * changes, FDC commands, CPU traps, HALTs) as fixed-size 16-byte
 * records.  Zero allocations after init; push is a predicted branch
 * plus a few word stores.  cap == 0 disables the ring entirely —
 * push becomes a cheap no-op.
 *
 * Snapshot save dumps the ring into a HIST chunk; dump_state.py
 * decodes it for offline analysis of boot hangs and similar issues
 * without having to reproduce them under a live tracer.
 *
 * Event kind values are part of the on-disk HIST format — do not
 * renumber.
 */

#ifndef MS0515_TRACE_H
#define MS0515_TRACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MS0515_EVT_REG_A = 1,   /* data[0]  = reg_a value                   */
    MS0515_EVT_DISP  = 2,   /* data[0..1] = new dispatcher (little end) */
    MS0515_EVT_FDC   = 3,   /* data[0] = FDC reg (0..3), data[1] = val  */
    MS0515_EVT_TRAP  = 4,   /* data[0] = vector number                  */
    MS0515_EVT_HALT  = 5,   /* (no payload)                             */
    MS0515_EVT_MEMW  = 6,   /* write to a watched memory range;
                             * data_len=3: byte write
                             *   data[0..1] = addr, data[2] = byte value
                             * data_len=4: word write
                             *   data[0..1] = addr, data[2..3] = word   */
    MS0515_EVT_MEMR  = 7,   /* read from a watched memory range; same
                             * payload layout as MEMW (value is what
                             * was returned to the CPU)                 */
    MS0515_EVT_PSW   = 8,   /* PSW priority field (bits 7-5) changed
                             * between cpu_step entry and exit;
                             * data[0] = new priority (0..7)
                             * data[1] = previous priority             */
} ms0515_event_kind_t;

/* 16-byte event record.  Stored directly in the ring's backing array
 * and written verbatim into the HIST snapshot chunk. */
typedef struct {
    uint64_t cycle;       /* monotonic CPU cycle count at event time  */
    uint16_t pc;          /* CPU instruction_pc at event time         */
    uint8_t  kind;        /* ms0515_event_kind_t                      */
    uint8_t  data_len;    /* 0..4                                     */
    uint8_t  data[4];     /* event-specific payload                   */
} ms0515_event_t;

typedef struct {
    ms0515_event_t *events;    /* cap entries (or NULL when disabled)   */
    size_t          cap;       /* 0 = ring disabled                     */
    size_t          head;      /* next write slot, in [0, cap)          */
    uint64_t        written;   /* total pushes; wrap if > cap           */
} ms0515_event_ring_t;

/* Allocate or re-allocate the ring to `cap_events` slots.  Passing 0
 * frees any existing buffer and disables the ring. */
void ms0515_event_ring_resize(ms0515_event_ring_t *r, size_t cap_events);

/* Free backing storage and zero the ring. */
void ms0515_event_ring_free(ms0515_event_ring_t *r);

/* Push one event.  No-op if the ring is disabled (cap == 0). */
void ms0515_event_ring_push(ms0515_event_ring_t *r,
                            uint64_t cycle, uint16_t pc,
                            uint8_t kind,
                            const void *data, size_t len);

static inline bool ms0515_event_ring_enabled(const ms0515_event_ring_t *r)
{
    return r->cap > 0;
}

/* Iterate entries in chronological order, oldest first.  Calls `cb`
 * once per valid event with the caller's userdata.  Handles the wrap
 * case (only the most recent `cap` entries survive when written > cap).
 * Does nothing if the ring is disabled or empty. */
typedef void (*ms0515_event_visitor_t)(void *userdata,
                                       const ms0515_event_t *evt);
void ms0515_event_ring_walk(const ms0515_event_ring_t *r,
                            ms0515_event_visitor_t cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_TRACE_H */
