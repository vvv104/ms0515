// encoding_check.mjs — www/edit.js's encodings under Node: the commander's
// viewer and editor read and write the machine's texts with them.  KOI-8 on
// the MS 0515 is KOI-8R's letters with ROM-B's pseudographics at 200-277
// (not KOI-8R's); Rodionov's monitor has a table of its own there.  The
// guess must tell the two by the frames, and KOI-8 from CP866 by the words.
// The same cases as src/files/tests/test_viewer.cpp.
//
//   node src/web/encoding_check.mjs
import { decodeBytes, encodeText, guessEncoding, looksLikeText } from "./www/edit.js";

let checks = 0;
function fail(what) { throw new Error(what); }
const eq = (got, want, what) => { ++checks; if (got !== want) fail(`${what}: ${JSON.stringify(got)}, expected ${JSON.stringify(want)}`); };
const bytes = (...codes) => Uint8Array.from(codes);
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ROM-B: the mixed lines at 200-217, the double at 220-237, the single at 240-257
eq(decodeBytes(bytes(0xA0, 0xA4, 0xA6, 0xA4, 0xA1), "koi8"), "┌─┬─┐", "ROM-B single lines");
eq(decodeBytes(bytes(0x90, 0x94, 0x91, 0x95, 0x93, 0x94, 0x92), "koi8"), "╔═╗║╚═╝", "ROM-B double lines");
eq(decodeBytes(bytes(0x80, 0x82, 0x9B, 0xAB), "koi8"), "╧╤░█", "ROM-B mixed, shade, block");
eq(decodeBytes(bytes(0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5), "koi8"), "Ёё╭╮╯╰", "ROM-B Ёё and round corners");
eq(decodeBytes(bytes(0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE), "koi8"), "→←↑↓÷±№¤■", "ROM-B signs");
eq(decodeBytes(bytes(0xF0, 0xD2, 0xC9), "koi8"), "При", "KOI-8 letters");
// Rodionov's: the single lines at 200-217, the mixed at 240-257, his signs at 260-277
eq(decodeBytes(bytes(0x80, 0x84, 0x86, 0x84, 0x81), "koi8rod"), "┌─┬─┐", "Rodionov single lines");
eq(decodeBytes(bytes(0x90, 0x94, 0x91), "koi8rod"), "╔═╗", "Rodionov double lines");
eq(decodeBytes(bytes(0xA0, 0xA2), "koi8rod"), "╧╤", "Rodionov mixed lines");
eq(decodeBytes(bytes(0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB), "koi8rod"),
   "°ё►◄▲▼→←↓↑÷░", "Rodionov signs");
eq(decodeBytes(bytes(0xBC, 0xBD, 0xBE, 0xBF), "koi8rod"), "┌±№©", "Rodionov (C)");
// both ways
++checks; if (!same(encodeText("┌─┐", "koi8"), [0xA0, 0xA4, 0xA1])) fail("encode ROM-B frame");
++checks; if (!same(encodeText("┌─┐", "koi8rod"), [0x80, 0x84, 0x81])) fail("encode Rodionov frame");
++checks; if (!same(encodeText("©При", "koi8rod"), [0xBF, 0xF0, 0xD2, 0xC9])) fail("encode (C) and letters");

// the guess
const text = (s) => Uint8Array.from(s, (c) => c.charCodeAt(0));
const romb = "\xA0\xA4\xA4\xA6\xA4\xA4\xA1\r\n\xA5 NA\xA5 NB\xA5\r\n\xA3\xA4\xA4\xA8\xA4\xA4\xA2\r\n";
const rod = "\x80\x84\x84\x86\x84\x84\x81\r\n\x85 NA\x85 NB\x85\r\n\x83\x84\x84\x88\x84\x84\x82\r\n";
eq(guessEncoding(text(romb)), "koi8", "a ROM-B panel");
eq(guessEncoding(text(rod)), "koi8rod", "a Rodionov panel");
eq(guessEncoding(text("\x90\x94\x94\x91\r\n\x95  \x95\r\n\x93\x94\x94\x92\r\n")), "koi8", "double lines: ROM-B's");
const doc = "\x80\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x81\r\n" +
            "\x85 \xF7\xEE\xE9\xED\xE1\xEE\xE9\xE5 !!!\x85\r\n" +
            "\x85 \xCE\xC5\xCC\xDA\xD1 \xCE\xC1\xD6\xC9\xCD\xC1\xD4\xD8 \x85\r\n" +
            "\x83\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x84\x82\r\n";
eq(guessEncoding(text(doc)), "koi8rod", "frames round a Russian text: KOI-8, not CP866");
eq(guessEncoding(text("\xF0\xD2\xC9\xD7\xC5\xD4 \xCD\xC9\xD2")), "koi8", "KOI-8 words");
eq(guessEncoding(text("\x8F\xE0\xA8\xA2\xA5\xE2 \xAC\xA8\xE0")), "ibm866", "CP866 words");
eq(guessEncoding(text("HELLO \x0Epriwet\x0F")), "koi7s", "the РУС / ЛАТ shifts");
eq(guessEncoding(text("EXPRESS SERVICE\r\n")), "ascii", "upper-case English");

// text or bytes: a text drawn in frames is a text; a program's bytes are not
eq(looksLikeText(text(doc + doc + doc)), true, "a framed text is a text");
eq(looksLikeText(text("\x0C\tA LINE\x1B[1m\r\n")), true, "a form feed and ESC are text");
const program = Uint8Array.from({ length: 1024 }, (_, i) => (i * 37 + 11) & 0xFF);
eq(looksLikeText(program), false, "a program's bytes are not");

console.log(`encoding check: ${checks} checks passed`);
