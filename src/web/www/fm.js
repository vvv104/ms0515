// fm.js — the files of the mounted images, two panes side by side.
//
// A commander over the RT-11 directories the module reads through the
// disk library (ms_disk_*): each pane shows one source - a floppy side or
// the HD image, the unused areas listed with the files - and the ten keys
// below, always drawn as in Midnight Commander, act on the selected file:
// F1 upload a file of the user's into the pane, F2 download to the
// computer, F3 view, F4 edit, F5 copy to the other pane, F6 rename, F7
// init the pane's volume, F8 delete, F9 squeeze, F10 quit (Esc too); Tab,
// the arrows and Enter as in the commander; Insert and Shift with the
// arrows mark files (yellow, as there), and F2 / F5 / F8 then take the
// marked ones.  The viewer's keys, as mc's:
// F1 the encoding (KOI-7, KOI-8R, CP866 in turn), F2 wrap / unwrap at the
// machine's 80 columns, F3 and F10 back, F4 text / hex / octal in turn, F5
// go to a line, F7 search (a string in the encoding; a byte sequence in
// hex / octal), the hit scrolled to and marked.  The editor's: F1 the
// encoding, F2 save, F4 the representation, F5 go to, F7 search, F8
// replace / insert (bytes), F10 back.
//
// `deps`: { sources() -> [{ id, label, path, side, linear, name }],
//           api, module(), writable(source, op) -> Promise (the image
//           unmounted around a write), say, onClose }.
import { ByteEditor, decodeBytes, encodeText } from "./edit.js?v=@STAMP@";

const LATIN_NAME = /^[A-Z0-9$]{1,6}(\.[A-Z0-9$]{1,3})?$/;
const ENCODINGS = [["koi7", "KOI-7"], ["koi8-r", "KOI-8R"], ["ibm866", "CP866"]];

const el = (tag, cls, text) => {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text !== undefined) e.textContent = text;
  return e;
};

// Text, or bytes?  A text file is printable characters (the 8-bit letters
// of KOI-8R / CP866 included), CR LF TAB ^Z, with nothing else but the
// zero padding of its last block.
function looksLikeText(bytes) {
  let end = bytes.length;
  while (end > 0 && (bytes[end - 1] === 0 || bytes[end - 1] === 26)) --end;
  const sample = bytes.subarray(0, Math.min(end, 4096));
  if (!sample.length) return true;
  let text = 0;
  for (const v of sample)
    if ((v >= 0x20 && v < 0x7F) || v === 9 || v === 10 || v === 13 || v === 26 || v >= 0xC0) ++text;
  return text >= sample.length * 0.97 && !sample.includes(0);
}

// The encoding a text is likely in: 7-bit with the KOI-7 Cyrillic range in
// use -> KOI-7, else KOI-8R (CP866 is an F-key away).
function guessEncoding(bytes) {
  const b = bytes.subarray(0, 4096);
  const high = b.some((v) => v >= 0x80);
  return !high && b.some((v) => v >= 0x60 && v <= 0x7F) ? "koi7" : "koi8-r";
}

export class Commander {
  constructor(root, deps) {
    this.root = root;
    this.deps = deps;
    this.panes = [];
    this.active = 0;
    this.v = null;                 // the viewer / editor's state while open
    this.build();
    // The keys are taken at the document while the commander is open: the
    // focus may sit on the viewer's text, a textarea or nowhere at all.
    document.addEventListener("keydown", (e) => { if (!this.root.hidden) this.key(e); });
  }

