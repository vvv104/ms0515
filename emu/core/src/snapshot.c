/*
 * snapshot.c — Machine state snapshot serialization.
 *
 * Binary format:
 *   [Header 16 bytes] [Chunk]* [END chunk]
 *
 * Header:
 *   4 bytes  magic "MS05"
 *   2 bytes  format version (LE)
 *   2 bytes  flags (reserved, 0)
 *   4 bytes  ROM CRC32 (LE)
 *   4 bytes  reserved (0)
 *
 * Each chunk:
 *   4 bytes  chunk ID (ASCII)
 *   4 bytes  data size (LE)
 *   N bytes  data
 */

#include <ms0515/snapshot.h>
#include <string.h>
#include <stdlib.h>

#define SNAP_MAGIC      "MS05"
#define SNAP_VERSION    1

/* ── I/O helpers ─────────────────────────────────────────────────────────── */

static bool write_u8(snap_io_t *io, uint8_t v)
{
    return io->write(io->ctx, &v, 1);
}

static bool write_u16(snap_io_t *io, uint16_t v)
{
    uint8_t buf[2] = { (uint8_t)(v & 0xFF), (uint8_t)(v >> 8) };
    return io->write(io->ctx, buf, 2);
}

static bool write_u32(snap_io_t *io, uint32_t v)
{
    uint8_t buf[4] = {
        (uint8_t)(v),       (uint8_t)(v >> 8),
        (uint8_t)(v >> 16), (uint8_t)(v >> 24)
    };
    return io->write(io->ctx, buf, 4);
}

static bool write_i32(snap_io_t *io, int32_t v)
{
    return write_u32(io, (uint32_t)v);
}

static bool write_u64(snap_io_t *io, uint64_t v)
{
    return write_u32(io, (uint32_t)(v & 0xFFFFFFFFu))
        && write_u32(io, (uint32_t)(v >> 32));
}

static bool write_bytes(snap_io_t *io, const void *data, size_t n)
{
    return io->write(io->ctx, data, n);
}

static bool read_u8(snap_io_t *io, uint8_t *v)
{
    return io->read(io->ctx, v, 1);
}

static bool read_u16(snap_io_t *io, uint16_t *v)
{
    uint8_t buf[2];
    if (!io->read(io->ctx, buf, 2)) return false;
    *v = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return true;
}

static bool read_u32(snap_io_t *io, uint32_t *v)
{
    uint8_t buf[4];
    if (!io->read(io->ctx, buf, 4)) return false;
    *v = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
         ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    return true;
}

static bool read_i32(snap_io_t *io, int32_t *v)
{
    uint32_t u;
    if (!read_u32(io, &u)) return false;
    *v = (int32_t)u;
    return true;
}

static bool read_u64(snap_io_t *io, uint64_t *v)
{
    uint32_t lo, hi;
    if (!read_u32(io, &lo) || !read_u32(io, &hi)) return false;
    *v = ((uint64_t)hi << 32) | lo;
    return true;
}

static bool read_bytes(snap_io_t *io, void *data, size_t n)
{
    return io->read(io->ctx, data, n);
}

static bool write_bool(snap_io_t *io, bool v)
{
    return write_u8(io, v ? 1 : 0);
}

static bool read_bool(snap_io_t *io, bool *v)
{
    uint8_t u;
    if (!read_u8(io, &u)) return false;
    *v = u != 0;
    return true;
}

/* ── Chunk I/O ───────────────────────────────────────────────────────────── */

static bool write_chunk_hdr(snap_io_t *io, const char id[4], uint32_t size)
{
    return write_bytes(io, id, 4) && write_u32(io, size);
}

static bool read_chunk_hdr(snap_io_t *io, char id[4], uint32_t *size)
{
    return read_bytes(io, id, 4) && read_u32(io, size);
}

/* ── CRC32 (ISO 3309 / ITU-T V.42) ─────────────────────────────────────── */

uint32_t snap_crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ── CPU chunk ───────────────────────────────────────────────────────────── */

