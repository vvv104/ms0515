/*
 * snapshot.h — Machine state snapshot (save/load state)
 *
 * Serializes the complete emulator state to/from a binary file.
 * The format is field-by-field (not raw struct dump) for portability
 * across compilers and platforms.  All multi-byte integers are stored
 * little-endian.
 *
 * The file consists of a 16-byte header followed by tagged chunks.
 * Unknown chunks are skipped for forward compatibility.
 *
 * ROM image data is NOT saved — only a CRC32 checksum for verification.
 * Floppy disk image data is NOT saved — only the file paths.
 * RAM disk data (512 KB) IS saved when the expansion is enabled.
 */

#ifndef MS0515_SNAPSHOT_H
#define MS0515_SNAPSHOT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "board.h"
#include "ms7004.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── I/O abstraction ─────────────────────────────────────────────────────── */

/*
 * snap_io_t — opaque byte-stream interface for snapshot serialization.
 *
 * The caller provides read/write/seek callbacks so that the snapshot
 * code is decoupled from FILE* or any specific I/O layer.
 */
typedef struct {
    bool (*write)(void *ctx, const void *data, size_t n);
    bool (*read)(void *ctx, void *data, size_t n);
    bool (*seek)(void *ctx, long offset);      /* SEEK_CUR only */
    void *ctx;
} snap_io_t;

/* ── Error codes ─────────────────────────────────────────────────────────── */

typedef enum {
    SNAP_OK = 0,
    SNAP_ERR_IO,            /* I/O error                                    */
    SNAP_ERR_BAD_MAGIC,     /* Not a snapshot file                          */
    SNAP_ERR_BAD_VERSION,   /* Unsupported format version                   */
    SNAP_ERR_ROM_MISMATCH,  /* ROM CRC32 does not match                     */
    SNAP_ERR_CORRUPT,       /* Data truncated or chunk size mismatch        */
} snap_error_t;

/* ── Save ────────────────────────────────────────────────────────────────── */

/*
 * snap_save — Write the complete machine state to a byte stream.
 *
 * `rom_crc32`  — precomputed CRC32 of the loaded ROM image.
 * `disk_paths` — array of 4 paths (NULL for empty drives).
 */
snap_error_t snap_save(const ms0515_board_t *board,
                       const ms7004_t *kbd7004,
                       uint32_t rom_crc32,
                       const char *disk_paths[4],
                       snap_io_t *io);

/* ── Load ────────────────────────────────────────────────────────────────── */

/*
 * snap_load — Read machine state from a byte stream.
 *
 * `rom_crc32_out` — receives the ROM CRC32 stored in the snapshot.
 *                   The caller must compare it with the current ROM.
 * `disk_paths_out` — receives malloc'd strings for mounted disk paths.
 *                    The caller must free() each non-NULL entry.
 *
 * On success, the board and kbd7004 structs are fully populated.
 * The caller must:
 *   1. Verify rom_crc32_out matches the current ROM.
 *   2. Re-establish cpu.board back-pointer.
 *   3. Re-wire kbd.tx_callback / tx_callback_ctx.
 *   4. Re-wire kbd7004->uart pointer.
 *   5. Re-register sound/serial callbacks.
 *   6. Re-mount disk images from disk_paths_out.
 */
snap_error_t snap_load(ms0515_board_t *board,
                       ms7004_t *kbd7004,
                       uint32_t *rom_crc32_out,
                       char *disk_paths_out[4],
                       snap_io_t *io);

/* ── CRC32 utility ───────────────────────────────────────────────────────── */

uint32_t snap_crc32(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_SNAPSHOT_H */