  // ── the DOM ─────────────────────────────────────────────────────────────
  build() {
    const panes = el("div", "fm-panes");
    for (let i = 0; i < 2; ++i) {
      const pane = el("div", "fm-pane");
      const src = document.createElement("select");
      const list = el("div", "fm-list");
      list.tabIndex = 0;
      const foot = el("div", "fm-foot", "");
      pane.append(src, list, foot);
      panes.appendChild(pane);
      const p = { pane, src, list, foot, files: [], free: 0, selected: -1, source: null, prev: "", marks: new Set() };
      src.addEventListener("focus", () => { p.prev = src.value; });
      src.onchange = () => { this.active = i; this.load(p, p.prev); p.prev = src.value; };
      list.addEventListener("pointerdown", () => this.activate(i));
      list.addEventListener("click", (e) => { const row = e.target.closest(".fm-row"); if (row) this.select(p, +row.dataset.i); });
      list.addEventListener("dblclick", (e) => { if (e.target.closest(".fm-row")) this.run(() => this.view()); });
      this.panes.push(p);
    }
    this.bar = el("div", "fm-bar");
    this.drawListBar();
    this.viewer = el("div", "fm-viewer");
    this.viewer.hidden = true;
    this.vbar = el("div", "fm-bar");
    this.vname = el("span", "fm-vname", "");
    this.vtext = el("pre", "fm-text", "");
    this.vtext.tabIndex = 0;                          // the arrows and PgUp / PgDn scroll it
    this.vedit = el("div", "fm-edit");
    this.viewer.append(this.vname, this.vtext, this.vedit, this.vbar);
    this.root.append(panes, this.bar, this.viewer);
  }

  // A bar of the ten keys: [label, title, action] per key, an empty label
  // for a key with nothing to do here.
  drawBar(bar, keys) {
    bar.replaceChildren();
    keys.forEach(([label, title, on], i) => {
      const b = el("button", label ? null : "none");
      b.append(el("span", "num", String(i + 1)), el("span", "label", label ?? ""));
      b.title = title ?? "";
      b.disabled = !label;
      if (on) b.onclick = () => this.run(on);
      bar.appendChild(b);
    });
  }

  drawListBar() {
    this.drawBar(this.bar, [
      ["Upload", "a file from your computer into this pane", () => this.upload()],
      ["Download", "the file to your computer", () => this.download()],
      ["View", "the file as text or bytes", () => this.view()],
      ["Edit", "the file as text, or its bytes", () => this.edit()],
      ["Copy", "to the other pane", () => this.copy()],
      ["Rename", "", () => this.rename()],
      ["Init", "the pane's volume anew - every file on it lost", () => this.init()],
      ["Delete", "", () => this.remove()],
      ["Squeeze", "the files packed, one free area", () => this.squeeze()],
      ["Quit", "back to the screen (Esc too)", () => this.close()],
    ]);
  }

  // ── open / close / the panes ────────────────────────────────────────────
  open() {
    const sources = this.deps.sources();
    for (const [i, p] of this.panes.entries()) {
      const keep = p.src.value;
      p.src.replaceChildren();
      for (const s of sources) { const o = document.createElement("option"); o.value = s.id; o.textContent = s.label; p.src.appendChild(o); }
      if (!sources.length) { p.files = []; p.source = null; this.draw(p); continue; }
      p.src.value = sources.some((s) => s.id === keep) ? keep : sources[Math.min(i, sources.length - 1)].id;
      this.load(p);
    }
    this.root.hidden = false;
    this.focusList();
  }

  // F10 / Esc: the page's Files button closes the commander (and shows the screen).
  close() { this.v = null; this.viewer.hidden = true; this.deps.onClose(); }

  activate(i) { this.active = i; for (const [k, p] of this.panes.entries()) p.pane.classList.toggle("active", k === i); }

  focusList() { this.activate(this.active); this.panes[this.active].list.focus(); }

  sourceOf(p) { return this.deps.sources().find((s) => s.id === p.src.value) ?? null; }

  // The pane's source, read; a volume with no directory (a blank image)
  // is initialised on the user's word, else the pane goes back to what it
  // showed (`prev`, the id to fall back to).
  load(p, prev) {
    const was = p.source;
    p.source = this.sourceOf(p);
    if (!was || !p.source || was.id !== p.source.id) p.marks.clear();
    p.files = [];
    p.free = 0;
    if (p.source) {
      const api = this.deps.api;
      const text = api.diskDir(p.source.path, p.source.side, p.source.linear ? 1 : 0);
      if (!text) {
        const src = p.source;                       // pinned: the panes may be redrawn meanwhile
        if (confirm(`${src.label} has no RT-11 directory.  Initialise it (INIT: a blank volume)?`)) {
          this.deps.writable(src, () => api.diskInit(src.path, src.side, src.linear ? 1 : 0))
            .then((done) => { if (!done) throw new Error(api.diskError()); p.src.value = src.id; this.load(p); })
            .catch((err) => this.deps.say("error: " + (err?.message ?? err)));
          return;
        }
        if (prev !== undefined && prev !== p.src.value) { p.src.value = prev; this.load(p); return; }
        this.deps.say("error: " + api.diskError());
      } else {
        const dir = JSON.parse(text);
        p.files = dir.files;
        p.free = dir.free;
      }
    }
    p.selected = p.files.length ? Math.min(Math.max(p.selected, 0), p.files.length - 1) : -1;
    this.draw(p);
  }

