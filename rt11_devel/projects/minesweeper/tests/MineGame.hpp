/*
 * MineGame.hpp - the minesweeper under test.
 *
 * The game's folder (the project's build: MINE.SAV with its sum and
 * sprites, MINE.HLP) run the way `ms0515-run MINE` runs it - the machine of ms0515-run,
 * without a terminal or a window.  The test presses the MS 7004's keys and
 * reads back what a player sees - the sprites in video memory, compared
 * with the table in MINE.DAT, and the console's text - and what he does not:
 * the field in the program's memory, to know where the mines are.
 *
 * The folder is the project's build/game unless --mine-game=<folder> names
 * another; the suite skips itself when there is none.
 */
#pragma once

#include <doctest/doctest.h>

#include "Machine.hpp"
#include "EmulatorInternal.hpp"

#include <ms0515/Emulator.hpp>
#include <ms0515/Terminal.hpp>
#include <ms0515/Typist.hpp>

extern "C" {
#include <ms0515/core/board.h>
#include <ms0515/core/memory.h>
}

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace mine {

namespace fs = std::filesystem;

inline std::map<std::string, std::string> &options()
{
    static std::map<std::string, std::string> opts;
    return opts;
}

inline std::string optOr(const char *name, const std::string &dflt)
{
    auto it = options().find(name);
    return it == options().end() || it->second.empty() ? dflt : it->second;
}

inline fs::path gameDir()   { return optOr("game", MINE_GAME_DIR); }
inline fs::path spritesPath() { return optOr("sprites", MINE_SPRITES); }
inline bool built() { return fs::exists(gameDir() / "MINE.SAV") && fs::exists(spritesPath()); }

inline std::vector<uint8_t> readAll(const fs::path &p)
{
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

/* The sprites of the game by what they show (MINE.DAT's numbers). */
enum Sprite : int {
    kOpen0 = 0,                 /* an open cell, 1..8 the count of mines round it */
    kMine = 9, kBlown = 10, kCursor = 11, kClosed = 12, kFlag = 13, kWrong = 14,
};

/* The program's globals, from their base (the program's high limit): the
 * layout PAS1 gave the VAR block every module declares. */
constexpr int kFieldOffset = 1012;      /* A: POLE = ARRAY[0..41,0..21], a word a cell */
constexpr int kFieldRows = 22;
constexpr int kSizeOffset = 2866;       /* PGP, NGP, KM */
constexpr int kCursorOffset = 2886;     /* X, Y after KPPM,KNPM,KOK and XMIN..YMAX */

class MineGame {
public:
    ms0515::run::Machine machine;

    /* Start MINE from a copy of the game's folder and run to its first wait
     * for a key: the Beginner field is up. */
    MineGame()
    {
        dir_ = fs::temp_directory_path() / ("ms0515_mine_" + std::to_string(counter()++));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        for (const auto &e : fs::directory_iterator(gameDir()))
            fs::copy_file(e.path(), dir_ / e.path().filename());
        sprites_ = readAll(spritesPath());
        const auto sav = readAll(dir_ / "MINE.SAV");
        /* The globals lie a word past the program's high limit (word 50
         * of its file). */
        base_ = static_cast<uint16_t>((sav[050] | sav[051] << 8) + 2);
        REQUIRE(machine.start(dir_ / "MINE.SAV", {}));
        idle(3000);
    }
    ~MineGame()
    {
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }
    MineGame(const MineGame &)            = delete;
    MineGame &operator=(const MineGame &) = delete;

    const fs::path &dir() const { return dir_; }
    bool ended() const { return machine.ended(); }

    /* Frames until the program has waited for a key for a while (or is
     * over): whatever it was drawing is drawn. */
    void idle(int cap = 1500)
    {
        int waiting = 0;
        for (int f = 0; f < cap && !machine.ended() && waiting < 30; ++f) {
            typist_.pump(machine.emulator());
            machine.step();
            waiting = machine.waitingForKey() ? waiting + 1 : 0;
        }
    }
    void press(ms0515::Key key)  { typist_.type(key); run(40); idle(); }
    void press(char c)           { typist_.type(static_cast<uint8_t>(c)); run(40); idle(); }
    void space()                 { press(' '); }
    void enter()                 { press(ms0515::Key::Return); }

    /* ── what the program holds ─────────────────────────────────────── */

    uint16_t word(uint16_t address) { return machine.emulator().readWord(address); }
    int global(int offset)          { return static_cast<int16_t>(word(static_cast<uint16_t>(base_ + offset))); }
    int width()   { return global(kSizeOffset); }
    int height()  { return global(kSizeOffset + 2); }
    int mines()   { return global(kSizeOffset + 4); }
    int cursorX() { return global(kCursorOffset); }
    int cursorY() { return global(kCursorOffset + 2); }
    /* A[x,y]: 0..8 closed with that many mines round it, 9 a closed mine,
     * 10..18 open, 19..28 marked. */
    int field(int x, int y) { return global(kFieldOffset + 2 * (x * kFieldRows + y)); }

    /* ── what the player sees ───────────────────────────────────────── */

    /* The sprite at screen cell (column, row), or -1: sixteen dots by
     * eight lines, a word a line, forty words a line. */
    int spriteAt(int column, int row)
    {
        const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
        for (int n = 0; n < 36; ++n) {
            bool same = true;
            for (int line = 0; line < 8 && same; ++line) {
                const int at = 80 * (row * 8 + line) + 2 * column;
                same = vram[at] == sprites_[n * 16 + line * 2] && vram[at + 1] == sprites_[n * 16 + line * 2 + 1];
            }
            if (same) return n;
        }
        return -1;
    }
    /* The sprite of field cell (x, y), 1-based as the game counts them:
     * the field is centred, its first row the screen's fifth. */
    int cell(int x, int y) { return spriteAt((40 - width()) / 2 + x - 1, 4 + y - 1); }

    std::string row(int r)
    {
        auto s = ms0515::Terminal{}.decode(machine.emulator()).row(r);
        while (!s.empty() && s.back() == ' ') s.pop_back();
        return s;
    }
    std::string screen()
    {
        std::string out;
        for (int r = 0; r < ms0515::Terminal::kRows; ++r) out += row(r) + '\n';
        return out;
    }

    /* Move the cursor to field cell (x, y) with the arrows. */
    void moveTo(int x, int y)
    {
        for (int n = 0; n < 64 && cursorX() < x; ++n) press(ms0515::Key::Right);
        for (int n = 0; n < 64 && cursorX() > x; ++n) press(ms0515::Key::Left);
        for (int n = 0; n < 64 && cursorY() < y; ++n) press(ms0515::Key::Down);
        for (int n = 0; n < 64 && cursorY() > y; ++n) press(ms0515::Key::Up);
        REQUIRE(cursorX() == x);
        REQUIRE(cursorY() == y);
    }

private:
    fs::path dir_;
    std::vector<uint8_t> sprites_;
    uint16_t base_ = 0;
    ms0515::Typist typist_;

    static int &counter() { static int n = 0; return n; }
    void run(int frames)
    {
        for (int f = 0; f < frames && !machine.ended(); ++f) {
            typist_.pump(machine.emulator());
            machine.step();
        }
    }
};

}  // namespace mine
