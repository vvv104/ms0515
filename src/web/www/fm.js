// fm.js — the files of the mounted images, two panes side by side.
//
// A commander over the RT-11 directories the module reads through the
// disk library (ms_disk_*): each pane shows one source - a floppy side or
// the HD image - and the buttons below act on the selected file: View
// (text in KOI-7 / KOI-8R / CP866, or a hex dump), Copy to the other pane, Rename,
// Delete; Download saves the file to the computer, Upload adds a file of
// the user's to the active pane.  F3 / F5 / F6 / F8, Tab, the arrows and
// Enter do what they did in the commander this is named after; Esc closes.
//
// `deps`: { sources() -> [{ id, label, path, side, linear, name }],
//           api, module(), writable(source, op) -> Promise (the image
//           unmounted around a write), onClose }.
const LATIN_NAME = /^[A-Z0-9$]{1,6}(\.[A-Z0-9$]{1,3})?$/;
// KOI-7 N1: the machine's 7-bit texts keep Latin in 0x40-0x5F and put the
// Cyrillic capitals in 0x60-0x7F.
const KOI7 = "ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ";
function decodeKoi7(bytes) {
  let out = "";
  for (const b of bytes) out += b >= 0x60 && b <= 0x7F ? KOI7[b - 0x60] : String.fromCharCode(b);
  return out;
}

export class Commander {
  constructor(root, deps) {
    this.root = root;
    this.deps = deps;
    this.panes = [];
    this.active = 0;
    this.build();
    root.addEventListener("keydown", (e) => this.key(e));
  }

