/* rt11.h - RT-11's programmed requests for a C program on the MS 0515.
 *
 * Each is a function in rt/emt.s with the request's own name: what it does
 * is the monitor's to say (the RT-11 Programmer's Reference Manual), what
 * the machine adds is docs/programming.md.  The C library (stdio and the
 * rest) is built over these; a program may use them directly. */

#ifndef RT11_H
#define RT11_H

/* .TTYOUT: the character out to the console, waiting for room in the
 * monitor's ring. */
void rt11_ttyout(int c);

/* .TTYIN: the next character typed, waited for; the low 7 bits. */
int rt11_ttyin(void);

/* .PRINT: the string out, up to a 0 - and a new line after it - or up to
 * a 0200 without one. */
void rt11_print(const char *s);

/* .SETTOP: the program's memory up to top, or as much as there is; the
 * new high limit comes back. */
void *rt11_settop(void *top);

/* .EXIT: back to the monitor.  exit() of the C library is this. */
void exit(int status) __attribute__((noreturn));

/* -- Files, a block of 512 bytes at a time (rt/files.c) ---------------
 * A program opens a file on a channel (0..15) by name once at the start
 * - .LOOKUP needs the USR - and reads or writes blocks of it during its
 * run.  The monitor lies behind the VRAM window: close the window round
 * a request (ms_window in ms0515.h).  Every request gives -1 on an
 * error, whose code rt11_error() tells. */

/* "DK:NAME.EXT", or "NAME.EXT" on DK, as the four RAD50 words the
 * monitor takes.  0. */
int rt11_filespec(unsigned spec[4], const char *name);
/* .LOOKUP: the file opened for reading on the channel; its length in
 * blocks, or -1 (the error: 0 the channel in use, 1 no such file, 2 no
 * such device). */
int rt11_lookup(int channel, const unsigned spec[4]);
/* .ENTER: a new file of so many blocks (0: half the largest free space)
 * opened for writing on the channel; its length, or -1.  rt11_close
 * makes it permanent. */
int rt11_enter(int channel, const unsigned spec[4], int blocks);
/* .READW: `words` words from the file's block into the buffer, waited
 * for; the words read - fewer at the end of the file - or -1 (the
 * error: 0 past the end, 1 the device, 2 the channel not open). */
int rt11_readw(int channel, unsigned block, void *buffer, unsigned words);
/* .READ: the same begun and left to the disk's handler, which fills the
 * buffer on its interrupts while the program goes on; rt11_wait says
 * when it is done.  The words asked for, or -1. */
int rt11_read(int channel, unsigned block, void *buffer, unsigned words);
/* .WRITW: `words` words from the buffer into the file's block, waited
 * for; the words written, or -1. */
int rt11_writew(int channel, unsigned block, const void *buffer, unsigned words);
/* .WAIT: until the channel's transfer is over; 0, or -1 when it failed. */
int rt11_wait(int channel);
/* .CLOSE: the channel freed; a file being written is made permanent. */
int rt11_close(int channel);
/* The last error's code, byte 052 of the system communication area. */
int rt11_error(void);

/* EMT 375 with a request block, and EMT 374 with the code and channel:
 * what the requests above are made of (rt/emt.s). */
int rt11_request(void *area);
int rt11_request374(unsigned code_and_channel);

/* The first address above the program - its stack's top, where the
 * program started with SP - and so the heap's beginning: malloc takes
 * the memory from here up by .SETTOP. */
extern char *rt11_memtop;

#endif
