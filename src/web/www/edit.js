// edit.js — the file editor of the commander: text in the machine's
// encodings, or bytes in octal / hex.
//
// Text: a textarea over the decoded file; saving encodes it back the same
// way (KOI-7, KOI-8 or CP866; a character the encoding has not becomes
// "?"), CR LF line ends as RT-11 writes them.
//
// Bytes: a grid of the file - an offset, the bytes as octal (the machine's
// own notation, as DUMP shows them) or hex, the characters - with a cursor
// that the arrows move; typing digits sets the byte's digits in turn, a
// character typed in the character column sets the byte to it.  Replace
// mode (the default) overwrites, Insert mode (the Insert key) puts a new
// byte before the cursor; Delete and Backspace remove bytes in either.
const KOI7 = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ";

// KOI-8 on the MS 0515: KOI-8R's letters at 0xC0..0xFF, and at 0x80..0xBF
// (octal 200-277) the pseudographics of ROM-B - its table of 64 glyphs at
// 157000, not KOI-8R's; ROM-A prints nothing for these codes.  Rodionov's
// monitor draws them on ROM-A from a table of its own ("koi8rod"): the single
// and the mixed rows swapped, his signs in the last row, 233 not printed.
const ROMB_GRAPH = [..."╧╨╤╡╢╖╕╥╙╘╒╜╛╞╟╓╔╗╝╚═║╦╣╩╠╬░▒▓╫╪┌┐┘└─│┬┤┴├┼█▄▌▐▀Ёё╭╮╯╰→←↑↓÷±№¤■ "];
const ROD_GRAPH = [..."┌┐┘└─│┬┤┴├┼█▄▌▐▀╔╗╝╚═║╦╣╩╠╬.▒▓╫╪╧╨╤╡╢╖╕╥╙╘╒╜╛╞╟╓°ё►◄▲▼→←↓↑÷░┌±№©"];
const KOI8_LETTERS = [..."юабцдефгхийклмнопярстужвьызшэщчъЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ"];
const CP866_HIGH = [..."АБВГДЕЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдежзийклмноп░▒▓│┤╡╢╖╕╣║╗╝╜╛┐" +
                      "└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀рстуфхцчшщъыьэюяЁёЄєЇїЎў°∙·√№¤■ "];

function koi8Char(b, graph) {
  if (b < 0x80) return String.fromCharCode(b);
  return b < 0xC0 ? graph[b - 0x80] : KOI8_LETTERS[b - 0xC0];
}

// The four ends of a line-drawing character - up, right, down, left - as
// 0 none, 1 single, 2 double.
const EDGES = new Map(Object.entries({
  "─": "0101", "│": "1010", "┌": "0110", "┐": "0011", "└": "1100", "┘": "1001", "├": "1110", "┤": "1011",
  "┬": "0111", "┴": "1101", "┼": "1111", "═": "0202", "║": "2020", "╔": "0220", "╗": "0022", "╚": "2200",
  "╝": "2002", "╠": "2220", "╣": "2022", "╦": "0222", "╩": "2202", "╬": "2222", "╒": "0210", "╓": "0120",
  "╕": "0012", "╖": "0021", "╘": "1200", "╙": "2100", "╛": "1002", "╜": "2001", "╞": "1210", "╟": "2120",
  "╡": "1012", "╢": "2021", "╤": "0212", "╥": "0121", "╧": "1202", "╨": "2101", "╪": "1212", "╫": "2121",
  "╭": "0110", "╮": "0011", "╯": "1001", "╰": "1100",
}).map(([ch, e]) => [ch, [...e].map(Number)]));
const NO_EDGES = [0, 0, 0, 0];

// How well a table's lines join in the text: +1 for each pair of neighbours
// (side by side, or one above the other) whose facing ends meet in the same
// style, -1 where one end reaches out and the other does not answer.
function joins(bytes, glyphOf) {
  const rows = [[]];
  for (const b of bytes) {
    if (b === 10) rows.push([]);
    else if (b !== 13) rows[rows.length - 1].push(EDGES.get(glyphOf(b)) ?? NO_EDGES);
  }
  const score = (out, into) => (out && into === out ? 1 : out || into ? -1 : 0);
  let total = 0;
  rows.forEach((row, y) => row.forEach((e, x) => {
    const right = row[x + 1], below = rows[y + 1]?.[x];
    if (right && (e !== NO_EDGES || right !== NO_EDGES)) total += score(e[1], right[3]);
    if (below && (e !== NO_EDGES || below !== NO_EDGES)) total += score(e[2], below[0]);
  }));
  return total;
}