  refresh() { for (const p of this.panes) this.load(p); }

  draw(p) {
    p.list.replaceChildren();
    for (const [i, f] of p.files.entries()) {
      const row = el("div", "fm-row" + (i === p.selected ? " selected" : "") + (f.empty ? " unused" : "") + (p.marks.has(f.name) ? " marked" : ""));
      row.dataset.i = i;
      row.append(el("span", "n", f.empty ? "< UNUSED >" : f.name), el("span", "b", String(f.blocks)),
                 el("span", "d", f.date || ""), el("span", "p", f.protected ? "P" : ""));
      p.list.appendChild(row);
    }
    const files = p.files.filter((f) => !f.empty);
    for (const name of [...p.marks]) if (!files.some((f) => f.name === name)) p.marks.delete(name);
    const marked = files.filter((f) => p.marks.has(f.name));
    p.foot.textContent = !p.source ? "nothing mounted"
      : marked.length ? `${marked.length} marked, ${marked.reduce((a, f) => a + f.blocks, 0)} blocks of ${files.length} file(s), ${p.free} free`
      : `${files.length} file(s), ${files.reduce((a, f) => a + f.blocks, 0)} blocks, ${p.free} free`;
    p.list.querySelector(".selected")?.scrollIntoView({ block: "nearest" });
  }

  select(p, i) { p.selected = i; this.draw(p); }

  // Insert, Shift+arrow: the current file's mark toggled, then the move.
  mark(p, move) {
    const f = p.files[p.selected];
    if (f && !f.empty) { if (p.marks.has(f.name)) p.marks.delete(f.name); else p.marks.add(f.name); }
    this.select(p, Math.max(0, Math.min(p.files.length - 1, p.selected + move)));
  }

  // What an action works on: the marked files of the active pane, else the
  // current one (an unused area is not a file).
  targets() {
    const p = this.panes[this.active];
    if (!p.source) return [];
    const marked = p.files.filter((f) => !f.empty && p.marks.has(f.name));
    if (marked.length) return marked.map((file) => ({ pane: p, source: p.source, file }));
    const c = this.current();
    return c ? [c] : [];
  }

  // The selected file of the active pane; null on nothing; an unused area
  // is not a file (throws).
  current() {
    const p = this.panes[this.active];
    if (!(p.selected >= 0 && p.source)) return null;
    const file = p.files[p.selected];
    if (file.empty) throw new Error("that is an unused area, not a file");
    return { pane: p, source: p.source, file };
  }

  // ── the keys ─────────────────────────────────────────────────────────────
  key(e) {
    if (this.v) { this.viewerKey(e); return; }
    const p = this.panes[this.active];
    const acts = { F1: () => this.upload(), F2: () => this.download(), F3: () => this.view(), F4: () => this.edit(),
                   F5: () => this.copy(), F6: () => this.rename(), F7: () => this.init(), F8: () => this.remove(),
                   F9: () => this.squeeze(), F10: () => this.close(), Enter: () => this.view(), Escape: () => this.close(),
                   Tab: () => { this.active ^= 1; this.focusList(); },
                   Insert: () => this.mark(p, 1),
                   ArrowUp: () => e.shiftKey ? this.mark(p, -1) : this.select(p, Math.max(0, p.selected - 1)),
                   ArrowDown: () => e.shiftKey ? this.mark(p, 1) : this.select(p, Math.min(p.files.length - 1, p.selected + 1)),
                   Home: () => this.select(p, 0), End: () => this.select(p, p.files.length - 1) };
    const a = acts[e.key];
    if (!a) return;
    e.preventDefault(); e.stopPropagation();
    this.run(a);
  }

  run(fn) { Promise.resolve().then(fn).catch((err) => this.deps.say("error: " + (err?.message ?? err))); }

