/*
 * memory.h — MS0515 address space and bank switching
 *
 * Physical memory layout (128 KB RAM + 16 KB ROM):
 *
 *   The CPU has a 16-bit address bus → 64 KB directly addressable.
 *   Memory is organized in 8 banks of 8 KB each (banks 0–7).
 *   A second set of 8 banks provides the "extended" (additional) RAM,
 *   giving 128 KB total.  Switching between primary and extended banks
 *   is controlled by the Memory Dispatcher register at 0177400.
 *
 *   Address ranges (octal):
 *     000000–017777  Bank 0  (8 KB)
 *     020000–037777  Bank 1  (8 KB)
 *     040000–057777  Bank 2  (8 KB)
 *     060000–077777  Bank 3  (8 KB)
 *     100000–117777  Bank 4  (8 KB)
 *     120000–137777  Bank 5  (8 KB)
 *     140000–157777  Bank 6  (8 KB)  — shadowed by extended ROM when enabled
 *     160000–177377  Bank 7  (8 KB)  — overlaid with ROM
 *     177400–177776                  — I/O registers
 *
 * Video RAM (VRAM):
 *   16 KB of physical RAM (banks 7 in the high memory area) is shared
 *   between the CPU and the video controller.  The CPU cannot access VRAM
 *   directly because it sits behind ROM addresses (160000–177777).
 *   Instead, VRAM is accessed through a movable "virtual window" —
 *   bits 10-11 of the Memory Dispatcher register select which CPU address
 *   range maps to VRAM, and bit 7 enables VRAM access:
 *
 *     Bits 11:10  Virtual window
 *       0   0     000000–037777
 *       0   1     040000–077777
 *       1   x     100000–137777
 *
 * ROM:
 *   Two K573RF4B UV-erasable PROMs, 16 KB total.
 *   In default mode, only the upper 8 KB is visible (160000–177377).
 *   When System Register A bit 7 = 1 ("extended ROM"), the full 16 KB
 *   is mapped at 140000–177377, but this shadows bank 6 of RAM.
 *
 * Sources:
 *   - NS4 technical description, section 4.3, figures 5-6
 *   - MAME driver: src/mame/ussr/ms0515.cpp
 */

#ifndef MS0515_MEMORY_H
#define MS0515_MEMORY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ────────────────────────────────────────────────────────────── */

#define MEM_BANK_SIZE       8192     /* 8 KB per bank                        */
#define MEM_BANK_COUNT      8        /* Banks 0–7                            */
#define MEM_RAM_SIZE        (MEM_BANK_SIZE * MEM_BANK_COUNT * 2)  /* 128 KB  */
#define MEM_ROM_SIZE        16384    /* 16 KB ROM                            */
#define MEM_VRAM_SIZE       16384    /* 16 KB video RAM                      */

/* Address type — result of address translation */
typedef enum {
    ADDR_TYPE_RAM       = 0,   /* Main or extended RAM                      */
    ADDR_TYPE_ROM       = 1,   /* ROM                                       */
    ADDR_TYPE_VRAM      = 2,   /* Video RAM (accessed through window)       */
    ADDR_TYPE_IO        = 3,   /* I/O register space (177400–177776)        */
    ADDR_TYPE_DENIED    = 4    /* Access denied / unmapped                  */
} mem_addr_type_t;

/* ── Memory Dispatcher register (0177400) bit fields ─────────────────────── */

#define MEM_DISP_BANK_MASK   0x007F  /* Bits 6-0: bank select (1=primary)   */
#define MEM_DISP_VRAM_EN     0x0080  /* Bit 7: VRAM access enable           */
#define MEM_DISP_MON_IRQ     0x0100  /* Bit 8: VBlank IRQ assertion         */
#define MEM_DISP_TIMER_IRQ   0x0200  /* Bit 9: Timer IRQ enable             */
#define MEM_DISP_VRAM_WIN0   0x0400  /* Bit 10: VRAM window select low      */
#define MEM_DISP_VRAM_WIN1   0x0800  /* Bit 11: VRAM window select high     */
#define MEM_DISP_PAR0        0x1000  /* Bit 12: Parallel port control       */
#define MEM_DISP_PAR1        0x2000  /* Bit 13: Parallel port control       */

/* ── Memory state structure ───────────────────────────────────────────────── */

typedef struct ms0515_memory {
    /*
     * RAM storage: 128 KB total, organized as 16 banks of 8 KB.
     * Banks 0–7 are "primary", banks 8–15 are "extended" (secondary).
     * Index: ram[bank * MEM_BANK_SIZE ... (bank+1) * MEM_BANK_SIZE - 1]
     */
    uint8_t  ram[MEM_RAM_SIZE];

    /* ROM storage: 16 KB */
    uint8_t  rom[MEM_ROM_SIZE];

    /* Video RAM: 16 KB, physically overlaps with bank 7 area */
    uint8_t  vram[MEM_VRAM_SIZE];

    /* Memory Dispatcher register (address 0177400) */
    uint16_t dispatcher;

    /* Extended ROM flag — mirrors System Register A bit 7 */
    bool     rom_extended;
} ms0515_memory_t;

/* ── Translation result ───────────────────────────────────────────────────── */

typedef struct {
    mem_addr_type_t type;       /* Where does this address land?             */
    uint32_t        offset;     /* Byte offset into the relevant storage     */
} mem_translation_t;

/* ── Public API ───────────────────────────────────────────────────────────── */

/*
 * mem_init — Zero-fill all RAM/VRAM, clear dispatcher register.
 */
void mem_init(ms0515_memory_t *mem);

/*
 * mem_load_rom — Copy a ROM image into the ROM buffer.
 *
 * `data` must point to `size` bytes (up to MEM_ROM_SIZE).
 */
void mem_load_rom(ms0515_memory_t *mem, const uint8_t *data, uint32_t size);

/*
 * mem_translate — Translate a 16-bit CPU address to physical storage.
 *
 * Accounts for bank switching, VRAM window, ROM overlay, and I/O space.
 * Returns the address type and byte offset.
 */
mem_translation_t mem_translate(const ms0515_memory_t *mem, uint16_t address);

/*
 * mem_read_byte / mem_read_word — Read from translated memory.
 * mem_write_byte / mem_write_word — Write to translated memory.
 *
 * These operate on the physical storage arrays directly, using the
 * result of mem_translate().  I/O addresses are NOT handled here —
 * the caller (board.c) must intercept ADDR_TYPE_IO and forward to
 * the appropriate device.
 */
uint8_t  mem_read_byte(const ms0515_memory_t *mem, mem_translation_t tr);
uint16_t mem_read_word(const ms0515_memory_t *mem, mem_translation_t tr);
void     mem_write_byte(ms0515_memory_t *mem, mem_translation_t tr, uint8_t val);
void     mem_write_word(ms0515_memory_t *mem, mem_translation_t tr, uint16_t val);

/*
 * mem_get_vram — Direct pointer to VRAM for the video renderer.
 */
const uint8_t *mem_get_vram(const ms0515_memory_t *mem);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_MEMORY_H */