// Pairs of different letters side by side: words, which frames are not.
function letterPairs(bytes, isLetter) {
  let n = 0;
  for (let i = 0; i + 1 < bytes.length; ++i)
    if (bytes[i] !== bytes[i + 1] && isLetter(bytes[i]) && isLetter(bytes[i + 1])) ++n;
  return n;
}

// Text, or bytes?  A text file is printable characters (the 8-bit letters
// and the pseudographics of KOI-8 / CP866 included), CR LF TAB FF ESC, the
// KOI-7 shifts, ^Z, with nothing else but the zero padding of its last block.
export function looksLikeText(bytes) {
  let end = bytes.length;
  while (end > 0 && (bytes[end - 1] === 0 || bytes[end - 1] === 26)) --end;
  const sample = bytes.subarray(0, Math.min(end, 4096));
  if (!sample.length) return true;
  let text = 0;
  for (const v of sample)
    if ((v >= 0x20 && v !== 0x7F) || [9, 10, 12, 13, 14, 15, 26, 27].includes(v)) ++text;
  return text >= sample.length * 0.97 && !sample.includes(0);
}

// Letter frequencies, per cent, of English a-z and of Russian in the KOI-7
// order (ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ), both over the bytes 0x60..0x7F;
// 0 for the English punctuation there (` { | } ~ DEL).
const EN_FREQ = [0, 8.2, 1.5, 2.8, 4.3, 12.7, 2.2, 2.0, 6.1, 7.0, 0.15, 0.77, 4.0, 2.4, 6.7, 7.5, 1.9, 0.095, 6.0, 6.3, 9.1, 2.8, 0.98, 2.4, 0.15, 2.0, 0.074, 0, 0, 0, 0, 0];
const RU_FREQ = [0.64, 8.0, 1.6, 0.48, 3.0, 8.5, 0.26, 1.7, 0.97, 7.4, 1.2, 3.5, 4.4, 3.2, 6.7, 10.9, 2.8, 2.0, 4.7, 5.5, 6.3, 2.6, 0.94, 4.5, 1.7, 1.9, 1.6, 0.73, 0.32, 0.36, 1.4, 0.04];

// The encoding a text is most likely in.  8-bit: the letters that make
// words and the lines that join - KOI-8's (letters at 0xC0..0xFF, frames at
// 0x80..0xBF) against CP866's (letters at 0x80..0xAF and 0xE0..0xF1, frames
// at 0xB0..0xDF); in KOI-8, ROM-B's pseudographics unless Rodionov's join
// better.  A ^N or ^O: KOI-7 with the РУС / ЛАТ shifts (10L01.DOC on the
// Mihin disk).  Else the bytes 0x60..0x7F are either English lowercase or
// Russian in KOI-7: the one whose letter frequencies fit them better wins,
// KOI-7 only by a clear margin (0.2 nats a letter, 20 letters at least) -
// Russian transliterated in Latin letters, and program text, read as
// English.  Nothing there at all: ASCII.
export function guessEncoding(bytes) {
  const b = bytes.subarray(0, 8192);
  if (b.some((v) => v >= 0x80)) {
    const romB = joins(b, (v) => (v >= 0x80 && v < 0xC0 ? ROMB_GRAPH[v - 0x80] : null));
    const rod = joins(b, (v) => (v >= 0x80 && v < 0xC0 ? ROD_GRAPH[v - 0x80] : null));
    const cpJoins = joins(b, (v) => (v >= 0x80 ? CP866_HIGH[v - 0x80] : null));
    const koi8 = letterPairs(b, (v) => v >= 0xC0) + Math.max(romB, rod, 0);
    const cp866 = letterPairs(b, (v) => (v >= 0x80 && v <= 0xAF) || (v >= 0xE0 && v <= 0xF1)) + Math.max(cpJoins, 0);
    if (cp866 > koi8) return "ibm866";
    return rod > romB ? "koi8rod" : "koi8";
  }
  if (b.some((v) => v === 0x0E || v === 0x0F)) return "koi7s";      // the terminal's РУС / ЛАТ shifts
  let en = 0, ru = 0, n = 0;
  for (const v of b) {
    if (v < 0x60 || v > 0x7F) continue;
    en += Math.log((EN_FREQ[v - 0x60] + 0.02) / 100);
    ru += Math.log((RU_FREQ[v - 0x60] + 0.02) / 100);
    ++n;
  }
  return n >= 20 && (ru - en) / n >= 0.2 ? "koi7" : "ascii";
}

