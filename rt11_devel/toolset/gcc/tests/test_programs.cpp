/*
 * test_programs.cpp - the example programs, run and read.
 */

#include "Programs.hpp"

#include "Embedded.hpp"
#include "EmulatorInternal.hpp"
#include <ms0515/Typist.hpp>

extern "C" {
#include <ms0515/core/board.h>
}

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

using namespace gcc_tests;

namespace {

/* ARITH's table (examples/arith.c), and what C says of each pair on a
 * 16-bit machine: the quotient truncated toward zero, the remainder with
 * the dividend's sign, the product's low 16 bits - and the unsigned
 * quotient and remainder of the same bits. */
constexpr int16_t kPairs[][2] = {
    {7, 2}, {-7, 2}, {7, -2}, {-7, -2},
    {32767, 1}, {-32768, 1}, {-32768, -1}, {0, 5}, {1, 32767},
    {30000, 7}, {-30000, 7}, {12345, -123}, {-32768, 2}, {32767, 32767},
    {255, 256}, {-1, 1}, {-1, 2}, {-1, -1}, {1000, 1000}, {4096, 64},
};

std::string expectedArith(int16_t a, int16_t b)
{
    const auto wrap = [](long v) { return static_cast<int16_t>(static_cast<uint16_t>(v & 0xFFFF)); };
    const long la = a, lb = b;
    const auto ua = static_cast<uint16_t>(a), ub = static_cast<uint16_t>(b);
    std::ostringstream s;
    s << a << ' ' << b << ": " << wrap(la / lb) << ' ' << wrap(la % lb) << ' ' << wrap(la * lb)
      << ' ' << ua / ub << ' ' << ua % ub;
    return s.str();
}

}  // namespace

TEST_SUITE("gcc toolset programs") {

TEST_CASE("HELLO prints its line by .PRINT and comes back to the monitor") {
    if (!built("HELLO")) { MESSAGE("HELLO.SAV not built - skipped"); return; }
    const Run r = run("HELLO");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    CHECK(r.printed == "HELLO FROM GCC\n");
}

TEST_CASE("ARITH: the 16-bit helpers agree with C on every pair") {
    if (!built("ARITH")) { MESSAGE("ARITH.SAV not built - skipped"); return; }
    const Run r = run("ARITH");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const auto got = lines(r.printed);
    constexpr std::size_t n = sizeof kPairs / sizeof kPairs[0];
    REQUIRE(got.size() == n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        CAPTURE(i);
        CHECK(got[i] == expectedArith(kPairs[i][0], kPairs[i][1]));
    }
    CHECK(got[n] == "ARITH DONE");
}

TEST_CASE("LIBC: printf, strings, ctype, stdlib and the heap, a line a group") {
    if (!built("LIBC")) { MESSAGE("LIBC.SAV not built - skipped"); return; }
    const Run r = run("LIBC");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const std::vector<std::string> expected = {
        "42|-42|65535|ff|FF|10|c|str|%",
        "[   42][42   ][00042][ab][   ab][ab   ]",
        "123456|-123456|4294967295|abcdef",
        "-32768|32767|65535",
        "864192 -300000 4294836225 142857",
        "7-x",
        "abc 6",
        "5 -1 foobar 2 3 0 aabcde xxxyz -1 0",
        "1 1 A q 1",
        "-123 42 5 511 -31 []",
        "1 1 1 1",
        "1 1",
        "LIBC DONE",
    };
    const auto got = lines(r.printed);
    REQUIRE(got.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == expected[i]);
    }
}

TEST_CASE("LIBC2: the rest of the strings, the long conversions, printf's corners") {
    if (!built("LIBC2")) { MESSAGE("LIBC2.SAV not built - skipped"); return; }
    const Run r = run("LIBC2");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const std::vector<std::string> expected = {
        "abc 0 0 abc abcde 4 1 0 6 1",
        "123456 -70000 12 65535 65535 -16 15 12 z",
        "5 keep 5 [] [    x][y    ][toolong][-42    ][-000042] [ffffffff][10][   42]",
        "-16384 -3 1 -1",
        "LIBC2 DONE",
    };
    const auto got = lines(r.printed);
    REQUIRE(got.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == expected[i]);
    }
}

/* INPUT (examples/input.c): a line typed at the program, read by
 * getchar and given back backwards; a character by .TTYIN itself.  The
 * monitor echoes what is typed, so the program's lines are looked for
 * among the console's in order. */
