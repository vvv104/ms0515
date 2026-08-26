// edit.js — the file editor of the commander: text in the machine's
// encodings, or bytes in octal / hex.
//
// Text: a textarea over the decoded file; saving encodes it back the same
// way (KOI-7, KOI-8R or CP866; a character the encoding has not becomes
// "?"), CR LF line ends as RT-11 writes them.
//
// Bytes: a grid of the file - an offset, the bytes as octal (the machine's
// own notation, as DUMP shows them) or hex, the characters - with a cursor
// that the arrows move; typing digits sets the byte's digits in turn, a
// character typed in the character column sets the byte to it.  Replace
// mode (the default) overwrites, Insert mode (the Insert key) puts a new
// byte before the cursor; Delete and Backspace remove bytes in either.
const KOI7 = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ";

// ── the encodings both ways ─────────────────────────────────────────────────
export function decodeBytes(bytes, enc) {
  if (enc === "koi7") {
    let out = "";
    for (const b of bytes) out += b >= 0x60 && b <= 0x7F ? KOI7[b - 0x60] : String.fromCharCode(b);
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
  } else {
    const dec = new TextDecoder(enc);
    for (let b = 128; b < 256; ++b) map.set(dec.decode(new Uint8Array([b])), b);
  }
  encoders.set(enc, map);
  return map;
}

export function encodeText(text, enc) {
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

  render() {
    const lines = [];
    const n = this.bytes.length;
    const last = Math.max(n, 1);
    for (let o = 0; o < last; o += this.perLine) {
      const parts = [o.toString(8).padStart(6, "0") + "  "];
      let chars = "";
      for (let i = o; i < o + this.perLine; ++i) {
        if (i < n) {
          const cell = this.fmt(this.bytes[i]);
          parts.push(i === this.pos && this.column === "num" ? `<span class="cur">${cell}</span>` : cell);
          const g = glyph(this.bytes[i], this.enc).replace("<", "&lt;").replace("&", "&amp;");
          chars += i === this.pos && this.column === "chr" ? `<span class="cur">${g}</span>` : g;
        } else if (i === n && this.insert) {
          parts.push(i === this.pos ? `<span class="cur">${"·".repeat(this.width)}</span>` : " ".repeat(this.width));
          chars += i === this.pos && this.column === "chr" ? `<span class="cur"> </span>` : " ";
        } else {
          parts.push(" ".repeat(this.width));
          chars += " ";
        }
      }
      lines.push(parts.join(" ") + "  " + chars);
    }
    this.grid.innerHTML = lines.join("\n");
    this.grid.querySelector(".cur")?.scrollIntoView({ block: "nearest" });
    this.status.textContent = `${this.bytes.length} bytes · offset ${this.pos.toString(8)} (oct) · `
      + `${this.insert ? "INSERT" : "replace"} · ${this.radix} · Tab: digits / characters, Insert: the mode`;
  }

  click(e) {
    const rect = this.grid.getBoundingClientRect();
    const ch = this.grid.scrollWidth / Math.max(1, this.grid.textContent.split("\n")[0].length);
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