// ── the encodings both ways ─────────────────────────────────────────────────
// KOI-7 with the terminal's РУС / ЛАТ shifts: ^N (0x0E) makes 0x40..0x7F
// Cyrillic - lowercase at 0x40..0x5F, uppercase at 0x60..0x7F - until ^O
// (0x0F) brings Latin back; a text starts in Latin.  The shifts themselves
// are not shown.
function decodeKoi7Shifted(bytes) {
  let out = "", rus = false;
  for (const b of bytes) {
    if (b === 0x0E) rus = true;
    else if (b === 0x0F) rus = false;
    else if (rus && b >= 0x60 && b <= 0x7F) out += KOI7[b - 0x60];
    else if (rus && b >= 0x40 && b <= 0x5F) out += KOI7[b - 0x40].toLowerCase();
    else out += String.fromCharCode(b);
  }
  return out;
}

// The way back: a Cyrillic letter puts ^N before it when Latin was on, a
// Latin letter (or anything else of 0x40..0x7F) puts ^O when Cyrillic was.
function encodeKoi7Shifted(text) {
  const out = [];
  let rus = false;
  for (const ch of text) {
    const upper = KOI7.indexOf(ch), lower = ch === ch.toUpperCase() ? -1 : KOI7.indexOf(ch.toUpperCase());
    const c = ch.codePointAt(0);
    if (upper >= 0 || lower >= 0) {
      if (!rus) { out.push(0x0E); rus = true; }
      out.push(upper >= 0 ? 0x60 + upper : 0x40 + lower);
    } else if (c >= 0x40 && c <= 0x7F) {
      if (rus) { out.push(0x0F); rus = false; }
      out.push(c);
    } else {
      out.push(c < 128 ? c : 0x3F);
    }
  }
  return Uint8Array.from(out);
}

export function decodeBytes(bytes, enc) {
  if (enc === "koi7s") return decodeKoi7Shifted(bytes);
  if (enc === "ascii") {                 // 7-bit: a byte above 127 is a "."
    let out = "";
    for (const b of bytes) out += b < 128 ? String.fromCharCode(b) : ".";
    return out;
  }
  if (enc === "koi7") {
    let out = "";
    for (const b of bytes) out += b >= 0x60 && b <= 0x7F ? KOI7[b - 0x60] : String.fromCharCode(b);
    return out;
  }
  if (enc === "koi8" || enc === "koi8rod") {
    const graph = enc === "koi8" ? ROMB_GRAPH : ROD_GRAPH;
    let out = "";
    for (const b of bytes) out += koi8Char(b, graph);
    return out;
  }
  return new TextDecoder(enc).decode(bytes);
}

const encoders = new Map();
function encoderFor(enc) {
  if (encoders.has(enc)) return encoders.get(enc);
  const map = new Map();
  if (enc === "koi7") {
    for (let i = 0; i < KOI7.length; ++i) map.set(KOI7[i], 0x60 + i);
    for (let i = 0; i < KOI7.length; ++i) map.set(KOI7[i].toLowerCase(), 0x60 + i);
  } else if (enc !== "ascii") {          // ascii: nothing above 127 - a "?" for what has no byte
    for (let b = 128; b < 256; ++b) {
      const ch = decodeBytes(new Uint8Array([b]), enc);
      if (!map.has(ch)) map.set(ch, b);  // a glyph twice (Rodionov's ┌): its first code
    }
  }
  encoders.set(enc, map);
  return map;
}