TEST_CASE("INPUT: getchar reads the line typed, rt11_ttyin the character") {
    if (!built("INPUT")) { MESSAGE("INPUT.SAV not built - skipped"); return; }
    const fs::path dir = fs::temp_directory_path() / "ms0515_gcc_input";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(savDir() / "INPUT.SAV", dir / "INPUT.SAV");
    ms0515::run::Machine machine;
    REQUIRE(machine.start(dir / "INPUT.SAV", {}));
    ms0515::Typist typist;
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    const auto step = [&] { typist.pump(machine.emulator()); machine.step(); printed += text.convert(machine.takeOutput()); };
    for (int f = 0; f < 60 && !machine.ended(); ++f) step();
    for (char c : std::string("Hello, 123\n")) typist.type(static_cast<uint8_t>(c));
    for (int f = 0; f < 150 && !machine.ended(); ++f) step();
    for (char c : std::string("z\n")) typist.type(static_cast<uint8_t>(c));
    for (int f = 0; f < 300 && !machine.ended(); ++f) step();
    REQUIRE(machine.ended());
    printed += text.convert(machine.drainOutput());
    CHECK_FALSE(machine.failed());
    const auto got = lines(printed);
    const std::vector<std::string> expected = {"TYPE A LINE", "INPUT 10:321 ,olleH", "TTYIN 122", "INPUT DONE"};
    std::size_t at = 0;
    for (const auto &line : expected) {
        while (at < got.size() && got[at] != line) ++at;
        CAPTURE(line);
        CHECK(at < got.size());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/* GCC 15.2's pdp11 backend compares longs wrongly when the high words
 * are equal; build-toolchain.sh patches it (toolset/gcc/README.md, "A
 * trap in the compiler").  This fails on a compiler without the patch. */
TEST_CASE("CMPLONG: signed comparisons of longs, the compiler's fix in place") {
    if (!built("CMPLONG")) { MESSAGE("CMPLONG.SAV not built - skipped"); return; }
    const Run r = run("CMPLONG");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    constexpr long kLongPairs[][2] = {
        {100000L, 5L}, {-100000L, 5L},
        {32768L, 5L}, {40000L, 32767L}, {-32768L, -40000L},
        {32768L, 0L}, {0x10000L + 32768L, 0x10000L + 5L},
        {5L, 32768L}, {-5L, -32768L}, {32768L, 32768L},
    };
    const auto got = lines(r.printed);
    constexpr std::size_t n = sizeof kLongPairs / sizeof kLongPairs[0];
    REQUIRE(got.size() == n + 1);
    for (std::size_t i = 0; i < n; ++i) {
        const long x = kLongPairs[i][0], y = kLongPairs[i][1];
        std::string expected;
        for (bool b : {x > y, x < y, x >= y, x <= y, x >= 0, x < 0}) expected += b ? '1' : '0';
        CAPTURE(i);
        CHECK(got[i] == expected);
    }
    CHECK(got[n] == "CMPLONG DONE");
}

/* MACHINE (examples/machine.c) over the machine library: the screen in
 * colour with a diagonal and one cell's attribute, the clock counting
 * while it waits, the keyboard taken until Q, the console back. */
TEST_CASE("MACHINE: the screen, the clock and the keyboard through ms0515.h") {
    if (!built("MACHINE")) { MESSAGE("MACHINE.SAV not built - skipped"); return; }
    const fs::path dir = fs::temp_directory_path() / "ms0515_gcc_machine";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(savDir() / "MACHINE.SAV", dir / "MACHINE.SAV");
    ms0515::run::Machine machine;
    REQUIRE(machine.start(dir / "MACHINE.SAV", {}));
    ms0515::Typist typist;
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    const auto step = [&] { typist.pump(machine.emulator()); machine.step(); printed += text.convert(machine.takeOutput()); };
    for (int f = 0; f < 60; ++f) step();
    REQUIRE_FALSE(machine.ended());
    CHECK_FALSE(machine.emulator().isHires());

    const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
    const auto pixel = [&](int x, int y) { return (vram[y * 80 + (x >> 3) * 2] >> (7 - (x & 7))) & 1; };
    CHECK(pixel(0, 0) == 0);                /* unplotted */
    CHECK(pixel(8, 5) == 1);                /* x * 5 / 8 */
    CHECK(pixel(160, 100) == 1);
    CHECK(pixel(319, 199) == 1);
    CHECK(pixel(100, 10) == 0);
    CHECK(vram[1] == 0x07);                 /* the attribute asked: ink white */
    CHECK(vram[3 * 80 + 2 * 2 + 1] == 0x42);  /* cell (2, 3): ink red, bright */

    typist.type('q');
    for (int f = 0; f < 200 && !machine.ended(); ++f) step();
    REQUIRE(machine.ended());
    CHECK_FALSE(machine.failed());
    CHECK(machine.emulator().isHires());
    printed += text.convert(machine.drainOutput());
    /* The screen left black for the console: in 640x200 the 8000
     * attribute bytes would show as stripes.  What is lit now is the
     * console's own text, a line or two. */
    int lit = 0;
    for (int at = 0; at < 200 * 80; ++at) lit += vram[at] != 0;
    CHECK(lit < 1500);
    unsigned frames = 0;
    int codes = 0;
    std::istringstream line(printed);
    std::string word, framesWord;
    REQUIRE((line >> word >> frames >> framesWord >> codes));
    CHECK(word == "MACHINE:");
    CHECK(frames >= 25);
    CHECK(frames < 300);
    CHECK(codes >= 1);
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/* A machine with a typist and the console's text gathered, for the
 * examples that are pressed at while they run. */
struct Driven {
    ms0515::run::Machine machine;
    ms0515::Typist typist;
    ms0515::run::ConsoleText text{ms0515::run::ConsoleText::Reader::plain};
    std::string printed;
    fs::path dir;

    explicit Driven(const char *name)
    {
        dir = fs::temp_directory_path() / (std::string("ms0515_gcc_") + name);
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string file = std::string(name) + ".SAV";
        fs::copy_file(savDir() / file, dir / file);
        REQUIRE(machine.start(dir / file, {}));
    }
    ~Driven()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void step()
    {
        typist.pump(machine.emulator());
        machine.step();
        printed += text.convert(machine.takeOutput());
    }
    void run(int frames)
    {
        for (int f = 0; f < frames && !machine.ended(); ++f) step();
    }
    void finish()
    {
        run(1000);
        REQUIRE(machine.ended());
        printed += text.convert(machine.drainOutput());
        CHECK_FALSE(machine.failed());
    }
    const ms0515_board_t &board() { return ms0515::internal::board(machine.emulator()); }
};

/* HELD (examples/held.c): Right tapped while the program is still
 * throwing keys away, then Left tapped once and later Left held down
 * for forty frames - the keyboard repeats it, as a real one does; the
 * program says when Left's hold began and ended, in its own frames:
 * nine of them after the single code, on through the repeats and four
 * past the last. */
TEST_CASE("HELD: a held key is a timer its codes wind up, and the flush drops what came before") {
    if (!built("HELD")) { MESSAGE("HELD.SAV not built - skipped"); return; }
    Driven d("HELD");
    d.run(35);
    d.typist.type(ms0515::Key::Right);
    d.run(65);                                  /* frame 100: well past the flush */
    d.typist.type(ms0515::Key::Left);
    d.run(50);                                  /* frame 150 */
    d.machine.emulator().keyPress(ms0515::Key::Left, true);
    d.run(40);
    d.machine.emulator().keyPress(ms0515::Key::Left, false);
    d.finish();
    const auto got = lines(d.printed);
    REQUIRE(got.size() == 1);
    MESSAGE(got[0]);
    std::istringstream line(got[0]);
    std::string word, slash, rightWord;
    unsigned t[4];
    int right = -1;
    REQUIRE((line >> word >> t[0] >> t[1] >> t[2] >> t[3] >> slash >> rightWord >> right));
    CHECK(word == "HELD");
    CHECK(t[1] - t[0] == 9);                    /* MS_HELD_FIRST after one code */
    CHECK(t[2] > t[1]);
    CHECK(t[3] - t[2] >= 40);                   /* held through the repeats, MS_HELD_NEXT past the last */
    CHECK(t[3] - t[2] <= 50);
    CHECK(right == 0);
}

/* PORTS (examples/ports.c): the joystick's lines held by the harness
 * for a while, the border seen blue while the screen is on, the
 * speaker's level flipped once, and a word in each half of bank 3. */
TEST_CASE("PORTS: the joystick, the border, the speaker and the banks") {
    if (!built("PORTS")) { MESSAGE("PORTS.SAV not built - skipped"); return; }
    Driven d("PORTS");
    d.run(30);
    REQUIRE_FALSE(d.machine.ended());
    CHECK(d.machine.emulator().borderColor() == 1);
    const int level = d.board().sound_value;
    d.machine.emulator().setJoystick(ms0515::Emulator::Joy::Up | ms0515::Emulator::Joy::Fire);
    d.run(20);
    d.machine.emulator().setJoystick(0);
    d.run(30);
    CHECK(d.board().sound_value != level);      /* flipped at the program's frame 20 */
    d.finish();
    CHECK(d.machine.emulator().borderColor() == 0);
    const auto got = lines(d.printed);
    REQUIRE(got.size() == 2);
    CHECK(got[0] == "JOY 24");
    CHECK(got[1] == "BANK 1234 5678");
}

/* DRAW (examples/draw.c): a column, a box, an image with and without
 * its attributes, text in the ROM's font - ASCII and Cyrillic - read
 * cell by cell; the glyphs compared with the font found in the ROM the
 * machine carries, by the shape of '0' and of the Cyrillic A as the
 * library finds them. */
TEST_CASE("DRAW: ms_vfill, ms_fill, ms_blit and ms_text in the ROM's font") {
    if (!built("DRAW")) { MESSAGE("DRAW.SAV not built - skipped"); return; }
    Driven d("DRAW");
    d.run(80);
    REQUIRE_FALSE(d.machine.ended());
    const uint8_t *vram = board_get_vram(&d.board());
    const auto cell = [&](int c, int y) { return vram + y * 80 + c * 2; };

    for (int y = 10; y <= 20; ++y) { CHECK(cell(5, y)[0] == 0xFF); CHECK(cell(5, y)[1] == 0x02); }
    CHECK(cell(5, 9)[0] == 0);
    CHECK(cell(5, 21)[0] == 0);
    CHECK(cell(4, 15)[0] == 0);
    for (int y = 30; y <= 32; ++y)
        for (int c = 10; c <= 12; ++c) { CHECK(cell(c, y)[0] == 0xAA); CHECK(cell(c, y)[1] == 0x48); }
    CHECK(cell(13, 31)[0] == 0);
    CHECK(cell(9, 31)[1] == 0x07);
    const uint8_t pixels[6] = {0x81, 0x18, 0xFF, 0x00, 0x3C, 0xC3};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c) {
            CHECK(cell(20 + c, 50 + r)[0] == pixels[r * 2 + c]);
            CHECK(cell(20 + c, 50 + r)[1] == r * 2 + c + 1);
            CHECK(cell(24 + c, 50 + r)[0] == pixels[r * 2 + c]);
            CHECK(cell(24 + c, 50 + r)[1] == 0x07);     /* the screen's own */
        }

    const auto rom = ms0515::run::embedded::rom;
    const auto find = [&](const uint8_t (&shape)[8]) -> const uint8_t * {
        for (std::size_t at = 0; at + 8 <= rom.size(); ++at)
            if (std::memcmp(rom.data() + at, shape, 8) == 0) return rom.data() + at;
        return nullptr;
    };
    const uint8_t zero[8] = {0x00, 0x3C, 0x46, 0x4A, 0x52, 0x62, 0x3C, 0x00};
    const uint8_t cyrA[8] = {0x30, 0x78, 0xCC, 0xCC, 0xFC, 0xCC, 0xCC, 0x00};
    const uint8_t *zeroAt = find(zero);
    const uint8_t *cyrAAt = find(cyrA);
    REQUIRE(zeroAt != nullptr);
    REQUIRE(cyrAAt != nullptr);
    const uint8_t *mainFont = zeroAt - 16 * 8;
    const uint8_t *altFont = cyrAAt - 33 * 8;
    const auto glyph = [&](int koi8) { return koi8 < 0200 ? mainFont + (koi8 - 040) * 8 : altFont + (koi8 - 0300) * 8; };
    const int text[] = {'H', 'i', '!'};
    for (int i = 0; i < 3; ++i)
        for (int r = 0; r < 8; ++r) {
            CAPTURE(i); CAPTURE(r);
            CHECK(cell(i, 100 + r)[0] == glyph(text[i])[r]);
            CHECK(cell(i, 100 + r)[1] == 0x06);
        }
    const int cyrillic[] = {0304, 0301};
    for (int i = 0; i < 2; ++i)
        for (int r = 0; r < 8; ++r) {
            CAPTURE(i); CAPTURE(r);
            CHECK(cell(10 + i, 100 + r)[0] == glyph(cyrillic[i])[r]);
            CHECK(cell(10 + i, 100 + r)[1] == 0x05);
        }
    for (int r = 0; r < 8; ++r) { CHECK(cell(12, 100 + r)[0] == 0); CHECK(cell(13, 100 + r)[0] == 0); }
    int lit = 0;                                 /* the letters are letters, not blanks */
    for (int r = 0; r < 8; ++r) lit += cell(0, 100 + r)[0] != 0;
    CHECK(lit >= 5);

    d.typist.type('q');
    d.finish();
    CHECK(d.machine.emulator().isHires());
}

/* STREAM (examples/stream.c): a block read with the screen on, the
 * window closed round the requests and opened again - a bar as long as
 * the block says, the bar drawn before it untouched, the monitor alive
 * for the end. */
TEST_CASE("STREAM: a file read with the screen on, through ms_window") {
    if (!built("STREAM")) { MESSAGE("STREAM.SAV not built - skipped"); return; }
    const fs::path dir = fs::temp_directory_path() / "ms0515_gcc_stream";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(savDir() / "STREAM.SAV", dir / "STREAM.SAV");
    std::vector<uint8_t> data(512, 0);
    data[0] = 17;
    std::ofstream(dir / "STREAM.DAT", std::ios::binary)
        .write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    ms0515::run::Machine machine;
    REQUIRE(machine.start(dir / "STREAM.SAV", {}));
    ms0515::Typist typist;
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    const auto step = [&] { typist.pump(machine.emulator()); machine.step(); printed += text.convert(machine.takeOutput()); };
    for (int f = 0; f < 100 && !machine.ended(); ++f) step();
    REQUIRE_FALSE(machine.ended());
    const uint8_t *vram = board_get_vram(&ms0515::internal::board(machine.emulator()));
    const auto cell = [&](int c, int y) { return vram + y * 80 + c * 2; };
    for (int c = 0; c < 10; ++c) { CHECK(cell(c, 11)[0] == 0xFF); CHECK(cell(c, 11)[1] == 0x07); }
    for (int c = 0; c < 17; ++c) { CHECK(cell(c, 51)[0] == 0xFF); CHECK(cell(c, 51)[1] == 0x04); }
    CHECK(cell(17, 51)[0] == 0);
    typist.type('q');
    for (int f = 0; f < 300 && !machine.ended(); ++f) step();
    REQUIRE(machine.ended());
    printed += text.convert(machine.drainOutput());
    CHECK_FALSE(machine.failed());
    CHECK(lines(printed) == std::vector<std::string>{"STREAM 256 17"});
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/* DEEP (examples/deep.c), built with STACK 4096: a recursion that takes
 * three kilobytes of stack, where the default kilobyte would not do. */
TEST_CASE("DEEP: STACK gives the stack its room") {
    if (!built("DEEP")) { MESSAGE("DEEP.SAV not built - skipped"); return; }
    const Run r = run("DEEP");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    CHECK(r.printed == "DEEP 7260\n");
}

/* FILES (examples/files.c) over rt11.h: FILES.DAT of three blocks put
 * beside it, read waited for and not, the end of the file, OUT.DAT
 * written and read back on the host, a file that is not there. */
TEST_CASE("FILES: .LOOKUP, .READW, .READ and .WAIT, .ENTER and .WRITW, .CLOSE") {
    if (!built("FILES")) { MESSAGE("FILES.SAV not built - skipped"); return; }
    const fs::path dir = fs::temp_directory_path() / "ms0515_gcc_files";
    fs::remove_all(dir);
    fs::create_directories(dir);
    fs::copy_file(savDir() / "FILES.SAV", dir / "FILES.SAV");
    std::vector<uint8_t> data;
    unsigned sums[3] = {0, 0, 0};
    for (unsigned n = 0; n < 3; ++n)
        for (unsigned i = 0; i < 256; ++i) {
            const uint16_t w = static_cast<uint16_t>(n * 256 + i);
            data.push_back(static_cast<uint8_t>(w & 0xFF));
            data.push_back(static_cast<uint8_t>(w >> 8));
            sums[n] = static_cast<uint16_t>(sums[n] + w);
        }
    std::ofstream(dir / "FILES.DAT", std::ios::binary)
        .write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));

    ms0515::run::Machine machine;
    REQUIRE(machine.start(dir / "FILES.SAV", {}));
    ms0515::run::ConsoleText text(ms0515::run::ConsoleText::Reader::plain);
    std::string printed;
    for (int f = 0; f < 3000 && machine.step(); ++f) printed += text.convert(machine.takeOutput());
    REQUIRE(machine.ended());
    printed += text.convert(machine.drainOutput());
    CHECK_FALSE(machine.failed());
    const std::vector<std::string> expected = {
        "FILES: 3 blocks",
        "READW 256 " + std::to_string(sums[1]),
        "READ 256 " + std::to_string(sums[2]),
        "EOF -1 0",
        "ENTER 2 0",
        "NOFILE -1 1",
        "FILES DONE",
    };
    const auto got = lines(printed);
    REQUIRE(got.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == expected[i]);
    }

    std::ifstream out(dir / "OUT.DAT", std::ios::binary);
    REQUIRE(out.good());
    std::vector<uint8_t> written{std::istreambuf_iterator<char>(out), {}};
    REQUIRE(written.size() == 1024);
    for (unsigned i = 0; i < 256; ++i) {
        CHECK(written[2 * i] == i);
        CHECK(written[2 * i + 1] == 0xA5);
        CHECK(written[512 + 2 * i] == i);
        CHECK(written[512 + 2 * i + 1] == 0x5A);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}

/* FX (examples/fx.c): fx_div8, fx_mul, fx_sin and fx_cos against the
 * host's own arithmetic, the table's values by the same rounding. */
TEST_CASE("FX: the fixed-point division, multiply, sine and cosine") {
    if (!built("FX")) { MESSAGE("FX.SAV not built - skipped"); return; }
    const Run r = run("FX");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    constexpr int divs[][2] = {
        {32640, 128}, {-32640, 128}, {255, 1}, {-255, 1}, {0, 7},
        {12800, 100}, {-12800, 100}, {30000, 200}, {1000, 1000}, {999, 1000},
        {20480, 81}, {-20480, 81}, {4096, 17}, {255, 255}, {32767, 129},
    };
    constexpr int muls[][2] = {
        {256, 256}, {512, 256}, {-512, 256}, {256, -256}, {-256, -256},
        {1000, 128}, {-1000, 128}, {32767, 255}, {-32767, 255}, {3, 100},
        {-3, 100}, {181, 181}, {30000, -2}, {255, 255}, {0, 32767},
    };
    constexpr int angles[] = {0, 1, 32, 63, 64, 65, 96, 127, 128, 129, 160, 191, 192, 193, 224, 255, 256, 300, -1, -64};
    std::vector<std::string> expected;
    for (const auto &d : divs)
        expected.push_back(std::to_string(d[0]) + " " + std::to_string(d[1]) + ": " + std::to_string(d[0] / d[1]));
    for (const auto &m : muls)
        expected.push_back(std::to_string(m[0]) + " " + std::to_string(m[1]) + ": " +
                           std::to_string(static_cast<long>(m[0]) * m[1] / 256));
    const auto sine = [](int angle) {
        const double pi = 3.14159265358979323846;
        return static_cast<int>(std::lround(256.0 * std::sin(2.0 * pi * (angle & 255) / 256.0)));
    };
    for (int a : angles)
        expected.push_back(std::to_string(a) + ": " + std::to_string(sine(a)) + " " + std::to_string(sine(a + 64)));
    expected.push_back("FX DONE");
    const auto got = lines(r.printed);
    REQUIRE(got.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(got[i] == expected[i]);
    }
}

TEST_CASE("CALC counts the primes below 10000 and tells its time") {
    if (!built("CALC")) { MESSAGE("CALC.SAV not built - skipped"); return; }
    const Run r = run("CALC");
    REQUIRE(r.ended);
    CHECK_FALSE(r.failed);
    const auto got = lines(r.printed);
    REQUIRE(got.size() == 2);
    CHECK(got[0].rfind("TIME: ", 0) == 0);
    MESSAGE("CALC " << got[0]);
    CHECK(got[1] == "PRIMES BELOW 10000: 1229 LAST 9973");
}

}
