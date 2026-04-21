/*
 * board.h — MS0515 system board (motherboard)
 *
 * The system board integrates all components of the Elektronika MS 0515:
 *   - KR1807VM1 CPU (PDP-11 compatible)
 *   - 128 KB RAM with bank switching
 *   - 16 KB ROM
 *   - 16 KB Video RAM
 *   - KR580VI53 programmable timer (3 channels)
 *   - KR580VV51 keyboard USART
 *   - KR580VV51 serial port USART
 *   - KR580VV55 parallel port (system registers A, B, C)
 *   - KR1818VG93 floppy disk controller
 *
 * I/O register map (from NS4 tech desc, Appendix 1):
 *   0177400–0177437  Memory Dispatcher (bank register)
 *   0177440          Keyboard data (read)
 *   0177442          Keyboard status (read) / command (write)
 *   0177460          Keyboard TX data (write)
 *   0177462          Keyboard command (write)
 *   0177500–0177506  Timer read (channels 0-2, control)
 *   0177520–0177526  Timer write (channels 0-2, control)
 *   0177600          System Register A — floppy/ROM control (write)
 *   0177602          System Register B — status input (read)
 *   0177604          System Register C — audio/video (write)
 *   0177606          PPI control word (KR580VV55)
 *   0177640          FDC status/command
 *   0177642          FDC track register
 *   0177644          FDC sector register
 *   0177646          FDC data register
 *   0177700          Serial data (read)
 *   0177702          Serial status (read) / command (write)
 *   0177720          Serial TX data (write)
 *   0177722          Serial command (write)
 *
 * System Register A (0177600) — output:
 *   Bits 1-0  Floppy drive select (0–3)
 *   Bit  2    Motor on (active low)
 *   Bit  3    Side select (1 = upper)
 *   Bit  4    LED VD9
 *   Bit  5    LED VD16
 *   Bit  6    Cassette output
 *   Bit  7    Extended ROM (1 = full 16 KB visible at 140000–177377)
 *
 * System Register B (0177602) — input:
 *   Bit  0    FDC INTRQ (0 = ready for command)
 *   Bit  1    FDC DRQ (1 = data byte ready)
 *   Bit  2    Drive ready (0 = ready)
 *   Bits 4-3  DIP switches: video refresh rate
 *                00 = 50 Hz, 01 = 72 Hz, 1x = 60 Hz
 *   Bit  7    Cassette input
 *
 * System Register C (0177604) — output:
 *   Bits 2-0  Border color (GRB)
 *   Bit  3    Video resolution (0 = 320×200, 1 = 640×200)
 *   Bit  4    LED VD17
 *   Bit  5    Sound control (tone)
 *   Bit  6    Sound enable
 *   Bit  7    Timer gate (speaker modulation)
 *
 * Sources:
 *   - NS4 technical description (3.858.420 TO), sections 4.1–4.10
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#ifndef MS0515_BOARD_H
#define MS0515_BOARD_H

#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"
#include "memory.h"
#include "timer.h"
#include "keyboard.h"
#include "floppy.h"
#include "ramdisk.h"
#include "trace.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Callbacks — the board uses these to notify the frontend ──────────────── */

/*
 * Called whenever the sound output state changes.
 * `value` is 0 or 1 (square wave speaker signal).
 */
typedef void (*board_sound_cb_t)(void *userdata, int value);

/*
 * Called when a byte is sent to the serial port (printer/RS-232).
 * Returns true if the byte was accepted.
 */
typedef bool (*board_serial_out_cb_t)(void *userdata, uint8_t byte);

/*
 * Called when the serial port wants to receive a byte.
 * Returns true and fills `*byte` if data is available.
 */
typedef bool (*board_serial_in_cb_t)(void *userdata, uint8_t *byte);

/* ── Board state ──────────────────────────────────────────────────────────── */