export function encodeText(text, enc) {
  if (enc === "koi7s") return encodeKoi7Shifted(text);
  const map = encoderFor(enc);
  const out = [];
  for (const ch of text) {
    const c = ch.codePointAt(0);
    if (c < 128 && !(enc === "koi7" && c >= 0x60 && c <= 0x7F && !map.has(ch))) out.push(c);
    else out.push(map.get(ch) ?? 0x3F);
  }
  return Uint8Array.from(out);
}

// A byte as a character for the grid (the encoding's, or "." for a control).
function glyph(b, enc) {
  if (b < 32 || b === 127) return ".";
  return decodeBytes(new Uint8Array([b]), enc);
}

// ── the byte grid ───────────────────────────────────────────────────────────
export class ByteEditor {
  // `box` gets the grid; `bytes` is copied; `enc` names the character column's encoding.
  constructor(box, bytes, { radix = "oct", enc = "koi7" } = {}) {
    this.box = box;
    this.bytes = Array.from(bytes);
    this.radix = radix;
    this.enc = enc;
    this.pos = 0;            // the cursor's byte
    this.digit = 0;          // the digit of it being typed
    this.column = "num";     // "num" | "chr"
    this.insert = false;
    this.changed = false;
    this.perLine = 16;
    this.shape = "";             // what the grid was drawn for; "" - not yet
    this.shown = 0;              // the cursor's byte as drawn
    this.grid = document.createElement("pre");
    this.grid.className = "ed-grid";
    this.grid.tabIndex = 0;
    this.grid.addEventListener("keydown", (e) => this.key(e));
    this.grid.addEventListener("click", (e) => this.click(e));
    this.status = document.createElement("div");
    this.status.className = "ed-status";
    box.replaceChildren(this.grid, this.status);
    this.render();
  }

  get width() { return this.radix === "oct" ? 3 : 2; }
  get base() { return this.radix === "oct" ? 8 : 16; }
  fmt(b) { return b.toString(this.base).padStart(this.width, "0"); }
  setRadix(r) { this.radix = r; this.digit = 0; this.render(); }
  setEncoding(e) { this.enc = e; this.render(); }
  focus() { this.grid.focus(); }
  result() { return Uint8Array.from(this.bytes); }

  // The grid: a div a line.  Drawn whole when its shape changes (the size,
  // the mode, the radix, the encoding); else only the line the cursor left
  // and the one it is on - a byte changes under the cursor alone.
  render() {
    const shape = `${this.bytes.length} ${this.insert} ${this.radix} ${this.enc} ${this.perLine}`;
    if (shape === this.shape && this.grid.childElementCount) {
      for (const o of new Set([this.lineOf(this.shown), this.lineOf(this.pos)])) this.drawLine(o);
    } else {
      this.grid.replaceChildren();
      const last = Math.max(this.bytes.length + (this.insert ? 1 : 0), 1);
      for (let o = 0; o < last; o += this.perLine) {
        this.grid.appendChild(document.createElement("div"));
        this.drawLine(o);
      }
      this.shape = shape;
    }
    this.shown = this.pos;
    this.grid.querySelector(".cur")?.scrollIntoView({ block: "nearest" });
    this.status.textContent = `${this.bytes.length} bytes · offset ${this.pos.toString(8)} (oct) · `
      + `${this.insert ? "INSERT" : "replace"} · ${this.radix} · Tab: digits / characters, Insert: the mode`;
  }

  lineOf(i) { return Math.floor(i / this.perLine) * this.perLine; }

  drawLine(o) {
    const el = this.grid.children[o / this.perLine];
    if (!el) return;
    const n = this.bytes.length;
    const parts = [o.toString(8).padStart(6, "0") + "  "];
    let chars = "";
    for (let i = o; i < o + this.perLine; ++i) {
      if (i < n) {
        const cell = this.fmt(this.bytes[i]);
        parts.push(i === this.pos && this.column === "num" ? `<span class="cur">${cell}</span>` : cell);
        const g = glyph(this.bytes[i], this.enc).replace(/&/g, "&amp;").replace(/</g, "&lt;");
        chars += i === this.pos && this.column === "chr" ? `<span class="cur">${g}</span>` : g;
      } else if (i === n && this.insert) {
        parts.push(i === this.pos ? `<span class="cur">${"·".repeat(this.width)}</span>` : " ".repeat(this.width));
        chars += i === this.pos && this.column === "chr" ? `<span class="cur"> </span>` : " ";
      } else {
        parts.push(" ".repeat(this.width));
        chars += " ";
      }
    }
    el.innerHTML = parts.join(" ") + "  " + chars;
  }