  // ── the file actions ────────────────────────────────────────────────────
  bytesOf(source, name) {
    const api = this.deps.api, M = this.deps.module();
    const n = api.diskGet(source.path, source.side, source.linear ? 1 : 0, name);
    if (n < 0) throw new Error(api.diskError());
    return M.HEAPU8.slice(api.diskData(), api.diskData() + n);
  }

  putBytes(source, name, bytes, file) {
    const api = this.deps.api, M = this.deps.module();
    const ptr = M._malloc(bytes.length || 1);
    M.HEAPU8.set(bytes, ptr);
    const [y, m, d] = file?.date ? file.date.split("-").map(Number) : [0, 0, 0];
    try {
      return this.deps.writable(source, () => api.diskPut(source.path, source.side, source.linear ? 1 : 0, name, ptr, bytes.length, y, m, d, file?.protected ? 1 : 0));
    } finally {
      M._free(ptr);
    }
  }

  async copy() {
    const list = this.targets();
    if (!list.length) return;
    const from = list[0].pane, to = this.panes[this.active ^ 1];
    if (!to.source) throw new Error("the other pane has no disk");
    if (to.source.id === from.source.id) throw new Error("the other pane shows the same disk");
    const taken = list.filter((c) => to.files.some((f) => f.name === c.file.name)).map((c) => c.file.name);
    if (taken.length && !confirm(`Replace on ${to.source.label}: ${taken.join(", ")}?`)) return;
    let n = 0;
    for (const c of list) {
      const bytes = this.bytesOf(c.source, c.file.name);
      if (!await this.putBytes(to.source, c.file.name, bytes, c.file)) throw new Error(`${c.file.name}: ${this.deps.api.diskError()}`);
      from.marks.delete(c.file.name);
      ++n;
    }
    this.load(to);
    this.draw(from);
    this.deps.say(n === 1 ? `${list[0].file.name} copied to ${to.source.label}` : `${n} files copied to ${to.source.label}`);
  }

  async rename() {
    const c = this.current();
    if (!c) return;
    const name = prompt(`Rename ${c.file.name} to (6.3, A-Z 0-9 $):`, c.file.name);
    if (!name || name === c.file.name) return;
    const upper = name.toUpperCase();
    if (!LATIN_NAME.test(upper)) throw new Error(`${name}: not an RT-11 name`);
    const ok = await this.deps.writable(c.source, () => this.deps.api.diskRename(c.source.path, c.source.side, c.source.linear ? 1 : 0, c.file.name, upper));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
  }

  async remove() {
    const list = this.targets().filter((c) => !c.file.protected);
    if (!list.length) { const c = this.current(); if (c?.file.protected) throw new Error(`${c.file.name} is protected`); return; }
    const names = list.map((c) => c.file.name);
    if (!confirm(`Delete from ${list[0].source.label}: ${names.length === 1 ? names[0] : names.length + " files (" + names.join(", ") + ")"}?`)) return;
    const src = list[0].source;
    const ok = await this.deps.writable(src, () => names.every((name) => this.deps.api.diskRm(src.path, src.side, src.linear ? 1 : 0, name)));
    if (!ok) throw new Error(this.deps.api.diskError());
    list[0].pane.marks.clear();
    this.refresh();
  }

  // F7: the pane's volume initialised - every file on it lost.
  async init() {
    const p = this.panes[this.active];
    if (!p.source) throw new Error("this pane has no disk");
    const files = p.files.filter((f) => !f.empty).length;
    if (!confirm(`INIT ${p.source.label}${files ? ` - its ${files} file(s) will be lost` : ""}.  Continue?`)) return;
    const src = p.source;
    const ok = await this.deps.writable(src, () => this.deps.api.diskInit(src.path, src.side, src.linear ? 1 : 0));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
  }

  // F9: the files packed to the front, the free blocks in one area at the end.
  async squeeze() {
    const p = this.panes[this.active];
    if (!p.source) throw new Error("this pane has no disk");
    if (!confirm(`Squeeze ${p.source.label}?`)) return;
    const src = p.source;
    const ok = await this.deps.writable(src, () => this.deps.api.diskSqueeze(src.path, src.side, src.linear ? 1 : 0));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
  }

  download() {
    for (const c of this.targets()) {
      const a = document.createElement("a");
      a.href = URL.createObjectURL(new Blob([this.bytesOf(c.source, c.file.name)]));
      a.download = c.file.name; a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    }
  }