static bool write_cpu(snap_io_t *f, const ms0515_cpu_t *cpu)
{
    for (int i = 0; i < 8; i++)
        if (!write_u16(f, cpu->r[i])) return false;
    if (!write_u16(f, cpu->psw)) return false;
    if (!write_u16(f, cpu->instruction)) return false;
    if (!write_u16(f, cpu->instruction_pc)) return false;
    if (!write_bool(f, cpu->halted)) return false;
    if (!write_bool(f, cpu->waiting)) return false;
    if (!write_bool(f, cpu->irq_halt)) return false;
    if (!write_bool(f, cpu->irq_bus_error)) return false;
    if (!write_bool(f, cpu->irq_reserved)) return false;
    if (!write_bool(f, cpu->irq_bpt)) return false;
    if (!write_bool(f, cpu->irq_iot)) return false;
    if (!write_bool(f, cpu->irq_emt)) return false;
    if (!write_bool(f, cpu->irq_trap)) return false;
    if (!write_bool(f, cpu->irq_tbit)) return false;
    for (int i = 0; i < 16; i++)
        if (!write_bool(f, cpu->irq_virq[i])) return false;
    for (int i = 0; i < 16; i++)
        if (!write_u16(f, cpu->irq_virq_vec[i])) return false;
    if (!write_i32(f, cpu->cycles)) return false;
    return true;
}

static bool read_cpu(snap_io_t *f, ms0515_cpu_t *cpu)
{
    for (int i = 0; i < 8; i++)
        if (!read_u16(f, &cpu->r[i])) return false;
    if (!read_u16(f, &cpu->psw)) return false;
    if (!read_u16(f, &cpu->instruction)) return false;
    if (!read_u16(f, &cpu->instruction_pc)) return false;
    if (!read_bool(f, &cpu->halted)) return false;
    if (!read_bool(f, &cpu->waiting)) return false;
    if (!read_bool(f, &cpu->irq_halt)) return false;
    if (!read_bool(f, &cpu->irq_bus_error)) return false;
    if (!read_bool(f, &cpu->irq_reserved)) return false;
    if (!read_bool(f, &cpu->irq_bpt)) return false;
    if (!read_bool(f, &cpu->irq_iot)) return false;
    if (!read_bool(f, &cpu->irq_emt)) return false;
    if (!read_bool(f, &cpu->irq_trap)) return false;
    if (!read_bool(f, &cpu->irq_tbit)) return false;
    for (int i = 0; i < 16; i++)
        if (!read_bool(f, &cpu->irq_virq[i])) return false;
    for (int i = 0; i < 16; i++)
        if (!read_u16(f, &cpu->irq_virq_vec[i])) return false;
    if (!read_i32(f, &cpu->cycles)) return false;
    return true;
}

#define CPU_CHUNK_SIZE (8*2 + 3*2 + 2 + 8 + 16 + 16*2 + 4)

/* ── Memory chunk ────────────────────────────────────────────────────────── */

#define MEM_CHUNK_SIZE (2 + 1 + MEM_RAM_SIZE + MEM_VRAM_SIZE)

static bool write_mem(snap_io_t *f, const ms0515_memory_t *mem)
{
    if (!write_u16(f, mem->dispatcher)) return false;
    if (!write_bool(f, mem->rom_extended)) return false;
    if (!write_bytes(f, mem->ram, MEM_RAM_SIZE)) return false;
    if (!write_bytes(f, mem->vram, MEM_VRAM_SIZE)) return false;
    return true;
}

static bool read_mem(snap_io_t *f, ms0515_memory_t *mem)
{
    if (!read_u16(f, &mem->dispatcher)) return false;
    if (!read_bool(f, &mem->rom_extended)) return false;
    if (!read_bytes(f, mem->ram, MEM_RAM_SIZE)) return false;
    if (!read_bytes(f, mem->vram, MEM_VRAM_SIZE)) return false;
    return true;
}

/* ── Timer chunk ─────────────────────────────────────────────────────────── */

static bool write_timer_ch(snap_io_t *f, const timer_channel_t *ch)
{
    if (!write_u16(f, ch->count)) return false;
    if (!write_u16(f, ch->reload)) return false;
    if (!write_u16(f, ch->latch)) return false;
    if (!write_bool(f, ch->latched)) return false;
    if (!write_u8(f, ch->mode)) return false;
    if (!write_u8(f, ch->rw_mode)) return false;
    if (!write_bool(f, ch->bcd)) return false;
    if (!write_bool(f, ch->write_lsb_next)) return false;
    if (!write_bool(f, ch->read_lsb_next)) return false;
    if (!write_bool(f, ch->gate)) return false;
    if (!write_bool(f, ch->out)) return false;
    if (!write_bool(f, ch->counting)) return false;
    if (!write_bool(f, ch->loaded)) return false;
    return true;
}

