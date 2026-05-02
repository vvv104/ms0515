/*
 * rom_patches.h — Runtime patches for guest ROM/OS quirks.
 *
 * This subsystem is the home for emulator-specific kludges that
 * intercept CPU execution at known PCs to fix issues that would
 * otherwise hang the guest software.  It exists as a separate
 * compilation unit so cpu.c and board.c stay clean: those files
 * model the K1801VM1 CPU and the MS0515 board respectively, with no
 * knowledge of which ROM image happens to be loaded.
 *
 * Patches are NOT faithful CPU/board emulation — they bypass real
 * ROM code or fake hardware responses.  Each patch should be gated
 * by a signature check on the ROM contents at the patched address
 * so it only fires for the targeted ROM image.
 *
 * When a patch's underlying issue is fixed by proper hardware
 * emulation (e.g. when a real cassette device is added), the
 * corresponding patch can be removed without touching cpu.c or
 * board.c.
 */

#ifndef MS0515_ROM_PATCHES_H
#define MS0515_ROM_PATCHES_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ms0515_board ms0515_board_t;

/*
 * rom_patches_apply — invoked from the board's per-step hook before
 * each CPU instruction fetch.  Returns true if a patch fired and the
 * CPU should skip its normal fetch/dispatch this step (the patch has
 * already advanced PC and set cpu->cycles); false otherwise.
 */
bool rom_patches_apply(ms0515_board_t *board);

#ifdef __cplusplus
}
#endif

#endif /* MS0515_ROM_PATCHES_H */