  upload() {
    const p = this.panes[this.active];
    if (!p.source) throw new Error("this pane has no disk");
    const input = document.createElement("input");
    input.type = "file";
    input.onchange = () => this.run(async () => {
      const f = input.files[0];
      if (!f) return;
      const name = rt11Name(f.name);
      if (p.files.some((x) => x.name === name) && !confirm(`Replace ${name} on ${p.source.label}?`)) return;
      const bytes = new Uint8Array(await f.arrayBuffer());
      if (!await this.putBytes(p.source, name, bytes, null)) throw new Error(this.deps.api.diskError());
      this.load(p);
      this.deps.say(`${f.name} added as ${name}`);
    });
    input.click();
  }

  // ── the viewer and the editor ───────────────────────────────────────────
  // v: { mode: "view" | "edit", repr: "text" | "hex" | "oct", enc, wrap,
  //      insert, source, file, bytes (as read), textarea | editor,
  //      query, hit: { at, len } }.
  openViewer(mode) {
    const c = this.current();
    if (!c) return;
    if (mode === "edit" && c.file.protected) throw new Error(`${c.file.name} is protected`);
    const bytes = this.bytesOf(c.source, c.file.name);
    const text = looksLikeText(bytes);
    this.v = { mode, repr: text ? "text" : "oct", enc: text ? guessEncoding(bytes) : "koi7", wrap: true, insert: false,
               source: c.source, file: c.file, bytes, textarea: null, editor: null, query: "", hit: null };
    this.viewer.hidden = false;
    this.showRepr();
  }

  view() { this.openViewer("view"); }
  edit() { this.openViewer("edit"); }

  // The current content as bytes: what is being edited, or what was read.
  currentBytes() {
    const v = this.v;
    if (v.textarea) return encodeText(v.textarea.value.replace(/\r?\n/g, "\r\n"), v.enc);
    if (v.editor) return v.editor.result();
    return v.bytes;
  }

  // Show the content in the representation and mode chosen.
  showRepr() {
    const v = this.v;
    const bytes = this.currentBytes();
    v.textarea = null; v.editor = null;
    this.vtext.hidden = true; this.vedit.hidden = true;
    this.vname.textContent = `${v.file.name}  (${bytes.length} bytes)  ${v.mode}, ${v.repr}, ${v.enc}`;
    if (v.mode === "view") {
      this.vtext.hidden = false;
      this.vtext.classList.toggle("wrap", v.repr === "text" && v.wrap);
      this.renderView(bytes);
      this.vtext.scrollTop = 0;
      this.vtext.focus();
    } else if (v.repr === "text") {
      this.vedit.hidden = false;
      const ta = document.createElement("textarea");
      ta.spellcheck = false;
      ta.value = this.asText(bytes);
      this.vedit.replaceChildren(ta);
      v.textarea = ta;
      ta.focus();
    } else {
      this.vedit.hidden = false;
      v.editor = new ByteEditor(this.vedit, bytes, { radix: v.repr, enc: v.enc });
      v.editor.insert = v.insert;
      v.editor.render();
      v.editor.focus();
    }
    this.drawViewerBar();
  }

  asText(bytes) {
    let end = bytes.length;
    while (end > 0 && (bytes[end - 1] === 0 || bytes[end - 1] === 26)) --end;   // the block padding
    return decodeBytes(bytes.subarray(0, end), this.v.enc).replace(/\r\n?/g, "\n");
  }

  // The view: the text, or the dump, with the search hit marked.
  renderView(bytes) {
    const v = this.v;
    const esc = (t) => t.replace(/&/g, "&amp;").replace(/</g, "&lt;");
    if (v.repr === "text") {
      const text = this.asText(bytes);
      if (v.hit) {
        const { at, len } = v.hit;
        this.vtext.innerHTML = esc(text.slice(0, at)) + "<mark>" + esc(text.slice(at, at + len)) + "</mark>" + esc(text.slice(at + len));
      } else {
        this.vtext.textContent = text;
      }
      return;
    }
    const width = v.repr === "oct" ? 3 : 2, base = v.repr === "oct" ? 8 : 16;
    const lines = [];
    const hit = v.hit ? [v.hit.at, v.hit.at + v.hit.len] : null;
    for (let o = 0; o < Math.min(bytes.length, 65536); o += 16) {
      const chunk = bytes.subarray(o, o + 16);
      const num = [...chunk].map((b, k) => {
        const t = b.toString(base).padStart(width, "0");
        return hit && o + k >= hit[0] && o + k < hit[1] ? `<mark>${t}</mark>` : t;
      }).join(" ");
      const pad = " ".repeat((16 - chunk.length) * (width + 1));
      const chars = [...chunk].map((b) => esc(b < 32 || b === 127 ? "." : decodeBytes(new Uint8Array([b]), v.enc))).join("");
      lines.push(o.toString(8).padStart(6, "0") + "  " + num + pad + "  " + chars);
    }
    this.vtext.innerHTML = lines.join("\n");
  }