static bool read_timer_ch(snap_io_t *f, timer_channel_t *ch)
{
    if (!read_u16(f, &ch->count)) return false;
    if (!read_u16(f, &ch->reload)) return false;
    if (!read_u16(f, &ch->latch)) return false;
    if (!read_bool(f, &ch->latched)) return false;
    if (!read_u8(f, &ch->mode)) return false;
    if (!read_u8(f, &ch->rw_mode)) return false;
    if (!read_bool(f, &ch->bcd)) return false;
    if (!read_bool(f, &ch->write_lsb_next)) return false;
    if (!read_bool(f, &ch->read_lsb_next)) return false;
    if (!read_bool(f, &ch->gate)) return false;
    if (!read_bool(f, &ch->out)) return false;
    if (!read_bool(f, &ch->counting)) return false;
    if (!read_bool(f, &ch->loaded)) return false;
    return true;
}

#define TIMER_CH_SIZE (3*2 + 10)
#define TIMER_CHUNK_SIZE (3 * TIMER_CH_SIZE)

/* ── Keyboard (USART) chunk ──────────────────────────────────────────────── */

#define KBD_CHUNK_SIZE (5 + 2 + 4 + 16 + 3*4 + 1)

static bool write_kbd(snap_io_t *f, const ms0515_keyboard_t *kbd)
{
    if (!write_u8(f, kbd->rx_data)) return false;
    if (!write_u8(f, kbd->tx_data)) return false;
    if (!write_u8(f, kbd->status)) return false;
    if (!write_u8(f, kbd->mode)) return false;
    if (!write_u8(f, kbd->command)) return false;
    if (!write_bool(f, kbd->rx_ready)) return false;
    if (!write_bool(f, kbd->tx_ready)) return false;
    if (!write_i32(f, kbd->init_step)) return false;
    if (!write_bytes(f, kbd->fifo, 16)) return false;
    if (!write_i32(f, kbd->fifo_head)) return false;
    if (!write_i32(f, kbd->fifo_tail)) return false;
    if (!write_i32(f, kbd->fifo_count)) return false;
    if (!write_bool(f, kbd->irq)) return false;
    return true;
}

static bool read_kbd(snap_io_t *f, ms0515_keyboard_t *kbd)
{
    if (!read_u8(f, &kbd->rx_data)) return false;
    if (!read_u8(f, &kbd->tx_data)) return false;
    if (!read_u8(f, &kbd->status)) return false;
    if (!read_u8(f, &kbd->mode)) return false;
    if (!read_u8(f, &kbd->command)) return false;
    if (!read_bool(f, &kbd->rx_ready)) return false;
    if (!read_bool(f, &kbd->tx_ready)) return false;
    if (!read_i32(f, &kbd->init_step)) return false;
    if (!read_bytes(f, kbd->fifo, 16)) return false;
    if (!read_i32(f, &kbd->fifo_head)) return false;
    if (!read_i32(f, &kbd->fifo_tail)) return false;
    if (!read_i32(f, &kbd->fifo_count)) return false;
    if (!read_bool(f, &kbd->irq)) return false;
    return true;
}

/* ── FDC chunk ───────────────────────────────────────────────────────────── */

/* Per-drive: track(4) + read_only(1) + motor_on(1) = 6 bytes
 * FDC core: selected(4) + 5 regs + 3 flags + 512 buf + 2*4 pos/len
 *         + pending_finish(1) + busy_delay(4) = 533
 * Total: 533 + 4*6 = 557 */
#define FDC_CHUNK_SIZE (4 + 5 + 3 + FDC_SECTOR_SIZE + 2*4 + 1 + 4 + 4*6)

