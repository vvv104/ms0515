/*
 * memory.c — MS0515 address space and bank switching implementation
 *
 * See memory.h for the full memory map description.
 *
 * Address translation algorithm:
 *   1. If address >= 0177400 → I/O space
 *   2. If address >= 0160000 → ROM (or extended ROM from 0140000)
 *   3. If VRAM access is enabled (dispatcher bit 7) and the address
 *      falls within the virtual window → VRAM
 *   4. Otherwise → RAM bank, selected by dispatcher bits 0–6
 *      (0 = extended bank, 1 = primary bank)
 */

#include <ms0515/memory.h>
#include <string.h>
#include <assert.h>

/* ── Initialization ───────────────────────────────────────────────────────── */

void mem_init(ms0515_memory_t *mem)
{
    memset(mem->ram,  0, sizeof(mem->ram));
    memset(mem->rom,  0, sizeof(mem->rom));
    memset(mem->vram, 0, sizeof(mem->vram));
    mem->dispatcher   = 0x007F;   /* All primary banks selected by default */
    mem->rom_extended = false;
}

void mem_load_rom(ms0515_memory_t *mem, const uint8_t *data, uint32_t size)
{
    assert(size <= MEM_ROM_SIZE);
    memcpy(mem->rom, data, size);
}

/* ── Address translation ──────────────────────────────────────────────────── */

/*
 * Determine which VRAM window is active based on dispatcher bits 10-11.
 *
 * Returns the start address of the window in CPU address space:
 *   bits 11:10 = 00 → window at 000000–037777
 *   bits 11:10 = 01 → window at 040000–077777
 *   bits 11:10 = 1x → window at 100000–137777
 */
static uint16_t vram_window_start(uint16_t dispatcher)
{
    int win = (dispatcher >> 10) & 3;
    switch (win) {
    case 0:  return 000000;
    case 1:  return 040000;
    default: return 0100000;  /* win == 2 or 3 */
    }
}

static uint16_t vram_window_end(uint16_t dispatcher)
{
    int win = (dispatcher >> 10) & 3;
    switch (win) {
    case 0:  return 037777;
    case 1:  return 077777;
    default: return 0137777;
    }
}

mem_translation_t mem_translate(const ms0515_memory_t *mem, uint16_t address)
{
    mem_translation_t result;

    /* 1. I/O register space: 0177400–0177776 */
    if (address >= 0177400) {
        result.type   = ADDR_TYPE_IO;
        result.offset = address - 0177400;
        return result;
    }

    /* 2. ROM space */
    if (mem->rom_extended) {
        /* Extended ROM: 0140000–0177377 (16 KB) */
        if (address >= 0140000) {
            result.type   = ADDR_TYPE_ROM;
            result.offset = address - 0140000;
            return result;
        }
    } else {
        /* Default ROM: 0160000–0177377 (8 KB) */
        if (address >= 0160000) {
            result.type   = ADDR_TYPE_ROM;
            /* Map to upper 8 KB of ROM image */
            result.offset = (address - 0160000) + MEM_BANK_SIZE;
            return result;
        }
    }

    /* 3. VRAM access through virtual window */
    if (mem->dispatcher & MEM_DISP_VRAM_EN) {
        uint16_t win_start = vram_window_start(mem->dispatcher);
        uint16_t win_end   = vram_window_end(mem->dispatcher);

        if (address >= win_start && address <= win_end) {
            result.type   = ADDR_TYPE_VRAM;
            result.offset = address - win_start;
            /* Clamp to VRAM size (16 KB) */
            if (result.offset >= MEM_VRAM_SIZE)
                result.offset %= MEM_VRAM_SIZE;
            return result;
        }
    }

    /* 4. RAM — determine bank and primary/extended selection */
    {
        int bank = address >> 13;             /* address / 8192 → bank 0–7 */
        uint16_t offset_in_bank = address & (MEM_BANK_SIZE - 1);

        /* Check dispatcher bit for this bank (1 = primary, 0 = extended) */
        bool primary = (mem->dispatcher & (1 << bank)) != 0;
        int phys_bank = primary ? bank : (bank + MEM_BANK_COUNT);

        result.type   = ADDR_TYPE_RAM;
        result.offset = (uint32_t)phys_bank * MEM_BANK_SIZE + offset_in_bank;
        return result;
    }
}

/* ── Memory read/write ────────────────────────────────────────────────────── */

uint8_t mem_read_byte(const ms0515_memory_t *mem, mem_translation_t tr)
{
    switch (tr.type) {
    case ADDR_TYPE_RAM:
        assert(tr.offset < MEM_RAM_SIZE);
        return mem->ram[tr.offset];
    case ADDR_TYPE_ROM:
        assert(tr.offset < MEM_ROM_SIZE);
        return mem->rom[tr.offset];
    case ADDR_TYPE_VRAM:
        assert(tr.offset < MEM_VRAM_SIZE);
        return mem->vram[tr.offset];
    default:
        return 0;   /* I/O and DENIED — caller handles these */
    }
}

uint16_t mem_read_word(const ms0515_memory_t *mem, mem_translation_t tr)
{
    /* PDP-11 is little-endian */
    uint8_t lo = mem_read_byte(mem, tr);
    tr.offset++;
    uint8_t hi = mem_read_byte(mem, tr);
    return (uint16_t)lo | ((uint16_t)hi << 8);
}

void mem_write_byte(ms0515_memory_t *mem, mem_translation_t tr, uint8_t val)
{
    switch (tr.type) {
    case ADDR_TYPE_RAM:
        assert(tr.offset < MEM_RAM_SIZE);
        mem->ram[tr.offset] = val;
        break;
    case ADDR_TYPE_VRAM:
        assert(tr.offset < MEM_VRAM_SIZE);
        mem->vram[tr.offset] = val;
        break;
    case ADDR_TYPE_ROM:
        /* Writes to ROM are silently ignored */
        break;
    default:
        break;
    }
}

void mem_write_word(ms0515_memory_t *mem, mem_translation_t tr, uint16_t val)
{
    mem_write_byte(mem, tr, (uint8_t)(val & 0xFF));
    tr.offset++;
    mem_write_byte(mem, tr, (uint8_t)(val >> 8));
}

const uint8_t *mem_get_vram(const ms0515_memory_t *mem)
{
    return mem->vram;
}