  // ── the DOM ─────────────────────────────────────────────────────────────
  build() {
    const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
    const panes = el("div", "fm-panes");
    for (let i = 0; i < 2; ++i) {
      const pane = el("div", "fm-pane");
      const src = document.createElement("select");
      const list = el("div", "fm-list");
      list.tabIndex = 0;
      const foot = el("div", "fm-foot", "");
      pane.append(src, list, foot);
      panes.appendChild(pane);
      const p = { pane, src, list, foot, files: [], selected: -1, source: null };
      src.onchange = () => { this.active = i; this.load(p); };
      list.addEventListener("pointerdown", () => this.activate(i));
      list.addEventListener("click", (e) => { const row = e.target.closest(".fm-row"); if (row) this.select(p, +row.dataset.i); });
      list.addEventListener("dblclick", (e) => { if (e.target.closest(".fm-row")) this.view(); });
      this.panes.push(p);
    }
    const bar = el("div", "fm-bar");
    const b = (label, key, on) => { const x = el("button", null, label); x.title = key; x.onclick = () => this.run(on); bar.appendChild(x); return x; };
    b("View", "F3", () => this.view());
    b("Copy", "F5: to the other pane", () => this.copy());
    b("Rename", "F6", () => this.rename());
    b("Delete", "F8", () => this.remove());
    bar.appendChild(el("span", "fm-gap"));
    b("Download", "the file to your computer", () => this.download());
    b("Upload", "a file from your computer into this pane", () => this.upload());
    bar.appendChild(el("span", "fm-gap"));
    b("Close", "Esc", () => this.close());
    const viewer = el("div", "fm-viewer");
    viewer.hidden = true;
    const vbar = el("div", "fm-bar");
    const enc = document.createElement("select");
    for (const [v, t] of [["koi7", "KOI-7"], ["koi8-r", "KOI-8R"], ["ibm866", "CP866"], ["hex", "hex"]]) { const o = document.createElement("option"); o.value = v; o.textContent = t; enc.appendChild(o); }
    enc.onchange = () => this.render();
    const vname = el("span", "fm-vname", "");
    const vclose = el("button", null, "Back");
    vclose.onclick = () => { viewer.hidden = true; this.focusList(); };
    vbar.append(vname, enc, el("span", "fm-gap"), vclose);
    const vtext = el("pre", "fm-text", "");
    viewer.append(vbar, vtext);
    this.root.append(panes, bar, viewer);
    this.viewer = { box: viewer, enc, name: vname, text: vtext, bytes: null };
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

  close() { this.viewer.box.hidden = true; this.root.hidden = true; this.deps.onClose(); }

  activate(i) { this.active = i; for (const [k, p] of this.panes.entries()) p.pane.classList.toggle("active", k === i); }

  focusList() { this.activate(this.active); this.panes[this.active].list.focus(); }

  sourceOf(p) { return this.deps.sources().find((s) => s.id === p.src.value) ?? null; }

  load(p) {
    p.source = this.sourceOf(p);
    p.files = [];
    if (p.source) {
      const text = this.deps.api.diskDir(p.source.path, p.source.side, p.source.linear ? 1 : 0);
      if (!text) { this.deps.say("error: " + this.deps.api.diskError()); }
      else p.files = JSON.parse(text);
    }
    p.selected = p.files.length ? Math.min(Math.max(p.selected, 0), p.files.length - 1) : -1;
    this.draw(p);
  }

  refresh() { for (const p of this.panes) this.load(p); }

  draw(p) {
    p.list.replaceChildren();
    for (const [i, f] of p.files.entries()) {
      const row = document.createElement("div");
      row.className = "fm-row" + (i === p.selected ? " selected" : "");
      row.dataset.i = i;
      const cell = (cls, t) => { const c = document.createElement("span"); c.className = cls; c.textContent = t; row.appendChild(c); };
      cell("n", f.name); cell("b", String(f.blocks)); cell("d", f.date || ""); cell("p", f.protected ? "P" : "");
      p.list.appendChild(row);
    }
    p.foot.textContent = p.source ? `${p.files.length} file(s), ${p.files.reduce((a, f) => a + f.blocks, 0)} blocks` : "nothing mounted";
    p.list.querySelector(".selected")?.scrollIntoView({ block: "nearest" });
  }

  select(p, i) { p.selected = i; this.draw(p); }

  current() {
    const p = this.panes[this.active];
    return p.selected >= 0 && p.source ? { pane: p, source: p.source, file: p.files[p.selected] } : null;
  }

  // ── the keys ─────────────────────────────────────────────────────────────
  key(e) {
    if (!this.viewer.box.hidden) { if (e.key === "Escape") { this.viewer.box.hidden = true; this.focusList(); e.preventDefault(); } return; }
    const p = this.panes[this.active];
    const acts = { F3: () => this.view(), F5: () => this.copy(), F6: () => this.rename(), F8: () => this.remove(), Enter: () => this.view(), Escape: () => this.close(),
                   Tab: () => { this.active ^= 1; this.focusList(); },
                   ArrowUp: () => this.select(p, Math.max(0, p.selected - 1)),
                   ArrowDown: () => this.select(p, Math.min(p.files.length - 1, p.selected + 1)),
                   Home: () => this.select(p, 0), End: () => this.select(p, p.files.length - 1) };
    const a = acts[e.key];
    if (!a) return;
    e.preventDefault(); e.stopPropagation();
    this.run(a);
  }

  run(fn) { Promise.resolve().then(fn).catch((err) => this.deps.say("error: " + (err?.message ?? err))); }

  // ── the actions ─────────────────────────────────────────────────────────
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

  view() {
    const c = this.current();
    if (!c) return;
    this.viewer.bytes = this.bytesOf(c.source, c.file.name);
    this.viewer.name.textContent = `${c.file.name}  (${this.viewer.bytes.length} bytes)`;
    // A guess at the encoding: binary -> hex; 7-bit text using the KOI-7
    // Cyrillic range -> KOI-7; else KOI-8R (CP866 is a click away).
    const b = this.viewer.bytes.subarray(0, 4096);
    const control = b.filter((v) => v < 32 && v !== 13 && v !== 10 && v !== 9 && v !== 26 && v !== 0).length;
    const high = b.some((v) => v >= 0x80);
    const koi7 = !high && b.some((v) => v >= 0x60 && v <= 0x7F);
    this.viewer.enc.value = control > b.length / 20 ? "hex" : koi7 ? "koi7" : "koi8-r";
    this.viewer.box.hidden = false;
    this.render();
  }

  render() {
    const bytes = this.viewer.bytes, enc = this.viewer.enc.value;
    if (enc === "hex") {
      const lines = [];
      for (let o = 0; o < Math.min(bytes.length, 65536); o += 16) {
        const chunk = bytes.subarray(o, o + 16);
        const hex = [...chunk].map((v) => v.toString(16).padStart(2, "0")).join(" ");
        const asc = [...chunk].map((v) => v >= 32 && v < 127 ? String.fromCharCode(v) : ".").join("");
        lines.push(o.toString(8).padStart(6, "0") + "  " + hex.padEnd(47) + "  " + asc);
      }
      this.viewer.text.textContent = lines.join("\n");
    } else {
      let end = bytes.length;
      while (end > 0 && (bytes[end - 1] === 0 || bytes[end - 1] === 26)) --end;   // the block padding
      const text = bytes.subarray(0, end);
      this.viewer.text.textContent = (enc === "koi7" ? decodeKoi7(text) : new TextDecoder(enc).decode(text)).replace(/\r\n?/g, "\n");
    }
  }

  async copy() {
    const c = this.current();
    if (!c) return;
    const to = this.panes[this.active ^ 1];
    if (!to.source) throw new Error("the other pane has no disk");
    if (to.source.id === c.source.id) throw new Error("the other pane shows the same disk");
    if (to.files.some((f) => f.name === c.file.name) && !confirm(`Replace ${c.file.name} on ${to.source.label}?`)) return;
    const bytes = this.bytesOf(c.source, c.file.name);
    if (!await this.putBytes(to.source, c.file.name, bytes, c.file)) throw new Error(this.deps.api.diskError());
    this.load(to);
    this.deps.say(`${c.file.name} copied to ${to.source.label}`);
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
    const c = this.current();
    if (!c) return;
    if (c.file.protected) throw new Error(`${c.file.name} is protected`);
    if (!confirm(`Delete ${c.file.name} from ${c.source.label}?`)) return;
    const ok = await this.deps.writable(c.source, () => this.deps.api.diskRm(c.source.path, c.source.side, c.source.linear ? 1 : 0, c.file.name));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
  }

  download() {
    const c = this.current();
    if (!c) return;
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([this.bytesOf(c.source, c.file.name)]));
    a.download = c.file.name; a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
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
