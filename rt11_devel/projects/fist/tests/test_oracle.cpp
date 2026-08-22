/*
 * test_oracle.cpp - the VRAM oracle: load a FIST .SAV straight into RAM, run
 * it headless to its keyboard-wait loop and dump the 16 KB VRAM, so a host
 * script (source/render_vram.py, gl_check.py) can compare the MACRO-11 code's
 * actual output with the Python reference.  Used by the verification builds
 * (FIST_MODE=fighter, FIST_GL=<routine> ...), not by the standalone game,
 * which loads through RT-11 (see test_game.cpp).
 */
#include "FistGame.hpp"

TEST_CASE("fist: VRAM oracle")
{
    std::string sav = fist::savPath();
    if (!fist::fs::exists(sav)) {
        MESSAGE("FIST.SAV not built - skipping the FIST VRAM oracle");
        return;
    }
    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();

    // Bank the address space exactly as FIST will at runtime (#3377: banks
    // 0-6 primary, VRAM window enabled) BEFORE loading, so the image lands
    // in the same banks the running program reads from.
    auto &board = ms0515::internal::board(emu);
    board.mem.dispatcher = 03377;
    std::vector<uint8_t> img = fist::readFile(sav);
    REQUIRE(img.size() > 01000);
    for (size_t i = 0; i < img.size(); ++i)
        emu.writeByte(static_cast<uint16_t>(i), img[i]);

    // RT-11 .SAV programs are based at 01000 (START).  A stack below the VRAM
    // window, IRQs masked (FIST does this itself on its first instructions).
    auto &cpu = ms0515::internal::cpu(emu);
    cpu.r[CPU_REG_PC] = 01000;
    cpu.r[CPU_REG_SP] = 037000;
    cpu.psw = 0340;
    for (int i = 0; i < 1000000; ++i)          // far more than the engine + present take
        emu.stepInstruction();

    const uint8_t *vram = board_get_vram(&board);
    fist::writeFile(fist::optOr("oracle-out", std::string{FIST_BUILD_DIR} + "/fist_vram.bin"),
                    vram, MEM_VRAM_SIZE);
    int nonzero = 0;
    for (int i = 0; i < MEM_VRAM_SIZE; ++i)
        if (vram[i]) ++nonzero;
    MESSAGE("VRAM non-zero bytes: " << nonzero);
    CHECK(nonzero > 100);                      // something was drawn (the host compares it)
}
