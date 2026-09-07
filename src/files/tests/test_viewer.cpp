/*
 * test_viewer.cpp — bytes to lines: text in the machine's encodings, the
 * octal and hex dumps, search.
 */
#include "Viewer.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace ms0515::files;

namespace {

std::vector<uint8_t> bytesOf(const std::string &s) { return {s.begin(), s.end()}; }

ViewOptions text(Encoding enc, bool wrap = true)
{
    ViewOptions o;
    o.view = View::text;
    o.encoding = enc;
    o.wrap = wrap;
    return o;
}

} // namespace

TEST_CASE("text: CR/LF and LF end lines, tabs expand, controls show as dots")
{
    const auto lines = renderLines(bytesOf("one\r\ntwo\nthree\tx\x01y"), text(Encoding::ascii));
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "one");
    CHECK(lines[1] == "two");
    CHECK(lines[2] == "three   x.y");
}

TEST_CASE("text wraps at the machine's 80 columns when asked")
{
    const std::string longLine(100, 'x');
    const auto wrapped = renderLines(bytesOf(longLine + "\nend"), text(Encoding::ascii, true));
    REQUIRE(wrapped.size() == 3);
    CHECK(wrapped[0].size() == 80);
    CHECK(wrapped[1].size() == 20);
    CHECK(wrapped[2] == "end");
    const auto plain = renderLines(bytesOf(longLine + "\nend"), text(Encoding::ascii, false));
    REQUIRE(plain.size() == 2);
    CHECK(plain[0].size() == 100);
}

TEST_CASE("the encodings: KOI-8R, KOI-7, KOI-7 with the SO/SI shifts, CP866, and 7-bit ASCII")
{
    bool shift = false;
    /* KOI-8R: 0xD0 0xD2 0xC9 0xD7 0xC5 0xD4 = привет (the upper-case row is 0xE0..) */
    const std::vector<uint8_t> koi8 = {0xD0, 0xD2, 0xC9, 0xD7, 0xC5, 0xD4};
    CHECK(renderLines(koi8, text(Encoding::koi8r))[0] == "привет");
    const std::vector<uint8_t> koi8up = {0xF0, 0xF2, 0xE9};
    CHECK(renderLines(koi8up, text(Encoding::koi8r))[0] == "ПРИ");
    /* KOI-7: the lower-case Latin positions are the Cyrillic letters -
     * the games' «na~nem?» is «НАЧНЕМ?» */
    CHECK(renderLines(bytesOf("na~nem?"), text(Encoding::koi7))[0] == "НАЧНЕМ?");
    CHECK(renderLines(bytesOf("DIR"), text(Encoding::koi7))[0] == "DIR");
    /* KOI-7 with shifts: ^N (0x0E) switches to РУС, ^O (0x0F) back */
    CHECK(renderLines(bytesOf("na\x0Ena\x0Fna"), text(Encoding::koi7shift))[0] == "naНАna");
    /* CP866: 0x80 = А, 0xE0 = р */
    const std::vector<uint8_t> cp = {0x80, 0xE0};
    CHECK(renderLines(cp, text(Encoding::cp866))[0] == "Ар");
    /* 7-bit ASCII shows the high half as dots */
    CHECK(renderLines(koi8, text(Encoding::ascii))[0] == "......");
    CHECK(decodeByte('A', Encoding::koi7, shift) == "A");
    CHECK(decodeByte('a', Encoding::koi7, shift) == "А");
}

TEST_CASE("the octal dump prints eight words a line, the hex dump sixteen bytes with an ASCII gutter")
{
    std::vector<uint8_t> bytes;
    for (int i = 0; i < 20; ++i) bytes.push_back(static_cast<uint8_t>(i));
    ViewOptions o;
    o.view = View::octal;
    const auto oct = renderLines(bytes, o);
    REQUIRE(oct.size() == 2);
    CHECK(oct[0].rfind("000000: 000400 001402 002404 003406 004410 005412 006414 007416", 0) == 0);
    CHECK(oct[1].rfind("000020: 010420 011422", 0) == 0);
    o.view = View::hex;
    const auto hex = renderLines(bytes, o);
    REQUIRE(hex.size() == 2);
    CHECK(hex[0].rfind("000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |", 0) == 0);
    CHECK(hex[0].find("|................|") != std::string::npos);
    CHECK(hex[1].rfind("000010  10 11 12 13", 0) == 0);
    /* printable bytes show in the gutter */
    const auto txt = renderLines(bytesOf("Hi"), o);
    CHECK(txt[0].find("|Hi|") != std::string::npos);
}