  drawViewerBar() {
    const v = this.v, view = v.mode === "view";
    const encLabel = ENCODINGS.find(([id]) => id === v.enc)[1];
    const reprLabel = { text: "Text", hex: "Hex", oct: "Octal" }[v.repr];
    this.drawBar(this.vbar, [
      [encLabel, "the encoding: KOI-7, KOI-8R, CP866 in turn", () => this.cycleEncoding()],
      view ? (v.repr === "text" ? [v.wrap ? "Unwrap" : "Wrap", "long lines at the machine's 80 columns", () => this.toggleWrap()] : [null])
           : ["Save", "the file written back", () => this.save()],
      view ? ["Quit", "back to the files (Esc too)", () => this.leaveViewer()] : [null],
      [reprLabel, "text, hex, octal in turn", () => this.cycleRepr()],
      ["Goto", "a line (text) or an offset (bytes)", () => this.goto()],
      [null],
      ["Search", v.repr === "text" ? "a string, in the encoding" : "a byte sequence in the digits shown", () => this.search()],
      !view && v.repr !== "text" ? [v.insert ? "Insert" : "Replace", "typing over, or inserting", () => this.toggleInsert()] : [null],
      [null],
      ["Quit", "back to the files (Esc too)", () => this.leaveViewer()],
    ]);
  }

  viewerKey(e) {
    const v = this.v, view = v.mode === "view";
    const acts = { F1: () => this.cycleEncoding(),
                   F2: () => view ? (v.repr === "text" && this.toggleWrap()) : this.save(),
                   F3: () => view && this.leaveViewer(), F4: () => this.cycleRepr(), F5: () => this.goto(), F7: () => this.search(),
                   F8: () => !view && v.repr !== "text" && this.toggleInsert(),
                   F10: () => this.leaveViewer(), Escape: () => this.leaveViewer() };
    const a = acts[e.key];
    if (!a) return;
    e.preventDefault(); e.stopPropagation();
    this.run(a);
  }

  // Another encoding or representation: the content is kept as bytes first.
  rekey(change) {
    const v = this.v;
    v.bytes = this.currentBytes();
    change();
    v.hit = null;
    this.showRepr();
  }

  cycleEncoding() {
    const ids = ENCODINGS.map(([id]) => id);
    this.rekey(() => { this.v.enc = ids[(ids.indexOf(this.v.enc) + 1) % ids.length]; });
  }

  cycleRepr() {
    const order = ["text", "hex", "oct"];
    this.rekey(() => { this.v.repr = order[(order.indexOf(this.v.repr) + 1) % order.length]; });
  }

  toggleWrap() {
    this.v.wrap = !this.v.wrap;
    this.vtext.classList.toggle("wrap", this.v.wrap);
    this.drawViewerBar();
    this.vtext.focus();
  }

  toggleInsert() {
    const v = this.v;
    v.insert = !v.insert;
    if (v.editor) { v.editor.insert = v.insert; v.editor.move(0); v.editor.focus(); }
    this.drawViewerBar();
  }

