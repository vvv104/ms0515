/*
 * hd.c — Paravirtual hard-disk device (HD:)
 *
 * See hd.h for the full register/command protocol and a discussion of the
 * controller variants.  This module models the "t2" variant: synchronous
 * programmed DMA, no interrupts.  A command written to HDCSR is executed
 * immediately, so the driver's "wait until ready" spin always sees ready.
 */

#include <ms0515/core/hd.h>
#include <ms0515/core/memory.h>

#include <stdlib.h>
#include <string.h>

/* How long (in CPU cycles) the activity LED lingers after a transfer.
 * Matches floppy.c's ACTIVITY_DECAY_CYCLES so both lamps fade alike. */
#define HD_ACTIVITY_DECAY_CYCLES   750000

/* ── Lifecycle ──────────────────────────────────────────────────────────── */

void hd_init(ms0515_hd_t *hd)
{
    memset(hd, 0, sizeof(*hd));
    hd->status = HD_CS_READY;
}

bool hd_mount(ms0515_hd_t *hd, const uint8_t *data, uint32_t size)
{
    if (size == 0 || (size % HD_BLOCK_SIZE) != 0)
        return false;

    hd_unmount(hd);

    hd->image = (uint8_t *)malloc(size);
    if (!hd->image)
        return false;

    if (data)
        memcpy(hd->image, data, size);
    else
        memset(hd->image, 0, size);

    hd->image_size = size;
    hd->dirty      = false;
    hd->enabled    = true;
    hd_reset(hd);
    return true;
}

void hd_set_enabled(ms0515_hd_t *hd, bool enabled)
{
    hd->enabled = enabled;
}

void hd_unmount(ms0515_hd_t *hd)
{
    free(hd->image);
    hd->image      = NULL;
    hd->image_size = 0;
    hd->dirty      = false;
    /* The controller stays enabled — the drive is now empty, not removed. */
}

void hd_reset(ms0515_hd_t *hd)
{
    hd->unit     = 0;
    hd->block    = 0;
    hd->bufaddr  = 0;
    hd->addr_hi  = 0;
    hd->wcount   = 0;
    hd->data_reg = 0;
    hd->status   = HD_CS_READY;
    hd->activity_remaining = 0;
    /* Image contents are preserved across a controller reset. */
}

/* ── I/O routing ────────────────────────────────────────────────────────── */

bool hd_handles(uint16_t offset)
{
    return offset == HD_IO_CSR    || offset == HD_IO_CSR_HI ||
           offset == HD_IO_DATA   || offset == (HD_IO_DATA + 1);
}

/* ── Command execution ──────────────────────────────────────────────────── */

/* Block count of the selected unit (0 = offline).  Only unit 0 is backed. */
static uint32_t hd_unit_blocks(const ms0515_hd_t *hd)
{
    if (hd->unit != 0 || !hd->image)
        return 0;
    uint32_t blocks = hd->image_size / HD_BLOCK_SIZE;
    return blocks > HD_MAX_BLOCKS ? HD_MAX_BLOCKS : blocks;
}

/* Move `hd->wcount` words between the image (at `hd->block`) and CPU memory
 * (at `hd->bufaddr`).  Sets HD_CS_ERROR if the image range is out of bounds. */
static void hd_transfer(ms0515_hd_t *hd, ms0515_memory_t *mem, bool writing)
{
    uint32_t bytes = (uint32_t)hd->wcount * 2u;
    uint32_t base  = hd->block * (uint32_t)HD_BLOCK_SIZE;

    if (!hd->image || base > hd->image_size || bytes > hd->image_size - base) {
        hd->status = HD_CS_READY | HD_CS_ERROR;
        return;
    }

    for (uint16_t w = 0; w < hd->wcount; ++w) {
        uint32_t off  = base + (uint32_t)w * 2u;
        uint16_t addr = (uint16_t)(hd->bufaddr + (uint32_t)w * 2u);
        mem_translation_t tr = mem_translate(mem, addr);

        if (writing) {
            uint16_t word = mem_read_word(mem, tr);
            hd->image[off]     = (uint8_t)(word & 0xFF);
            hd->image[off + 1] = (uint8_t)(word >> 8);
        } else {
            uint16_t word = (uint16_t)hd->image[off] |
                            (uint16_t)((uint16_t)hd->image[off + 1] << 8);
            mem_write_word(mem, tr, word);
        }
    }

    if (writing)
        hd->dirty = true;
    hd->status = HD_CS_READY;
    hd->activity_remaining = HD_ACTIVITY_DECAY_CYCLES;
}