TEST_CASE("search: a string typed by the user is encoded the way the file is, then found")
{
    const auto needle = encodeString("НА", Encoding::koi7);
    REQUIRE(needle.has_value());
    CHECK(*needle == bytesOf("na"));
    const auto k8 = encodeString("при", Encoding::koi8r);
    REQUIRE(k8.has_value());
    CHECK(*k8 == std::vector<uint8_t>{0xD0, 0xD2, 0xC9});
    CHECK_FALSE(encodeString("Я", Encoding::ascii).has_value());
    const auto hay = bytesOf("xx na~nem? na");
    CHECK(findBytes(hay, *needle, 0) == 3);
    CHECK(findBytes(hay, *needle, 4) == 11);
    CHECK_FALSE(findBytes(hay, *needle, 12).has_value());
}

TEST_CASE("the cycles and names the key bar shows")
{
    CHECK(nextView(View::text) == View::octal);
    CHECK(nextView(View::octal) == View::hex);
    CHECK(nextView(View::hex) == View::text);
    CHECK(nextEncoding(Encoding::cp866) == Encoding::ascii);
    CHECK(std::string(viewName(View::hex)) == "hex");
    CHECK(std::string(encodingName(Encoding::koi7shift)) == "KOI-7 ^N/^O");
}

TEST_CASE("a text file is told from a binary one; the text view drops the block padding at the end")
{
    /* an RT-11 text file: lines, a Ctrl-Z, then the block padded with NULs */
    std::string txt = "one\r\ntwo\r\n";
    txt += '\x1A';
    txt += std::string(512 - txt.size(), '\0');
    const auto textBytes = bytesOf(txt);
    CHECK(isTextLike(textBytes));
    const auto lines = renderLines(textBytes, text(Encoding::ascii));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "one");
    CHECK(lines[1] == "two");

    /* KOI-8 Cyrillic and a form feed are text; a NUL inside is not, nor a .SAV's bytes */
    CHECK(isTextLike(bytesOf("\xF0\xD2\xC9\xD7\xC5\xD4\r\n\x0C")));
    CHECK(isTextLike(bytesOf("")));
    CHECK_FALSE(isTextLike(bytesOf(std::string("abc\0def", 7))));
    std::vector<uint8_t> sav(1024, 0);
    for (size_t i = 0; i < sav.size(); i += 2) { sav[i] = static_cast<uint8_t>(i); sav[i + 1] = 0x01; }
    CHECK_FALSE(isTextLike(sav));
    /* the padding alone is no reason to call a file binary; the dumps keep it */
    CHECK(renderLines(textBytes, ViewOptions{View::hex, Encoding::ascii, true}).size() == 512 / 16);
}

TEST_CASE("the encoding is told from the bytes: the shifts, the 8-bit halves, KOI-7 Russian against plain English")
{
    /* the KOI-7 shifts settle it */
    CHECK(detectEncoding(bytesOf("HELLO \x0Epriwet\x0F")) == Encoding::koi7shift);
    /* 8-bit: KOI-8R letters live in 0xC0..0xFF, CP866's in 0x80..0xAF and 0xE0..0xF1 */
    CHECK(detectEncoding(bytesOf("\xF0\xD2\xC9\xD7\xC5\xD4 \xCD\xC9\xD2")) == Encoding::koi8r);
    CHECK(detectEncoding(bytesOf("\x8F\xE0\xA8\xA2\xA5\xE2 \xAC\xA8\xE0")) == Encoding::cp866);
    /* 7-bit: upper-case only, or English prose, is ASCII */
    CHECK(detectEncoding(bytesOf("EXPRESS SERVICE\r\nTYPE ANY KEY\r\n")) == Encoding::ascii);
    CHECK(detectEncoding(bytesOf("this is a plain english text file with some lines of prose in it\r\n"
                                 "and another line that says nothing much at all\r\n")) == Encoding::ascii);
    CHECK(detectEncoding(bytesOf("")) == Encoding::ascii);
    /* KOI-7 Russian in the lower-case range: the letters English hardly
     * uses - q j x and the ` { | } ~ signs - stand for common Cyrillic ones */
    CHECK(detectEncoding(bytesOf("priwet, |to prowerka. q duma`, ~to wse horo{o.\r\n"
                                 "sledu`]ij |kzemplqr fajla.\r\n")) == Encoding::koi7);
    /* a text-like file with a form feed and tabs stays what its letters say */
    CHECK(detectEncoding(bytesOf("\x0C\tA LINE\r\n")) == Encoding::ascii);
}
