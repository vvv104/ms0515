/*
 * test_fist_screen.cpp - VRAM oracle for the FIST (WotEF) MACRO-11 port.
 *
 * Loads a FIST .SAV image directly into RAM, runs it headless to its
 * keyboard-wait loop, and dumps the 16 KB VRAM so a host script can render
 * the *actual* MS-0515 output and compare it to the reference.  This is the
 * pixel-exact verification the port's clean-run smoke test cannot give.
 *
 * Opt-in: the test no-ops unless the FIST.SAV named by FIST_SAV_PATH (a
 * build artifact, so usually absent) exists; when present it dumps VRAM to
 * FIST_VRAM_OUT_PATH for host rendering.
 */
#include <doctest/doctest.h>

#include <ms0515/Emulator.hpp>

#include "../src/EmulatorInternal.hpp"

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/cpu.h>
#include <ms0515/core/memory.h>
}

#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef ASSETS_DIR
#error "ASSETS_DIR must be defined by the build system"
#endif
#ifndef FIST_SAV_PATH
#error "FIST_SAV_PATH must be defined by the build system"
#endif
#ifndef FIST_VRAM_OUT_PATH
#error "FIST_VRAM_OUT_PATH must be defined by the build system"
#endif

TEST_CASE("fist: VRAM oracle")
{
    std::ifstream probe(FIST_SAV_PATH, std::ios::binary);
    if (!probe.good()) {
        MESSAGE("FIST.SAV not built - skipping the FIST VRAM oracle");
        return;
    }
    const char *savPath = FIST_SAV_PATH;

    ms0515::Emulator emu;
    REQUIRE(emu.loadRomFile(std::string{ASSETS_DIR} + "/rom/ms0515-roma.rom"));
    emu.reset();

    // Bank the address space exactly as FIST will at runtime (#3377: banks
    // 0-6 primary, VRAM window enabled) BEFORE loading, so the image lands
    // in the same banks the running program reads from.
    auto &board = ms0515::internal::board(emu);
    board.mem.dispatcher = 03377;

    std::ifstream f(savPath, std::ios::binary);
    REQUIRE(f.good());
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    REQUIRE(img.size() > 01000);
    for (size_t i = 0; i < img.size(); ++i)
        emu.writeByte(static_cast<uint16_t>(i), img[i]);

    // RT-11 .SAV programs are based at 01000; FIST's first instruction is
    // START there.  Give it a stack below the VRAM window and mask IRQs
    // (FIST does this itself on its first instructions anyway).
    auto &cpu = ms0515::internal::cpu(emu);
    cpu.r[CPU_REG_PC] = 01000;
    cpu.r[CPU_REG_SP] = 037000;
    cpu.psw = 0340;

    // Far more than the ~100k instructions the engine + present take; the
    // program then spins harmlessly in its keyboard-poll loop.
    for (int i = 0; i < 1000000; ++i)
        emu.stepInstruction();

    const uint8_t *vram = board_get_vram(&board);
    {
        std::ofstream o(FIST_VRAM_OUT_PATH, std::ios::binary);
        o.write(reinterpret_cast<const char *>(vram), MEM_VRAM_SIZE);
    }

    // The engine must have drawn a substantial, non-blank picture.
    int nonzero = 0;
    for (int i = 0; i < MEM_VRAM_SIZE; ++i)
        if (vram[i])
            ++nonzero;
    CHECK(nonzero > 2000);
}