static bool write_fdc(snap_io_t *f, const ms0515_floppy_t *fdc)
{
    if (!write_i32(f, fdc->selected)) return false;
    if (!write_u8(f, fdc->status)) return false;
    if (!write_u8(f, fdc->command)) return false;
    if (!write_u8(f, fdc->track_reg)) return false;
    if (!write_u8(f, fdc->sector_reg)) return false;
    if (!write_u8(f, fdc->data_reg)) return false;
    if (!write_bool(f, fdc->drq)) return false;
    if (!write_bool(f, fdc->intrq)) return false;
    if (!write_bool(f, fdc->busy)) return false;
    if (!write_bytes(f, fdc->buffer, FDC_SECTOR_SIZE)) return false;
    if (!write_i32(f, fdc->buf_pos)) return false;
    if (!write_i32(f, fdc->buf_len)) return false;
    if (!write_bool(f, fdc->pending_finish)) return false;
    if (!write_i32(f, fdc->busy_delay)) return false;
    for (int i = 0; i < FDC_LOGICAL_UNITS; i++) {
        if (!write_i32(f, fdc->drives[i].track)) return false;
        if (!write_bool(f, fdc->drives[i].read_only)) return false;
        if (!write_bool(f, fdc->drives[i].motor_on)) return false;
    }
    return true;
}

static bool read_fdc(snap_io_t *f, ms0515_floppy_t *fdc)
{
    if (!read_i32(f, &fdc->selected)) return false;
    if (!read_u8(f, &fdc->status)) return false;
    if (!read_u8(f, &fdc->command)) return false;
    if (!read_u8(f, &fdc->track_reg)) return false;
    if (!read_u8(f, &fdc->sector_reg)) return false;
    if (!read_u8(f, &fdc->data_reg)) return false;
    if (!read_bool(f, &fdc->drq)) return false;
    if (!read_bool(f, &fdc->intrq)) return false;
    if (!read_bool(f, &fdc->busy)) return false;
    if (!read_bytes(f, fdc->buffer, FDC_SECTOR_SIZE)) return false;
    if (!read_i32(f, &fdc->buf_pos)) return false;
    if (!read_i32(f, &fdc->buf_len)) return false;
    if (!read_bool(f, &fdc->pending_finish)) return false;
    if (!read_i32(f, &fdc->busy_delay)) return false;
    for (int i = 0; i < FDC_LOGICAL_UNITS; i++) {
        if (!read_i32(f, &fdc->drives[i].track)) return false;
        if (!read_bool(f, &fdc->drives[i].read_only)) return false;
        if (!read_bool(f, &fdc->drives[i].motor_on)) return false;
    }
    return true;
}

/* ── Board chunk ─────────────────────────────────────────────────────────── */

#define BRD_CHUNK_SIZE (4 + 1 + 1 + 1 + 4 + 4 + 1 + 4 + 4 + 4)

static bool write_brd(snap_io_t *f, const ms0515_board_t *b)
{
    if (!write_u8(f, b->reg_a)) return false;
    if (!write_u8(f, b->reg_b)) return false;
    if (!write_u8(f, b->reg_c)) return false;
    if (!write_u8(f, b->ppi_control)) return false;
    if (!write_bool(f, b->hires_mode)) return false;
    if (!write_u8(f, b->border_color)) return false;
    if (!write_bool(f, b->sound_on)) return false;
    if (!write_i32(f, b->sound_value)) return false;
    if (!write_i32(f, b->frame_cycle_pos)) return false;
    if (!write_u8(f, b->dip_refresh)) return false;
    if (!write_i32(f, b->timer_counter)) return false;
    if (!write_i32(f, b->frame_counter)) return false;
    if (!write_u32(f, b->tape_bit_counter)) return false;
    return true;
}

static bool read_brd(snap_io_t *f, ms0515_board_t *b)
{
    if (!read_u8(f, &b->reg_a)) return false;
    if (!read_u8(f, &b->reg_b)) return false;
    if (!read_u8(f, &b->reg_c)) return false;
    if (!read_u8(f, &b->ppi_control)) return false;
    if (!read_bool(f, &b->hires_mode)) return false;
    if (!read_u8(f, &b->border_color)) return false;
    if (!read_bool(f, &b->sound_on)) return false;
    if (!read_i32(f, &b->sound_value)) return false;
    if (!read_i32(f, &b->frame_cycle_pos)) return false;
    if (!read_u8(f, &b->dip_refresh)) return false;
    if (!read_i32(f, &b->timer_counter)) return false;
    if (!read_i32(f, &b->frame_counter)) return false;
    if (!read_u32(f, &b->tape_bit_counter)) return false;
    return true;
}