/* Execute the command written to HDCSR, using the latched argument `arg`
 * (the value most recently written to HDDAT, extended with addr_hi). */
static void hd_execute(ms0515_hd_t *hd, ms0515_memory_t *mem, uint16_t cmd,
                       uint16_t arg)
{
    hd->status = HD_CS_READY;

    switch (cmd & 0xFF) {
    case HD_CMD_SET_UNIT:
        hd->unit = (uint16_t)(arg & 7);
        break;
    case HD_CMD_SET_BLOCK:
        hd->block = arg;
        break;
    case HD_CMD_SET_BUF:
        hd->bufaddr = (uint32_t)arg | ((uint32_t)hd->addr_hi << 16);
        hd->addr_hi = 0;
        break;
    case HD_CMD_SET_WCNT:
        hd->wcount = arg;
        break;
    case HD_CMD_READ:
        hd_transfer(hd, mem, false);
        break;
    case HD_CMD_WRITE:
        hd_transfer(hd, mem, true);
        break;
    case HD_CMD_GET_SIZE:
        hd->data_reg = (uint16_t)hd_unit_blocks(hd);
        break;
    default:
        hd->status = HD_CS_READY | HD_CS_ERROR;
        break;
    }
}

/* ── Register access ────────────────────────────────────────────────────── */

uint16_t hd_read_word(ms0515_hd_t *hd, uint16_t offset)
{
    if (offset == HD_IO_CSR)
        return hd->status;
    if (offset == HD_IO_DATA)
        return hd->data_reg;
    return 0;
}

uint8_t hd_read_byte(ms0515_hd_t *hd, uint16_t offset)
{
    if (offset == HD_IO_CSR)
        return (uint8_t)(hd->status & 0xFF);
    if (offset == HD_IO_CSR_HI)
        return (uint8_t)(hd->status >> 8);
    if (offset == HD_IO_DATA)
        return (uint8_t)(hd->data_reg & 0xFF);
    if (offset == HD_IO_DATA + 1)
        return (uint8_t)(hd->data_reg >> 8);
    return 0;
}

void hd_write_word(ms0515_hd_t *hd, ms0515_memory_t *mem,
                   uint16_t offset, uint16_t value)
{
    if (offset == HD_IO_DATA) {
        hd->data_reg = value;       /* latch the command argument */
        return;
    }
    if (offset == HD_IO_CSR) {
        hd_execute(hd, mem, value, hd->data_reg);
        return;
    }
    if (offset == HD_IO_CSR_HI)
        hd->addr_hi = (uint8_t)(value & 0xFF);
}

void hd_write_byte(ms0515_hd_t *hd, ms0515_memory_t *mem,
                   uint16_t offset, uint8_t value)
{
    if (offset == HD_IO_DATA) {
        hd->data_reg = (uint16_t)((hd->data_reg & 0xFF00) | value);
        return;
    }
    if (offset == HD_IO_DATA + 1) {
        hd->data_reg = (uint16_t)((hd->data_reg & 0x00FF) |
                                  ((uint16_t)value << 8));
        return;
    }
    if (offset == HD_IO_CSR_HI) {
        hd->addr_hi = value;        /* DMA buffer high byte (XM builds) */
        return;
    }
    if (offset == HD_IO_CSR)
        hd_execute(hd, mem, value, hd->data_reg);
}

/* ── Timing ─────────────────────────────────────────────────────────────── */

void hd_tick(ms0515_hd_t *hd, int cycles)
{
    if (hd->activity_remaining > cycles)
        hd->activity_remaining -= cycles;
    else
        hd->activity_remaining = 0;
}
