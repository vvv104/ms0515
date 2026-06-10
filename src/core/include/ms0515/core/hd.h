/*
 * hd.h — Paravirtual hard-disk device (HD:)
 *
 * The MS 0515 never shipped with a hard-disk controller.  HD is a
 * *paravirtual* block device — a convention shared across PDP-11
 * emulators (originating from the DVK emulator at pdp-11.org.ru) — that
 * a stock RT-11 driver (HD.SYS) talks to over two I/O registers.  It
 * lets RT-11 mount a backing image of arbitrary size as a fast random-
 * access volume, without modelling any real silicon.
 *
 * This module emulates the "t2" controller variant: 32-bit image sizes,
 * 22-bit buffer addresses, no interrupts, synchronous (programmed) DMA.
 * The driver distinguishes variants by the CSR value at rest:
 *   - low-byte bit 7 clear  → t1 (stops the CPU; not us)
 *   - bit 14 (CS.DMA) set   → t4/t5
 *   - bit 6  (CS.INT) set   → t3/t5 (interrupt-capable)
 * We report low-byte bit 7 set and bits 6/14 clear, i.e. a t2 device.
 *
 * Register map (the HD.SYS default HDCSR = 0177720):
 *   0177720  HDCSR  — command (write) / status (read)
 *   0177721  HDCSR+1 (byte) — high address bits of the DMA buffer (XM)
 *   0177722  HDDAT  — command argument (write) / result (read)
 *
 * Note these addresses overlap the (currently stubbed, unused) MS 0515
 * serial-port TX side.  board.c routes them to HD only while a HD image
 * is mounted; otherwise the serial stub keeps the addresses.
 *
 * Command protocol (from HD.SYS v2.0 disassembly, HD Sources/v2.0/HD.MAC):
 *   To issue a command the driver writes the argument to HDDAT (0177722)
 *   then the command code to HDCSR (0177720) — the CSR write executes it.
 *   After a transfer the driver spins on HDCSR until low-byte bit 7 is
 *   set (ready), then tests the CSR sign bit (bit 15) for an error.
 *   Because our execution is synchronous, "ready" is always true.
 *
 *   SetUni=1  select unit (argument & 7)
 *   SetBlk=2  set starting block number
 *   SetBuf=3  set DMA buffer address (low 16 bits via HDDAT, high via
 *             HDCSR+1 in XM builds)
 *   SetWCn=4  set transfer length in words
 *   CmdRea=5  read  WordCount words: image[block] -> memory[buffer]
 *   CmdWri=6  write WordCount words: memory[buffer] -> image[block]
 *   GetSiz=7  report the selected unit's size in blocks (read from HDDAT)
 *
 * Sources:
 *   - HD driver distribution kit (HD t1..t5), pdp-11.org.ru
 *   - HD.SYS v2.0 source (HD Sources/v2.0/HD.MAC)
 */

#ifndef MS0515_HD_H
#define MS0515_HD_H

#include <stdint.h>
#include <stdbool.h>

struct ms0515_memory;   /* forward decl — DMA target, defined in memory.h */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ───────────────────────────────────────────────────────────── */

#define HD_BLOCK_SIZE   512         /* RT-11 block size in bytes              */
#define HD_MAX_BLOCKS   65535u       /* RT-11 caps a logical volume here       */

/* ── I/O port offsets (relative to 0177400 base) ────────────────────────── */

#define HD_IO_CSR       0xD0        /* 0177720 — command / status            */
#define HD_IO_CSR_HI    0xD1        /* 0177721 — DMA buffer high byte (XM)    */
#define HD_IO_DATA      0xD2        /* 0177722 — argument / result           */

/* ── Command codes (written to HDCSR) ───────────────────────────────────── */

#define HD_CMD_SET_UNIT   1
#define HD_CMD_SET_BLOCK  2
#define HD_CMD_SET_BUF    3
#define HD_CMD_SET_WCNT   4
#define HD_CMD_READ       5
#define HD_CMD_WRITE      6
#define HD_CMD_GET_SIZE   7

/* ── CSR status bits ────────────────────────────────────────────────────── */

#define HD_CS_READY     0x0080      /* low-byte bit 7: ready (marks non-t1)  */
#define HD_CS_INT       0x0040      /* bit 6  (CS.INT): interrupt-capable    */
#define HD_CS_DMA       0x4000      /* bit 14 (CS.DMA): t4/t5 marker         */
#define HD_CS_ERROR     0x8000      /* bit 15: last command failed           */

/* ── Device state ───────────────────────────────────────────────────────── */

typedef struct ms0515_hd {
    bool      enabled;          /* image mounted and reachable on the bus     */

    /* Backing image (single unit 0).  Owned by this module. */
    uint8_t  *image;            /* malloc'd image bytes, NULL when unmounted  */
    uint32_t  image_size;       /* image length in bytes                      */
    bool      dirty;            /* image modified since the last clear        */

    /* Controller registers / latched command arguments. */
    uint16_t  unit;             /* selected unit (only 0 is backed)           */
    uint32_t  block;            /* starting block number                      */
    uint32_t  bufaddr;          /* DMA buffer address (up to 22 bits)         */
    uint8_t   addr_hi;          /* pending HDCSR+1 high byte for SetBuf        */
    uint16_t  wcount;           /* transfer length in words                   */
    uint16_t  data_reg;         /* HDDAT read value (e.g. GetSize result)     */
    uint16_t  status;           /* HDCSR read value                           */

    int       activity_remaining;   /* CPU cycles until the activity LED dims */
} ms0515_hd_t;

/* ── Public API ─────────────────────────────────────────────────────────── */

/*
 * hd_init — Reset the device to the unmounted power-on state.  Does not
 * allocate memory.
 */
void hd_init(ms0515_hd_t *hd);

/*
 * hd_mount — Allocate `size` bytes for the backing image and enable the
 * device.  If `data` is non-NULL, `size` bytes are copied in; otherwise
 * the image is zero-filled.  Returns false on allocation failure or when
 * `size` is not a positive multiple of HD_BLOCK_SIZE.
 *
 * A previously mounted image is released first.
 */
bool hd_mount(ms0515_hd_t *hd, const uint8_t *data, uint32_t size);

/*
 * hd_unmount — Release the backing image and disable the device.
 */
void hd_unmount(ms0515_hd_t *hd);

/*
 * hd_reset — Clear the controller registers (not the image contents).
 */
void hd_reset(ms0515_hd_t *hd);

/*
 * hd_handles — True if `offset` (relative to 0177400) is a HD register.
 */
bool hd_handles(uint16_t offset);

/*
 * hd_read_word / hd_read_byte — Read a HD register.
 */
uint16_t hd_read_word(ms0515_hd_t *hd, uint16_t offset);
uint8_t  hd_read_byte(ms0515_hd_t *hd, uint16_t offset);

/*
 * hd_write_word / hd_write_byte — Write a HD register.  A command write
 * to HDCSR may trigger a DMA transfer against `mem`, so the caller must
 * pass the board's memory.  `mem` may be NULL only when the write cannot
 * start a transfer (argument registers); passing it always is safe.
 */
void hd_write_word(ms0515_hd_t *hd, struct ms0515_memory *mem,
                   uint16_t offset, uint16_t value);
void hd_write_byte(ms0515_hd_t *hd, struct ms0515_memory *mem,
                   uint16_t offset, uint8_t value);

/*
 * hd_tick — Decay the activity LED timer by `cycles` CPU cycles.
 */
void hd_tick(ms0515_hd_t *hd, int cycles);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_HD_H */