/* ── History event ring chunk ────────────────────────────────────────────── */

/* HIST layout:
 *   u32 version  (= 1)
 *   u32 cap      (ring size in events)
 *   u32 head     (next write slot, raw)
 *   u32 reserved (0)
 *   u64 written  (total pushes; reader uses it + head to find oldest)
 *   cap × 16 bytes  (events in raw wire form)
 */
#define HIST_VERSION      1
#define HIST_HEADER_SIZE  (4 + 4 + 4 + 4 + 8)

static uint32_t history_chunk_size(const ms0515_event_ring_t *r)
{
    if (!r->cap) return 0;
    return (uint32_t)(HIST_HEADER_SIZE + r->cap * sizeof(ms0515_event_t));
}

static bool write_history(snap_io_t *f, const ms0515_event_ring_t *r)
{
    if (!write_u32(f, HIST_VERSION))          return false;
    if (!write_u32(f, (uint32_t)r->cap))      return false;
    if (!write_u32(f, (uint32_t)r->head))     return false;
    if (!write_u32(f, 0))                     return false;
    if (!write_u64(f, r->written))            return false;
    if (!write_bytes(f, r->events,
                     r->cap * sizeof(ms0515_event_t)))
        return false;
    return true;
}

static bool read_history(snap_io_t *f, ms0515_event_ring_t *r,
                         uint32_t chunk_size)
{
    uint32_t version, cap, head, reserved;
    uint64_t written;

    if (chunk_size < HIST_HEADER_SIZE) return false;
    if (!read_u32(f, &version) || version != HIST_VERSION) return false;
    if (!read_u32(f, &cap))            return false;
    if (!read_u32(f, &head))           return false;
    if (!read_u32(f, &reserved))       return false;
    if (!read_u64(f, &written))        return false;

    uint32_t expected = (uint32_t)(HIST_HEADER_SIZE + cap * sizeof(ms0515_event_t));
    if (chunk_size != expected) return false;

    ms0515_event_ring_resize(r, cap);
    if (cap && !r->events) return false;   /* allocation failed */

    if (cap) {
        if (!read_bytes(f, r->events,
                        cap * sizeof(ms0515_event_t)))
            return false;
    }
    if (cap && head >= cap) head = 0;      /* guard against corrupt data */
    r->head    = head;
    r->written = written;
    return true;
}

/* ── RAM disk chunk ──────────────────────────────────────────────────────── */

static bool write_ramdisk(snap_io_t *f, const ms0515_ramdisk_t *rd)
{
    if (!write_bool(f, rd->enabled)) return false;
    if (!write_u8(f, rd->ppi_a)) return false;
    if (!write_u8(f, rd->ppi_b)) return false;
    if (!write_u8(f, rd->ppi_c)) return false;
    if (!write_u8(f, rd->ppi_ctrl)) return false;
    if (!write_bool(f, rd->port_a_input)) return false;
    if (!write_bool(f, rd->port_b_input)) return false;
    if (!write_u8(f, rd->counter)) return false;
    if (rd->enabled && rd->ram) {
        if (!write_bytes(f, rd->ram, RAMDISK_SIZE)) return false;
    }
    return true;
}

static uint32_t ramdisk_chunk_size(const ms0515_ramdisk_t *rd)
{
    uint32_t sz = 1 + 4 + 2 + 1;  /* enabled + ppi regs + port flags + counter */
    if (rd->enabled && rd->ram)
        sz += RAMDISK_SIZE;
    return sz;
}

static bool read_ramdisk(snap_io_t *f, ms0515_ramdisk_t *rd, uint32_t chunk_size)
{
    if (!read_bool(f, &rd->enabled)) return false;
    if (!read_u8(f, &rd->ppi_a)) return false;
    if (!read_u8(f, &rd->ppi_b)) return false;
    if (!read_u8(f, &rd->ppi_c)) return false;
    if (!read_u8(f, &rd->ppi_ctrl)) return false;
    if (!read_bool(f, &rd->port_a_input)) return false;
    if (!read_bool(f, &rd->port_b_input)) return false;
    if (!read_u8(f, &rd->counter)) return false;

    uint32_t header_sz = 1 + 4 + 2 + 1;
    if (chunk_size > header_sz) {
        /* RAM disk data is present */
        if (rd->enabled) {
            if (!rd->ram) {
                rd->ram = (uint8_t *)malloc(RAMDISK_SIZE);
                if (!rd->ram) return false;
            }
            if (!read_bytes(f, rd->ram, RAMDISK_SIZE)) return false;
        }
    }
    return true;
}