  click(e) {
    const rect = this.grid.getBoundingClientRect();
    const ch = this.grid.scrollWidth / Math.max(1, this.grid.firstChild?.textContent.length ?? 1);
    const lh = parseFloat(getComputedStyle(this.grid).lineHeight) || 16;
    const row = Math.floor((e.clientY - rect.top + this.grid.scrollTop) / lh);
    const col = Math.floor((e.clientX - rect.left + this.grid.scrollLeft) / ch);
    const numStart = 8, numEnd = numStart + this.perLine * (this.width + 1) - 1, chrStart = numEnd + 2;
    let i;
    if (col >= chrStart) { i = row * this.perLine + Math.min(this.perLine - 1, col - chrStart); this.column = "chr"; }
    else { i = row * this.perLine + Math.min(this.perLine - 1, Math.max(0, Math.floor((col - numStart) / (this.width + 1)))); this.column = "num"; }
    this.pos = Math.min(i, this.bytes.length - (this.insert ? 0 : 1));
    this.digit = 0;
    this.render();
    this.grid.focus();
  }

  move(d) { this.pos = Math.max(0, Math.min(this.bytes.length - (this.insert ? 0 : 1), this.pos + d)); this.digit = 0; this.render(); }

  key(e) {
    const nav = { ArrowLeft: () => this.move(-1), ArrowRight: () => this.move(1), ArrowUp: () => this.move(-this.perLine),
                  ArrowDown: () => this.move(this.perLine), Home: () => this.move(-this.bytes.length), End: () => this.move(this.bytes.length),
                  PageUp: () => this.move(-this.perLine * 16), PageDown: () => this.move(this.perLine * 16),
                  Tab: () => { this.column = this.column === "num" ? "chr" : "num"; this.digit = 0; this.render(); },
                  Insert: () => { this.insert = !this.insert; this.digit = 0; this.move(0); },
                  Delete: () => this.removeAt(this.pos), Backspace: () => { if (this.pos > 0) { this.removeAt(this.pos - 1); this.pos = Math.max(0, this.pos - 1); this.render(); } } };
    if (nav[e.key]) { e.preventDefault(); nav[e.key](); return; }
    if (e.ctrlKey || e.altKey || e.metaKey || e.key.length !== 1) return;
    e.preventDefault();
    if (this.column === "chr") { this.putChar(e.key); return; }
    const v = parseInt(e.key, this.base);
    if (Number.isNaN(v)) return;
    this.putDigit(v);
  }

  removeAt(i) {
    if (i < 0 || i >= this.bytes.length) return;
    this.bytes.splice(i, 1);
    this.changed = true;
    this.pos = Math.min(this.pos, Math.max(0, this.bytes.length - (this.insert ? 0 : 1)));
    this.digit = 0;
    this.render();
  }

  putChar(ch) {
    const b = encodeText(ch, this.enc)[0];
    if (this.insert || this.pos >= this.bytes.length) this.bytes.splice(this.pos, 0, b);
    else this.bytes[this.pos] = b;
    this.changed = true;
    this.pos = Math.min(this.pos + 1, this.bytes.length - (this.insert ? 0 : 1));
    this.render();
  }

  putDigit(v) {
    if (this.digit === 0 && (this.insert || this.pos >= this.bytes.length)) { this.bytes.splice(this.pos, 0, 0); this.inserted = true; }
    const cur = this.bytes[this.pos] ?? 0;
    const digits = this.fmt(cur).split("").map((d) => parseInt(d, this.base));
    digits[this.digit] = v;
    const value = digits.reduce((a, d) => a * this.base + d, 0);
    if (value > 255) return;                                   // an octal byte starts with 0..3
    this.bytes[this.pos] = value;
    this.changed = true;
    if (++this.digit >= this.width) {
      this.digit = 0;
      this.inserted = false;
      this.pos = Math.min(this.pos + 1, this.bytes.length - (this.insert ? 0 : 1));
    }
    this.render();
  }
}