  // F5: a line of the text, or an offset (octal / hex, as shown) of the bytes.
  goto() {
    const v = this.v;
    if (v.repr === "text") {
      const n = parseInt(prompt("Go to line:", "1") ?? "", 10);
      if (!(n >= 1)) return;
      const text = v.textarea ? v.textarea.value : this.asText(this.currentBytes());
      let at = 0;
      for (let line = 1; line < n && at >= 0; ++line) at = text.indexOf("\n", at) + 1 || -1;
      if (at < 0) throw new Error(`no line ${n}`);
      const end = text.indexOf("\n", at);
      this.showAt(at, (end < 0 ? text.length : end) - at, text);
    } else {
      const base = v.repr === "oct" ? 8 : 16;
      const at = parseInt(prompt(`Go to offset (${v.repr === "oct" ? "octal" : "hex"}):`, "0") ?? "", base);
      const bytes = this.currentBytes();
      if (!(at >= 0 && at < bytes.length)) return;
      this.showAt(at, 1);
    }
  }

  // F7: a string in the encoding, or - in hex / octal - a byte sequence
  // ("101 102 077"); the next hit after the last one, marked and shown.
  search() {
    const v = this.v;
    const query = prompt(v.repr === "text" ? "Search:" : `Search bytes (${v.repr === "oct" ? "octal" : "hex"}, spaces between):`, v.query);
    if (!query) return;
    const from = v.hit && query === v.query ? v.hit.at + 1 : 0;
    v.query = query;
    if (v.repr === "text") {
      const text = v.textarea ? v.textarea.value : this.asText(this.currentBytes());
      const at = text.toLowerCase().indexOf(query.toLowerCase(), from);
      if (at < 0) throw new Error(`"${query}" not found${from ? " below" : ""}`);
      this.showAt(at, query.length, text);
    } else {
      const base = v.repr === "oct" ? 8 : 16;
      const seq = query.trim().split(/[\s,]+/).map((t) => parseInt(t, base));
      if (!seq.length || seq.some((b) => !(b >= 0 && b < 256))) throw new Error("not a byte sequence in the digits shown");
      const bytes = this.currentBytes();
      let at = -1;
      for (let i = from; i + seq.length <= bytes.length; ++i) {
        let k = 0;
        while (k < seq.length && bytes[i + k] === seq[k]) ++k;
        if (k === seq.length) { at = i; break; }
      }
      if (at < 0) throw new Error(`the bytes not found${from ? " below" : ""}`);
      this.showAt(at, seq.length);
    }
  }

  // Mark and show a place: a text span or a byte span.
  showAt(at, len, text) {
    const v = this.v;
    v.hit = { at, len };
    if (v.textarea) {
      const ta = v.textarea;
      ta.focus();
      ta.setSelectionRange(at, at + len);
      const line = (text ?? ta.value).slice(0, at).split("\n").length - 1;
      ta.scrollTop = Math.max(0, line * (parseFloat(getComputedStyle(ta).lineHeight) || 16) - ta.clientHeight / 2);
    } else if (v.editor) {
      v.editor.pos = at; v.editor.digit = 0; v.editor.render(); v.editor.focus();
    } else {
      this.renderView(this.currentBytes());
      this.vtext.querySelector("mark")?.scrollIntoView({ block: "center" });
      this.vtext.focus();
    }
  }

  async save() {
    const v = this.v;
    const bytes = this.currentBytes();
    if (!await this.putBytes(v.source, v.file.name, bytes, v.file)) throw new Error(this.deps.api.diskError());
    this.deps.say(`${v.file.name} saved (${bytes.length} bytes)`);
    this.v = null;
    this.viewer.hidden = true;
    this.refresh();
    this.focusList();
  }

  changed() {
    const v = this.v;
    if (v.mode !== "edit") return false;
    if (v.editor) return v.editor.changed;
    return v.textarea ? v.textarea.value !== this.asText(v.bytes) : false;
  }

  leaveViewer() {
    if (this.v && this.changed() && !confirm("Leave without saving?")) return;
    this.v = null;
    this.viewer.hidden = true;
    this.focusList();
  }
}

// A host file name as an RT-11 6.3 name: A-Z, 0-9 and $ of it, 6 + 3.
export function rt11Name(fileName) {
  const base = fileName.toUpperCase().replace(/[^A-Z0-9$.]/g, "");
  const dot = base.indexOf(".");
  const stem = (dot < 0 ? base : base.slice(0, dot)).slice(0, 6);
  const ext = dot < 0 ? "" : base.slice(dot + 1).replace(/\./g, "").slice(0, 3);
  if (!stem) throw new Error(`${fileName}: no RT-11 name in it (A-Z, 0-9, $)`);
  return ext ? `${stem}.${ext}` : stem;
}