/* ── MS7004 keyboard controller chunk ────────────────────────────────────── */

static bool write_ms7004(snap_io_t *f, const ms7004_t *k)
{
    if (!write_u16(f, (uint16_t)MS7004_KEY__COUNT)) return false;
    if (!write_bytes(f, k->held, MS7004_KEY__COUNT)) return false;
    if (!write_i32(f, k->held_count)) return false;
    if (!write_bool(f, k->caps_on)) return false;
    if (!write_bool(f, k->ruslat_on)) return false;
    if (!write_i32(f, (int32_t)k->repeat_key)) return false;
    if (!write_u32(f, k->repeat_next_ms)) return false;
    if (!write_u32(f, k->repeat_delay_ms)) return false;
    if (!write_u32(f, k->repeat_period_ms)) return false;
    if (!write_bool(f, k->repeat_enabled)) return false;
    for (int i = 0; i < 8; i++)
        if (!write_i32(f, (int32_t)k->key_stack[i])) return false;
    if (!write_i32(f, k->key_stack_top)) return false;
    if (!write_u32(f, k->now_ms)) return false;
    if (!write_u8(f, k->cmd_pending)) return false;
    if (!write_bool(f, k->data_enabled)) return false;
    if (!write_bool(f, k->sound_enabled)) return false;
    if (!write_bool(f, k->click_enabled)) return false;
    if (!write_bool(f, k->latin_indicator)) return false;
    return true;
}

static bool read_ms7004(snap_io_t *f, ms7004_t *k)
{
    uint16_t key_count;
    if (!read_u16(f, &key_count)) return false;

    /* Read held[] array — handle size changes gracefully */
    if (key_count <= MS7004_KEY__COUNT) {
        if (!read_bytes(f, k->held, key_count)) return false;
        memset(k->held + key_count, 0, MS7004_KEY__COUNT - key_count);
    } else {
        if (!read_bytes(f, k->held, MS7004_KEY__COUNT)) return false;
        /* Skip extra bytes from newer version */
        if (!f->seek(f->ctx, (long)(key_count - MS7004_KEY__COUNT)))
            return false;
    }

    int32_t i32;
    if (!read_i32(f, &k->held_count)) return false;
    if (!read_bool(f, &k->caps_on)) return false;
    if (!read_bool(f, &k->ruslat_on)) return false;
    if (!read_i32(f, &i32)) return false;
    k->repeat_key = (ms7004_key_t)i32;
    if (!read_u32(f, &k->repeat_next_ms)) return false;
    if (!read_u32(f, &k->repeat_delay_ms)) return false;
    if (!read_u32(f, &k->repeat_period_ms)) return false;
    if (!read_bool(f, &k->repeat_enabled)) return false;
    for (int i = 0; i < 8; i++) {
        if (!read_i32(f, &i32)) return false;
        k->key_stack[i] = (ms7004_key_t)i32;
    }
    if (!read_i32(f, &k->key_stack_top)) return false;
    if (!read_u32(f, &k->now_ms)) return false;
    if (!read_u8(f, &k->cmd_pending)) return false;
    if (!read_bool(f, &k->data_enabled)) return false;
    if (!read_bool(f, &k->sound_enabled)) return false;
    if (!read_bool(f, &k->click_enabled)) return false;
    if (!read_bool(f, &k->latin_indicator)) return false;
    return true;
}

#define MS7004_CHUNK_SIZE (2 + MS7004_KEY__COUNT + 4 + 2 + 4 + 3*4 + 1 + 8*4 + 4 + 4 + 1 + 4)

/* ── Disk paths chunk ────────────────────────────────────────────────────── */