typedef struct ms0515_board {
    /* Core components */
    ms0515_cpu_t      cpu;
    ms0515_memory_t   mem;
    ms0515_timer_t    timer;
    ms0515_keyboard_t kbd;
    ms0515_floppy_t   fdc;

    /* System registers (PPI KR580VV55, ports A/B/C) */
    uint8_t  reg_a;             /* System Register A (output)               */
    uint8_t  reg_b;             /* System Register B (input)                */
    uint8_t  reg_c;             /* System Register C (output)               */
    uint8_t  ppi_control;       /* PPI control word                         */

    /* Video state */
    bool     hires_mode;        /* true = 640×200, false = 320×200          */
    uint8_t  border_color;      /* 3-bit border color (GRB)                 */

    /* Sound state */
    bool     sound_on;          /* Master sound enable                      */
    int      sound_value;       /* Current speaker level (0 or 1)           */
    int      frame_cycle_pos;   /* CPU cycles elapsed in current frame      */

    /* DIP switch setting */
    uint8_t  dip_refresh;       /* Video refresh rate: 0=50, 1=72, 2=60 Hz  */

    /* Timing accumulators (in CPU clock ticks at 7.5 MHz) */
    int      timer_counter;     /* Counts down to next timer tick           */
    int      frame_counter;     /* Counts down to next VBlank               */

    /* Callbacks */
    board_sound_cb_t      sound_cb;
    board_serial_out_cb_t serial_out_cb;
    board_serial_in_cb_t  serial_in_cb;
    void                 *cb_userdata;

    /* Expansion RAM disk (EX0:) — see ramdisk.h for full documentation */
    ms0515_ramdisk_t ramdisk;

    /* Tape interface (bit 7 of RegB reads constant 0 — no recorder) */
    uint32_t tape_bit_counter;     /* reserved (snapshot compat) */

    /* I/O trace (see trace.h for zero-cost macros) */
    ms0515_trace_t trace;
} ms0515_board_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/*
 * board_init — Initialize the entire board and all sub-components.
 */
void board_init(ms0515_board_t *board);

/*
 * board_reset — Hardware reset (equivalent to power-on sequence).
 *
 * Resets CPU, timer, keyboard, FDC.  Clears system registers.
 * Programs PPI to mode 0, port B = input, ports A/C = output (code 202₈).
 */
void board_reset(ms0515_board_t *board);

/*
 * board_load_rom — Load a ROM image into the board's memory.
 */
void board_load_rom(ms0515_board_t *board, const uint8_t *data, uint32_t size);

/*
 * board_step_frame — Execute one video frame worth of CPU cycles.
 *
 * At 7.5 MHz and 50 Hz, this is 150000 cycles per frame.
 * Returns true if the frame completed normally, false if the CPU halted.
 */
bool board_step_frame(ms0515_board_t *board);

/*
 * board_step_cpu — Execute a single CPU instruction (for debugger).
 */
void board_step_cpu(ms0515_board_t *board);

/*
 * board_key_event — Notify the board of a key press or release.
 */
void board_key_event(ms0515_board_t *board, uint8_t scancode);

/* ── Memory bus interface (called by CPU) ─────────────────────────────────── */

/*
 * These are the functions that the CPU calls to read/write memory.
 * They handle address translation, I/O port dispatch, and VRAM access.
 */
uint16_t board_read_word(ms0515_board_t *board, uint16_t address);
void     board_write_word(ms0515_board_t *board, uint16_t address, uint16_t value);
uint8_t  board_read_byte(ms0515_board_t *board, uint16_t address);
void     board_write_byte(ms0515_board_t *board, uint16_t address, uint8_t value);

/* ── Callback registration ────────────────────────────────────────────────── */

void board_set_sound_callback(ms0515_board_t *board,
                              board_sound_cb_t cb, void *userdata);

void board_set_serial_callbacks(ms0515_board_t *board,
                                board_serial_in_cb_t in_cb,
                                board_serial_out_cb_t out_cb,
                                void *userdata);

/* ── RAM disk expansion (EX0:) ────────────────────────────────────────────── */

/*
 * board_ramdisk_enable — Enable the 512 KB RAM disk expansion board.
 * Allocates the backing memory and enables the I/O address range.
 */
void board_ramdisk_enable(ms0515_board_t *board);

/*
 * board_ramdisk_free — Release RAM disk memory.
 */
void board_ramdisk_free(ms0515_board_t *board);

/* ── Video buffer access ──────────────────────────────────────────────────── */

const uint8_t *board_get_vram(const ms0515_board_t *board);
bool           board_is_hires(const ms0515_board_t *board);
uint8_t        board_get_border_color(const ms0515_board_t *board);

/*
 * board_set_trace_callback — Enable or disable I/O tracing.
 *
 * When `cb` is non-NULL, trace macros (BOARD_TRACE) deliver formatted
 * lines to the callback.  Pass NULL to disable tracing.
 */
void board_set_trace_callback(ms0515_board_t *board,
                              ms0515_trace_cb_t cb, void *userdata);

/* Convenience macros: trace through the board's embedded trace context. */
#define BOARD_TRACE(board, ...)      TRACE(&(board)->trace, __VA_ARGS__)
#define BOARD_TRACE_ACTIVE(board)    TRACE_ACTIVE(&(board)->trace)

#ifdef __cplusplus
}
#endif

#endif /* MS0515_BOARD_H */