static bool write_disk_paths(snap_io_t *f, const char *paths[4], uint32_t *size_out)
{
    uint32_t total = 0;
    for (int i = 0; i < 4; i++) {
        uint16_t len = 0;
        if (paths[i])
            len = (uint16_t)strlen(paths[i]);
        if (!write_u16(f, len)) return false;
        total += 2;
        if (len > 0) {
            if (!write_bytes(f, paths[i], len)) return false;
            total += len;
        }
    }
    *size_out = total;
    return true;
}

static bool read_disk_paths(snap_io_t *f, char *paths_out[4])
{
    for (int i = 0; i < 4; i++) {
        uint16_t len;
        if (!read_u16(f, &len)) return false;
        if (len > 0) {
            paths_out[i] = (char *)malloc(len + 1);
            if (!paths_out[i]) return false;
            if (!read_bytes(f, paths_out[i], len)) {
                free(paths_out[i]);
                paths_out[i] = NULL;
                return false;
            }
            paths_out[i][len] = '\0';
        } else {
            paths_out[i] = NULL;
        }
    }
    return true;
}

static uint32_t disk_paths_size(const char *paths[4])
{
    uint32_t total = 0;
    for (int i = 0; i < 4; i++) {
        total += 2;
        if (paths[i])
            total += (uint32_t)strlen(paths[i]);
    }
    return total;
}

/* ── Save ────────────────────────────────────────────────────────────────── */

snap_error_t snap_save(const ms0515_board_t *board,
                       const ms7004_t *kbd7004,
                       uint32_t rom_crc32,
                       const char *disk_paths[4],
                       snap_io_t *out)
{
    /* Header */
    if (!write_bytes(out, SNAP_MAGIC, 4)) return SNAP_ERR_IO;
    if (!write_u16(out, SNAP_VERSION))    return SNAP_ERR_IO;
    if (!write_u16(out, 0))               return SNAP_ERR_IO;
    if (!write_u32(out, rom_crc32))       return SNAP_ERR_IO;
    if (!write_u32(out, 0))               return SNAP_ERR_IO;

    /* CPU */
    if (!write_chunk_hdr(out, "CPU\0", CPU_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_cpu(out, &board->cpu)) return SNAP_ERR_IO;

    /* Memory */
    if (!write_chunk_hdr(out, "MEM\0", MEM_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_mem(out, &board->mem)) return SNAP_ERR_IO;

    /* Timer */
    if (!write_chunk_hdr(out, "TMR\0", TIMER_CHUNK_SIZE)) return SNAP_ERR_IO;
    for (int i = 0; i < 3; i++)
        if (!write_timer_ch(out, &board->timer.ch[i])) return SNAP_ERR_IO;

    /* Keyboard USART */
    if (!write_chunk_hdr(out, "KBD\0", KBD_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_kbd(out, &board->kbd)) return SNAP_ERR_IO;

    /* FDC */
    if (!write_chunk_hdr(out, "FDC\0", FDC_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_fdc(out, &board->fdc)) return SNAP_ERR_IO;

    /* Board registers */
    if (!write_chunk_hdr(out, "BRD\0", BRD_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_brd(out, board)) return SNAP_ERR_IO;

    /* RAM disk */
    uint32_t rdk_sz = ramdisk_chunk_size(&board->ramdisk);
    if (!write_chunk_hdr(out, "RDK\0", rdk_sz)) return SNAP_ERR_IO;
    if (!write_ramdisk(out, &board->ramdisk)) return SNAP_ERR_IO;

    /* Event history ring — omitted if the ring is disabled (cap == 0). */
    uint32_t hist_sz = history_chunk_size(&board->history);
    if (hist_sz) {
        if (!write_chunk_hdr(out, "HIST", hist_sz)) return SNAP_ERR_IO;
        if (!write_history(out, &board->history)) return SNAP_ERR_IO;
    }

    /* MS7004 keyboard controller */
    if (!write_chunk_hdr(out, "7004", MS7004_CHUNK_SIZE)) return SNAP_ERR_IO;
    if (!write_ms7004(out, kbd7004)) return SNAP_ERR_IO;

    /* Disk paths */
    uint32_t dp_sz = disk_paths_size(disk_paths);
    if (!write_chunk_hdr(out, "DISK", dp_sz)) return SNAP_ERR_IO;
    uint32_t dp_written;
    if (!write_disk_paths(out, disk_paths, &dp_written)) return SNAP_ERR_IO;

    /* End marker */
    if (!write_chunk_hdr(out, "END\0", 0)) return SNAP_ERR_IO;

    return SNAP_OK;
}

/* ── Load ────────────────────────────────────────────────────────────────── */

snap_error_t snap_load(ms0515_board_t *board,
                       ms7004_t *kbd7004,
                       uint32_t *rom_crc32_out,
                       char *disk_paths_out[4],
                       snap_io_t *in)
{
    for (int i = 0; i < 4; i++)
        disk_paths_out[i] = NULL;

    /* Header */
    char magic[4];
    if (!read_bytes(in, magic, 4)) return SNAP_ERR_IO;
    if (memcmp(magic, SNAP_MAGIC, 4) != 0) return SNAP_ERR_BAD_MAGIC;

    uint16_t version;
    if (!read_u16(in, &version)) return SNAP_ERR_IO;
    if (version > SNAP_VERSION) return SNAP_ERR_BAD_VERSION;

    uint16_t flags;
    if (!read_u16(in, &flags)) return SNAP_ERR_IO;

    if (!read_u32(in, rom_crc32_out)) return SNAP_ERR_IO;

    uint32_t reserved;
    if (!read_u32(in, &reserved)) return SNAP_ERR_IO;

    /* Read chunks */
    bool got_cpu = false, got_mem = false, got_tmr = false;
    bool got_kbd = false, got_fdc = false, got_brd = false;
    bool got_rdk = false, got_7004 = false, got_disk = false;

    for (;;) {
        char id[4];
        uint32_t chunk_size;
        if (!read_chunk_hdr(in, id, &chunk_size)) {
            /* EOF before END chunk — accept if we got the essentials */
            break;
        }

        if (memcmp(id, "END\0", 4) == 0)
            break;

        if (memcmp(id, "CPU\0", 4) == 0) {
            if (!read_cpu(in, &board->cpu)) return SNAP_ERR_CORRUPT;
            got_cpu = true;
        } else if (memcmp(id, "MEM\0", 4) == 0) {
            if (!read_mem(in, &board->mem)) return SNAP_ERR_CORRUPT;
            got_mem = true;
        } else if (memcmp(id, "TMR\0", 4) == 0) {
            for (int i = 0; i < 3; i++)
                if (!read_timer_ch(in, &board->timer.ch[i])) return SNAP_ERR_CORRUPT;
            got_tmr = true;
        } else if (memcmp(id, "KBD\0", 4) == 0) {
            if (!read_kbd(in, &board->kbd)) return SNAP_ERR_CORRUPT;
            got_kbd = true;
        } else if (memcmp(id, "FDC\0", 4) == 0) {
            if (!read_fdc(in, &board->fdc)) return SNAP_ERR_CORRUPT;
            got_fdc = true;
        } else if (memcmp(id, "BRD\0", 4) == 0) {
            if (!read_brd(in, board)) return SNAP_ERR_CORRUPT;
            got_brd = true;
        } else if (memcmp(id, "RDK\0", 4) == 0) {
            if (!read_ramdisk(in, &board->ramdisk, chunk_size)) return SNAP_ERR_CORRUPT;
            got_rdk = true;
        } else if (memcmp(id, "HIST", 4) == 0) {
            if (!read_history(in, &board->history, chunk_size)) return SNAP_ERR_CORRUPT;
        } else if (memcmp(id, "7004", 4) == 0) {
            if (!read_ms7004(in, kbd7004)) return SNAP_ERR_CORRUPT;
            got_7004 = true;
        } else if (memcmp(id, "DISK", 4) == 0) {
            if (!read_disk_paths(in, disk_paths_out)) return SNAP_ERR_CORRUPT;
            got_disk = true;
        } else {
            /* Unknown chunk — skip */
            if (!in->seek(in->ctx, (long)chunk_size))
                return SNAP_ERR_IO;
        }
    }

    /* Verify essential chunks were present */
    if (!got_cpu || !got_mem || !got_brd)
        return SNAP_ERR_CORRUPT;

    (void)got_tmr; (void)got_kbd; (void)got_fdc;
    (void)got_rdk; (void)got_7004; (void)got_disk;

    return SNAP_OK;
}
