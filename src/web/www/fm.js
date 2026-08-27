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
// arrows mark files (yellow, as there), and F2 / F5 / F6 / F8 then take
// the marked ones.  Every change to a disk is confirmed in a dialog of the
// commander's own (centred, Enter / Esc); F5 on one file and F6 (RenMove)
// take a "DEV:NAME" - the disks by their system names, DZ0: .. DZ3:, HD0:
// - so a copy may land on the same disk under another name, and a rename
// to another disk is a move.  The viewer's keys, as mc's:
// F1 text / octal / hex in turn, F2 wrap / unwrap at the machine's 80
// columns, F3 and F10 back, F4 the encoding (ASCII, KOI-8R, KOI-7, KOI-7
// with the РУС / ЛАТ shifts ^N / ^O, CP866 in turn), F5 go to a line, F7 search (a string in the encoding; a byte
// sequence in hex / octal), the hit scrolled to and marked.  The editor's:
// F1 the representation, F2 save, F4 the encoding, F5 go to, F7 search, F8
// replace / insert (bytes), F10 back.
//
// A file that holds an RT-11 directory when read linearly - a logical disk
// - is entered with Enter as if a directory: ".." (or Backspace) leads out.
//
// `deps`: { sources() -> [{ id, label, path, side, linear, name }],
//           api, module(), writable(source, op) -> Promise (the image
//           unmounted around a write), say, onClose }.
import { ByteEditor, decodeBytes, encodeText } from "./edit.js?v=@STAMP@";
import { makeZip } from "./zip.js?v=@STAMP@";

const LATIN_NAME = /^[A-Z0-9$]{1,6}(\.[A-Z0-9$]{1,3})?$/;
const DEV_NAME = /^(?:([A-Z]+\d*):(?:([A-Z0-9$.]+)\/)?)?\s*([A-Z0-9$.]+)$/;   // "DZ1:NAME.EXT", "DZ1:VOL.DSK/NAME.EXT" or "NAME.EXT"
const ENCODINGS = [["ascii", "ASCII"], ["koi8-r", "KOI-8R"], ["koi7", "KOI-7"], ["koi7s", "KOI-7 РУС/ЛАТ"], ["ibm866", "CP866"]];

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
    if ((v >= 0x20 && v < 0x7F) || v === 9 || v === 10 || v === 13 || v === 14 || v === 15 || v === 26 || v >= 0xC0) ++text;
  return text >= sample.length * 0.97 && !sample.includes(0);
}

// The encoding a text is likely in: 7-bit with the KOI-7 Cyrillic range in
// use -> KOI-7, else KOI-8R (CP866 is an F-key away).
// Letter frequencies, per cent, of English a-z and of Russian in the KOI-7
// order (ЮАБЦДЕФГХИЙКЛМНОПЯРСТУЖВЬЫЗШЭЩЧЪ), both over the bytes 0x60..0x7F;
// 0 for the English punctuation there (` { | } ~ DEL).
const EN_FREQ = [0, 8.2, 1.5, 2.8, 4.3, 12.7, 2.2, 2.0, 6.1, 7.0, 0.15, 0.77, 4.0, 2.4, 6.7, 7.5, 1.9, 0.095, 6.0, 6.3, 9.1, 2.8, 0.98, 2.4, 0.15, 2.0, 0.074, 0, 0, 0, 0, 0];
const RU_FREQ = [0.64, 8.0, 1.6, 0.48, 3.0, 8.5, 0.26, 1.7, 0.97, 7.4, 1.2, 3.5, 4.4, 3.2, 6.7, 10.9, 2.8, 2.0, 4.7, 5.5, 6.3, 2.6, 0.94, 4.5, 1.7, 1.9, 1.6, 0.73, 0.32, 0.36, 1.4, 0.04];

// The encoding a text is most likely in.  A byte above 0x7F makes it
// KOI-8R; a ^N or ^O KOI-7 with the РУС / ЛАТ shifts (10L01.DOC on the
// Mihin disk).  Else the bytes 0x60..0x7F are either English lowercase or
// Russian in KOI-7: the one whose letter frequencies fit them better wins,
// KOI-7 only by a clear margin (0.2 nats a letter, 20 letters at least) -
// Russian transliterated in Latin letters, and program text, read as
// English.  Nothing there at all: ASCII.
export function guessEncoding(bytes) {
  const b = bytes.subarray(0, 8192);
  if (b.some((v) => v >= 0x80)) return "koi8-r";
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
    // Alt held shows the other meanings of the keys; let go, or the window
    // left (Alt+Tab), brings the usual ones back.
    document.addEventListener("keyup", (e) => { if (e.key === "Alt") { e.preventDefault(); this.altBar(false); } });
    window.addEventListener("blur", () => this.altBar(false));
    this.alt = false;
  }

  // ── the DOM ─────────────────────────────────────────────────────────────
  build() {
    const panes = el("div", "fm-panes");
    for (let i = 0; i < 2; ++i) {
      const pane = el("div", "fm-pane");
      const src = document.createElement("select");
      const head = el("div", "fm-head", "");
      const list = el("div", "fm-list");
      list.tabIndex = 0;
      const foot = el("div", "fm-foot", "");
      pane.append(src, head, list, foot);
      panes.appendChild(pane);
      const p = { pane, src, head, list, foot, files: [], free: 0, volume: null, selected: -1, source: null, prev: "", marks: new Set(), nested: null };
      src.addEventListener("focus", () => { p.prev = src.value; });
      src.onchange = () => { this.active = i; this.load(p, p.prev); p.prev = src.value; };
      list.addEventListener("pointerdown", () => this.activate(i));
      list.addEventListener("click", (e) => { const row = e.target.closest(".fm-row"); if (row) this.select(p, +row.dataset.i); });
      list.addEventListener("dblclick", (e) => { if (e.target.closest(".fm-row")) this.run(() => this.enter()); });
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
    this.dlg = el("div", "fm-dialog");
    this.dlg.hidden = true;
    this.root.append(panes, this.bar, this.viewer, this.dlg);
    this.dialog = null;            // { resolve, input } while a dialog is up
  }

  // ── the dialogs: centred in the commander, Enter / Esc ─────────────────
  // ask(text, { title, input, ok, cancel }): resolves to the input's text
  // (when there is one) or true on OK, null on Cancel.
  async ask(text, { title = "", input = null, ok = "OK", cancel = "Cancel" } = {}) {
    return (await this.showDialog(text, { title, input, buttons: [[ok, true], [cancel, null]] })).value;
  }

  // The dialog itself: a text, a field (input, its initial text), buttons
  // [[label, value], ...] - the first is the default, a value of true with a
  // field returns the field's text - and a check box (check, its label).
  // Resolves to { value, checked }; Esc gives null.
  showDialog(text, { title = "", input = null, buttons, check = null, list = null, fields = null }) {
    return new Promise((resolve) => {
      const box = el("div", "fm-dialog-box");
      if (title) box.append(el("div", "fm-dialog-title", title));
      box.append(el("div", "fm-dialog-text", text));
      let field = null, tick = null;
      if (input !== null) {
        field = document.createElement("input");
        field.type = "text"; field.value = input; field.spellcheck = false;
        box.append(field);
      }
      const inputs = [];
      if (fields) {                          // [[label, value, size], ...]: a small form
        const form = el("div", "fm-dialog-fields");
        for (const [label, value, size] of fields) {
          const i = document.createElement("input");
          i.type = "text"; i.value = value; i.spellcheck = false; i.size = size ?? 12; i.maxLength = size ?? 12;
          inputs.push(i);
          form.append(el("label", null, label), i);
        }
        box.append(form);
      }
      let box2 = null;
      if (list) {
        box2 = document.createElement("select");
        box2.size = Math.min(12, Math.max(3, list.length));
        box2.className = "fm-dialog-list";
        for (const item of list) box2.append(el("option", null, item));
        box2.selectedIndex = 0;
        box.append(box2);
      }
      if (check) {
        tick = document.createElement("input"); tick.type = "checkbox";
        const label = el("label");
        label.append(tick, " " + check);
        box.append(label);
      }
      const row = el("div", "fm-dialog-buttons");
      const bs = buttons.map(([label, value]) => ({ label, value, el: el("button", null, label) }));
      row.append(...bs.map((b) => b.el));
      box.append(row);
      this.dlg.replaceChildren(box);
      this.dlg.hidden = false;
      const pick = (value) => {
        this.dlg.hidden = true; this.dialog = null;
        resolve({ value: value === true && field ? field.value.trim() : value, checked: !!tick?.checked, index: box2 ? box2.selectedIndex : -1,
                  values: inputs.map((i) => i.value) });
        if (this.v) this.focusViewer(); else this.focusList();
      };
      this.dialog = { pick, field: field ?? inputs[0] ?? box2, buttons: bs };
      for (const b of bs) b.el.onclick = () => pick(b.value);
      if (box2) box2.ondblclick = () => pick(bs[0].value);
      (field ?? inputs[0] ?? box2 ?? bs[0].el).focus();
      if (field) field.select();
    });
  }

  // Enter the focused button (else the first), Esc null, the arrows between
  // the buttons, a button's first letter presses it (when no field is up).
  dialogKey(e) {
    const d = this.dialog, bs = d.buttons;
    const focused = bs.find((b) => b.el === document.activeElement);
    if (e.key === "Enter") { e.preventDefault(); d.pick((focused ?? bs[0]).value); }
    else if (e.key === "Escape") { e.preventDefault(); d.pick(null); }
    else if ((e.key === "ArrowLeft" || e.key === "ArrowRight") && focused) {
      e.preventDefault();
      bs[(bs.indexOf(focused) + (e.key === "ArrowRight" ? 1 : bs.length - 1)) % bs.length].el.focus();
    } else if (!d.field && e.key.length === 1) {
      const b = bs.find((x) => x.label[0].toLowerCase() === e.key.toLowerCase());
      if (b) { e.preventDefault(); d.pick(b.value); }
    }
  }

  // Before a protected file is deleted or moved: Yes / No - and, with several
  // files at hand, Cancel and a box answering for the other protected ones
  // alike.  The guard returns true, false, or CANCEL for the whole operation.
  protectedGuard(verb, several) {
    let rest = null;
    return async (c) => {
      if (!c.file.protected) return true;
      if (rest !== null) return rest;
      const buttons = several ? [["Yes", true], ["No", false], ["Cancel", CANCEL]] : [["Yes", true], ["No", false]];
      const r = await this.showDialog(`${c.file.name} is protected.  ${verb} it anyway?`,
                                      { title: "Protected", buttons, check: several ? "the same for the other protected files" : null });
      if (r.value === null) return several ? CANCEL : false;
      if (r.checked && r.value !== CANCEL) rest = r.value;
      return r.value;
    };
  }

  // The files of a list the guard lets through; null when it cancelled.
  async guarded(list, verb) {
    const guard = this.protectedGuard(verb, list.length > 1), out = [];
    for (const c of list) {
      const go = await guard(c);
      if (go === CANCEL) { this.deps.say(`${verb.toLowerCase()} cancelled - nothing done`); return null; }
      if (go) out.push(c);
    }
    return out;
  }

  focusViewer() { if (this.v?.textarea) this.v.textarea.focus(); else if (this.v?.editor) this.v.editor.focus(); else this.vtext.focus(); }

  // "DEV:NAME" -> { source (the pane's when no device is given), name }.
  parseTarget(text, here) {
    const m = DEV_NAME.exec(text.trim().toUpperCase());
    if (!m) throw new Error(`${text}: not "DZn:NAME.EXT" (or "DZn:VOL.DSK/NAME.EXT")`);
    const [, dev, vol, name] = m;
    if (!LATIN_NAME.test(name)) throw new Error(`${name}: not an RT-11 name (6.3, A-Z 0-9 $)`);
    let source = here;
    if (dev) {
      source = this.deps.sources().find((s) => s.dev === dev + ":") ?? null;
      if (!source) throw new Error(`${dev}: is not mounted`);
      if (vol) {
        source = this.volumeSource(source, vol);
        if (!this.syncVolume(source)) throw new Error(`${dev}:${vol} is not a volume`);
      }
    }
    return { source, name };
  }

  paneOf(source) { return this.panes.find((p) => p.source?.id === source.id) ?? null; }

  namesOn(source) {
    const p = this.paneOf(source);
    if (p) return p.files.filter((f) => !f.empty).map((f) => f.name);
    const text = this.deps.api.diskDir(source.path, source.side, source.linear ? 1 : 0);
    return text ? JSON.parse(text).files.filter((f) => !f.empty).map((f) => f.name) : [];
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

  // The bar under the lists: the keys as they are, or with Alt held.
  altBar(on) {
    if (this.alt === on) return;
    this.alt = on;
    if (this.root.hidden || this.v) return;
    if (on) this.drawAltBar(); else this.drawListBar();
  }

  drawAltBar() {
    this.drawBar(this.bar, [
      ["Left", "another disk on the left pane", () => this.changeDisk(0)],
      ["Right", "another disk on the right pane", () => this.changeDisk(1)],
      [], [],
      ["Volume", "the files gathered into a logical disk (a file the system mounts: MOUNT LD0: DZn:NAME)", () => this.makeVolume()],
      ["(Un)Protect", "the files /PROTECT - or /NOPROTECT when every one of them is", () => this.protect()],
      ["Find", "a pattern looked for on every mounted disk", () => this.find()],
      this.recoverKey(),
      [],
      ["System", "this pane's floppy made a system volume of the other pane's (its kit copied, the bootstrap written)", () => this.makeSystem()],
    ]);
  }

  // Alt+F8 by the row under the cursor: nothing on a file; Undelete on an
  // unused area that was a file (its name after < UNUSED >); Recover on
  // any other unused area - made a file, whatever lies in it.
  recoverKey() {
    const p = this.panes[this.active], f = p.files[p.selected];
    if (!f || !f.empty || f.up) return [];
    return f.was ? ["Undelete", `${f.was} back, or under another name`, () => this.recover()]
                 : ["Recover", "the unused area made a file of a name given", () => this.recover()];
  }

  // Alt+F1 / F2: the pane's disk list opened to pick from.
  changeDisk(i) {
    this.altBar(false);
    this.activate(i);
    const src = this.panes[i].src;
    src.focus();
    try { src.showPicker(); } catch { /* not every browser opens a list on request: the arrows work on it */ }
  }

  drawListBar() {
    this.drawBar(this.bar, [
      ["Upload", "a file from your computer into this pane", () => this.upload()],
      ["Download", "the file to your computer", () => this.download()],
      ["View", "the file as text or bytes", () => this.view()],
      ["Edit", "the file as text, or its bytes", () => this.edit()],
      ["Copy", "to the other pane", () => this.copy()],
      ["RenMove", "a new name, or another disk (DZn:NAME)", () => this.renmove()],
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
      if (!sources.length) { p.files = []; p.source = null; p.nested = null; this.draw(p); continue; }
      const chain = [];
      for (let s = p.nested; s; s = s.parent.parent ? s.parent : null) chain.unshift(s);
      if (chain.length && !sources.some((s) => s.id === chain[0].parent.id)) { chain.length = 0; p.nested = null; }
      for (const s of chain) { const o = document.createElement("option"); o.value = s.id; o.textContent = s.label; p.src.appendChild(o); }
      p.src.value = [...p.src.options].some((o) => o.value === keep) ? keep : sources[Math.min(i, sources.length - 1)].id;
      this.load(p);
    }
    this.root.hidden = false;
    this.focusList();
  }

  // F10 / Esc: the page's Files button closes the commander (and shows the screen).
  close() { this.v = null; this.viewer.hidden = true; this.deps.onClose(); }

  activate(i) { this.active = i; for (const [k, p] of this.panes.entries()) p.pane.classList.toggle("active", k === i); }

  focusList() { this.activate(this.active); this.panes[this.active].list.focus(); }

  sourceOf(p) {
    for (let s = p.nested; s; s = s.parent.parent ? s.parent : null) if (s.id === p.src.value) return s;
    return this.deps.sources().find((s) => s.id === p.src.value) ?? null;
  }

  // ── a logical disk entered as if a directory ───────────────────────────
  // The file's bytes taken out into the module's file system and read there
  // (linear, as the LD handler reads them); every change written back into
  // the file on its disk.  Its id "<disk id>/<NAME>", its device
  // "DZn:NAME/" in the dialogs, the option "DZn: disk: NAME" above the pane.
  volumeSource(parent, name) {
    return { id: `${parent.id}/${name}`, dev: `${parent.dev}${name}/`, label: `${parent.label}: ${name}`,
             name: parent.name, path: `/volumes/${parent.id.replace(/\//g, "_")}-${name}`,
             side: 0, linear: true, parent, file: name };
  }

  // The volume's file brought up to date from its disk; false when it
  // holds no RT-11 directory (then it is no volume).
  syncVolume(s) {
    const M = this.deps.module();
    M.FS.mkdirTree("/volumes");
    M.FS.writeFile(s.path, this.bytesOf(s.parent, s.file));
    return !!this.deps.api.diskDir(s.path, 0, 1);
  }

  // A write: on a disk, through the page (the image unmounted around it);
  // in a volume, on its file, which then goes back into its disk with the
  // entry's date and protection kept.
  async writable(source, op) {
    if (!source.parent) return this.deps.writable(source, op);
    if (!op()) return false;
    const bytes = this.deps.module().FS.readFile(source.path);
    const entry = this.dirOf(source.parent).find((f) => f.name === source.file);
    return this.putBytes(source.parent, source.file, bytes, entry);
  }

  dirOf(source) {
    const text = this.deps.api.diskDir(source.path, source.side, source.linear ? 1 : 0);
    return text ? JSON.parse(text).files.filter((f) => !f.empty) : [];
  }

  // Enter: a logical disk entered, ".." back out of it, any other file viewed.
  async enter() {
    const p = this.panes[this.active], f = p.files[p.selected];
    if (!f) return;
    if (f.up) { this.leaveVolume(p); return; }
    const c = this.current();
    const nested = this.volumeSource(c.source, c.file.name);
    if (!this.syncVolume(nested)) { await this.view(); return; }
    p.nested = nested;
    const o = document.createElement("option"); o.value = nested.id; o.textContent = nested.label;
    p.src.appendChild(o);
    p.src.value = nested.id; p.prev = nested.id;
    p.selected = 0;
    this.load(p);
  }

  // Backspace, Enter on "..": the pane back on the volume's disk, the
  // volume's file selected.
  leaveVolume(p) {
    const s = p.nested;
    if (!s) return;
    p.nested = s.parent.parent ? s.parent : null;
    [...p.src.options].find((o) => o.value === s.id)?.remove();
    p.src.value = s.parent.id; p.prev = s.parent.id;
    this.load(p);
    this.select(p, Math.max(0, p.files.findIndex((f) => f.name === s.file)));
  }

  // The pane's source, read; a volume with no directory (a blank image)
  // is initialised on the user's word, else the pane goes back to what it
  // showed (`prev`, the id to fall back to).
  load(p, prev) {
    const was = p.source;
    p.source = this.sourceOf(p);
    if (!was || !p.source || was.id !== p.source.id) p.marks.clear();
    p.files = [];
    p.free = 0;
    p.volume = null;
    if (p.nested && !p.source?.parent) {          // another disk picked above the pane: the volumes left
      for (let s = p.nested; s; s = s.parent.parent ? s.parent : null) [...p.src.options].find((o) => o.value === s.id)?.remove();
      p.nested = null;
    }
    if (p.source?.parent && !this.syncVolume(p.source)) { this.leaveVolume(p); return; }
    if (p.source) {
      const api = this.deps.api;
      const text = api.diskDir(p.source.path, p.source.side, p.source.linear ? 1 : 0);
      if (!text) {
        const src = p.source;                       // pinned: the panes may be redrawn meanwhile
        this.draw(p);
        this.initDialog(src, null)
          .then(async (done) => {
            if (done) {
              p.src.value = src.id;
            } else if (prev !== undefined && prev !== p.src.value) {
              p.src.value = prev;
            }
            this.load(p);
          })
          .catch((err) => this.deps.say("error: " + (err?.message ?? err)));
        return;
      } else {
        const dir = JSON.parse(text);
        p.files = dir.files;
        p.free = dir.free;
        p.volume = { volumeId: homeField(dir.volumeId), owner: homeField(dir.owner), segments: dir.segments };
        if (p.source.parent) p.files.unshift({ up: true, empty: true, name: "..", blocks: "" });
      }
    }
    p.selected = p.files.length ? Math.min(Math.max(p.selected, 0), p.files.length - 1) : -1;
    this.draw(p);
  }

  refresh() { for (const p of this.panes) this.load(p); }

  draw(p) {
    p.list.replaceChildren();
    for (const [i, f] of p.files.entries()) {
      const row = el("div", "fm-row" + (i === p.selected ? " selected" : "") + (f.up ? " up" : f.empty ? " unused" : "") + (p.marks.has(f.name) ? " marked" : ""));
      row.dataset.i = i;
      row.append(el("span", "n", f.up ? ".." : f.empty ? "< UNUSED >" + (f.was ? "  " + f.was : "") : f.name), el("span", "b", String(f.blocks)),
                 el("span", "d", f.date || ""), el("span", "p", f.protected ? "P" : ""));
      p.list.appendChild(row);
    }
    const files = p.files.filter((f) => !f.empty);
    for (const name of [...p.marks]) if (!files.some((f) => f.name === name)) p.marks.delete(name);
    const marked = files.filter((f) => p.marks.has(f.name));
    p.head.textContent = !p.volume ? "" : `${p.volume.volumeId || "(no volume id)"}${p.volume.owner ? "  ·  " + p.volume.owner : ""}  ·  ${p.volume.segments} directory segment${p.volume.segments === 1 ? "" : "s"}`;
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

  // Gray + / -: the files matching a pattern of the OS (* any string, % one
  // character, an omitted part = *) marked on top of the marks there, or
  // unmarked.
  async markPattern(p, on) {
    if (!p.source) return;
    const pattern = await this.ask(`${on ? "Select" : "Unselect"} the files matching (* any string, % one character; several patterns with commas):`,
                                   { title: on ? "Select" : "Unselect", input: "*.*" });
    if (!pattern) return;
    const test = patternsMatcher(pattern);
    let n = 0;
    for (const f of p.files) {
      if (f.empty || !test(f.name)) continue;
      ++n;
      if (on) p.marks.add(f.name); else p.marks.delete(f.name);
    }
    this.draw(p);
    this.deps.say(`${n} file${n === 1 ? "" : "s"} match ${pattern.trim().toUpperCase()}`);
  }

  // Gray *: every file's mark flipped.
  invertMarks(p) {
    for (const f of p.files) {
      if (f.empty) continue;
      if (p.marks.has(f.name)) p.marks.delete(f.name); else p.marks.add(f.name);
    }
    this.draw(p);
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
    if (file.up) throw new Error("that is the way out of the volume, not a file");
    if (file.empty) throw new Error("that is an unused area, not a file");
    return { pane: p, source: p.source, file };
  }

  // ── the keys ─────────────────────────────────────────────────────────────
  key(e) {
    if (this.dialog) { this.dialogKey(e); return; }
    if (e.key === "Alt") { e.preventDefault(); this.altBar(true); return; }
    if (this.v) { this.viewerKey(e); return; }
    if (e.altKey) { this.altKey(e); return; }
    const p = this.panes[this.active];
    const acts = { F1: () => this.upload(), F2: () => this.download(), F3: () => this.view(), F4: () => this.edit(),
                   F5: () => this.copy(), F6: () => this.renmove(), F7: () => this.init(), F8: () => this.remove(),
                   F9: () => this.squeeze(), F10: () => this.close(), Enter: () => this.enter(), Escape: () => this.close(),
                   Backspace: () => this.leaveVolume(p),
                   Tab: () => { this.active ^= 1; this.focusList(); },
                   Insert: () => this.mark(p, 1),
                   "+": () => this.markPattern(p, true), "-": () => this.markPattern(p, false), "*": () => this.invertMarks(p),
                   ArrowUp: () => e.shiftKey ? this.mark(p, -1) : this.select(p, Math.max(0, p.selected - 1)),
                   ArrowDown: () => e.shiftKey ? this.mark(p, 1) : this.select(p, Math.min(p.files.length - 1, p.selected + 1)),
                   Home: () => this.select(p, 0), End: () => this.select(p, p.files.length - 1) };
    const a = acts[e.key];
    if (!a) return;
    e.preventDefault(); e.stopPropagation();
    this.run(a);
  }

  // The keys with Alt held.
  altKey(e) {
    const acts = { F1: () => this.changeDisk(0), F2: () => this.changeDisk(1),
                   F5: () => this.makeVolume(), F6: () => this.protect(), F7: () => this.find(), F8: () => this.recover(),
                   F10: () => this.makeSystem() };
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

  // The blocks of a directory entry - an unused area's.
  areaBytes(source, ordinal) {
    const api = this.deps.api, M = this.deps.module();
    const n = api.diskArea(source.path, source.side, source.linear ? 1 : 0, ordinal);
    if (n < 0) throw new Error(api.diskError());
    return M.HEAPU8.slice(api.diskData(), api.diskData() + n);
  }

  // A file written (the buffer freed only after the write, which may wait
  // on a mount).  A volume is a file: when the file does not fit, the
  // volume grows by what the file needs and the put is tried again.
  async putBytes(source, name, bytes, file) {
    const api = this.deps.api, M = this.deps.module();
    const ptr = M._malloc(bytes.length || 1);
    M.HEAPU8.set(bytes, ptr);
    const [y, m, d] = file?.date ? file.date.split("-").map(Number) : [0, 0, 0];
    try {
      return await this.writable(source, () => {
        const put = () => api.diskPut(source.path, source.side, source.linear ? 1 : 0, name, ptr, bytes.length, y, m, d, file?.protected ? 1 : 0);
        if (put()) return 1;
        if (!source.parent || !api.diskGrow(source.path, Math.ceil(bytes.length / 512) || 1)) return 0;
        return put();
      });
    } finally {
      M._free(ptr);
    }
  }

  // The marked files (or the current one) to a disk, as copies or moved;
  // one file may get another name.  Confirms, and asks about the names it
  // would replace.
  // `named`: the target came from a DEV:NAME dialog, which is the word
  // already - the question is asked then only when a file gets replaced.
  async transfer(list, to, newName, move, named = false) {
    const from = list[0].pane;
    const verb = move ? "Move" : "Copy";
    const nameOn = (c) => newName ?? c.file.name;
    const there = this.namesOn(to);
    const taken = list.filter((c) => there.includes(nameOn(c))).map(nameOn);
    const what = list.length === 1 ? `${list[0].file.name}${newName && newName !== list[0].file.name ? " as " + newName : ""}` : `${list.length} files`;
    const question = `${verb} ${what} to ${to.dev} ${to.name}?` + (taken.length ? `  ${taken.join(", ")} there will be replaced.` : "");
    if ((taken.length || !named) && !await this.ask(question, { title: verb })) return;
    const todo = move ? await this.guarded(list, verb) : list;
    if (!todo) return;
    let n = 0;
    for (const c of todo) {
      const bytes = this.bytesOf(c.source, c.file.name);
      if (!await this.putBytes(to, nameOn(c), bytes, c.file)) throw new Error(`${c.file.name}: ${this.deps.api.diskError()}`);
      if (move && !await this.writable(c.source, () => this.deps.api.diskRm(c.source.path, c.source.side, c.source.linear ? 1 : 0, c.file.name)))
        throw new Error(`${c.file.name}: ${this.deps.api.diskError()}`);
      from.marks.delete(c.file.name);
      ++n;
    }
    this.refresh();
    this.deps.say(`${n === 1 ? what : n + " files"} ${move ? "moved" : "copied"} to ${to.dev}`);
  }

  // F5: the marked files to the other pane's disk; one file - a "DEV:NAME"
  // (the other pane's disk offered), the same disk under another name too.
  async copy() {
    const list = this.targets();
    if (!list.length) return;
    const from = list[0].pane, other = this.panes[this.active ^ 1];
    if (list.length > 1) {
      if (!other.source) throw new Error("the other pane has no disk");
      if (other.source.id === from.source.id) throw new Error("the other pane shows the same disk");
      await this.transfer(list, other.source, null, false);
      return;
    }
    const c = list[0];
    const offered = other.source && other.source.id !== from.source.id ? other.source : from.source;
    const answer = await this.ask(`Copy ${c.file.name} to (DEV:NAME):`, { title: "Copy", input: `${offered.dev}${c.file.name}` });
    if (answer === null || answer === "") return;
    const { source: to, name } = this.parseTarget(answer, from.source);
    if (to.id === from.source.id && name === c.file.name) throw new Error("a file cannot be copied onto itself");
    await this.transfer(list, to, name, false, true);
  }

  // F6 RenMove: one file - a "DEV:NAME": the same (or no) disk is a rename,
  // another disk a move; the marked files - moved to the other pane's disk.
  async renmove() {
    const list = this.targets();
    if (!list.length) return;
    const from = list[0].pane, other = this.panes[this.active ^ 1];
    if (list.length > 1) {
      if (!other.source) throw new Error("the other pane has no disk");
      if (other.source.id === from.source.id) throw new Error("the other pane shows the same disk");
      await this.transfer(list, other.source, null, true);
      return;
    }
    const c = list[0];
    const offered = other.source && other.source.id !== from.source.id ? other.source : from.source;
    const answer = await this.ask(`Rename or move ${c.file.name} to (DEV:NAME):`, { title: "RenMove", input: `${offered.dev}${c.file.name}` });
    if (answer === null || answer === "") return;
    const { source: to, name } = this.parseTarget(answer, from.source);
    if (to.id === from.source.id) {
      if (name === c.file.name) return;
      const ok = await this.writable(c.source, () => this.deps.api.diskRename(c.source.path, c.source.side, c.source.linear ? 1 : 0, c.file.name, name));
      if (!ok) throw new Error(this.deps.api.diskError());
      this.refresh();
      return;
    }
    await this.transfer(list, to, name, true, true);
  }

  // F8: the marked files (or the current one) deleted after a word; each
  // protected one asked about, the answers taken before anything goes.
  async remove() {
    const all = this.targets();
    if (!all.length) return;
    const src = all[0].source, names = all.map((c) => c.file.name);
    if (!await this.ask(`Delete from ${src.dev} ${names.length === 1 ? names[0] : names.length + " files (" + names.join(", ") + ")"}?`, { title: "Delete" })) return;
    const list = await this.guarded(all, "Delete");
    if (!list?.length) return;
    const ok = await this.writable(src, () => list.every((c) => this.deps.api.diskRm(src.path, src.side, src.linear ? 1 : 0, c.file.name)));
    if (!ok) throw new Error(this.deps.api.diskError());
    all[0].pane.marks.clear();
    this.refresh();
    this.deps.say(`${list.length === 1 ? list[0].file.name : list.length + " files"} deleted from ${src.dev}`);
  }

  // Alt+F5: the marked files (or the current one) gathered into a logical
  // disk - a file the system's LD handler mounts as a volume (MOUNT LD0:
  // DZn:NAME) - written to a disk, the other pane's offered.  Linear, one
  // directory segment per 72 files, the name's stem for the volume id.
  async makeVolume() {
    const list = this.targets();
    if (!list.length) return;
    const from = list[0].pane, other = this.panes[this.active ^ 1];
    const offered = other.source && other.source.id !== from.source.id ? other.source : from.source;
    const segments = Math.max(1, Math.ceil(list.length / 72));
    const blocks = 6 + 2 * segments + list.reduce((a, c) => a + c.file.blocks, 0);
    const files = list.length === 1 ? list[0].file.name : `${list.length} files`;
    const answer = await this.ask(`A logical disk of ${files} (${blocks} blocks) written as (DEV:NAME):`, { title: "Volume", input: `${offered.dev}VOLUME.DSK` });
    if (answer === null || answer === "") return;
    const { source: to, name } = this.parseTarget(answer, from.source);
    if (to.id === from.source.id && list.some((c) => c.file.name === name)) throw new Error(`${name} is one of the files going in`);
    const replaced = this.namesOn(to).includes(name) ? `  ${name} there will be replaced.` : "";
    if (replaced && !await this.ask(`Write ${name} (${blocks} blocks) to ${to.dev} ${to.name}?${replaced}`, { title: "Volume" })) return;
    const api = this.deps.api, M = this.deps.module();
    if (!api.ldCreate(blocks, segments, name.split(".")[0])) throw new Error(api.diskError());
    for (const c of list) {
      const bytes = this.bytesOf(c.source, c.file.name);
      const ptr = M._malloc(bytes.length || 1);
      M.HEAPU8.set(bytes, ptr);
      const [y, m, d] = c.file.date ? c.file.date.split("-").map(Number) : [0, 0, 0];
      const ok = api.ldPut(c.file.name, ptr, bytes.length, y, m, d, c.file.protected ? 1 : 0);
      M._free(ptr);
      if (!ok) throw new Error(`${c.file.name}: ${api.diskError()}`);
    }
    const image = M.HEAPU8.slice(api.ldData(), api.ldData() + api.ldSize());
    if (!await this.putBytes(to, name, image)) throw new Error(`${name}: ${api.diskError()}`);
    from.marks.clear();
    this.refresh();
    this.deps.say(`${name} written to ${to.dev} - a volume of ${files}; MOUNT LD0: ${to.dev}${name} in the system`);
  }

  // Alt+F10: this pane's floppy made a system volume of the other pane's:
  // the kit (every .SYS, PIP, DUP, DIR, RESORC) copied over, then the
  // bootstrap written the way COPY/BOOT does it (the library's writeBoot,
  // byte for byte the OS's) - no running system needed.  The other pane's
  // disk must boot itself; the target a floppy with a directory.
  async makeSystem() {
    const p = this.panes[this.active], other = this.panes[this.active ^ 1];
    const to = p.source, from = other.source;
    if (!to) throw new Error("this pane has no disk");
    if (!from || from.id === to.id) throw new Error("show the system disk on the other pane");
    if (to.linear || to.parent) throw new Error("only a floppy boots the machine: the target must be a floppy");
    if (from.linear || from.parent) throw new Error("the source must be a floppy that boots");
    if (!p.volume) throw new Error(`${to.dev} holds no RT-11 directory: INIT it first (F7)`);
    const api = this.deps.api;
    const monitor = api.diskBooted(from.path, from.side);
    if (!monitor) throw new Error(`${from.dev} ${from.name} is not a system volume: no bootstrap on it`);
    const kit = this.dirOf(from).filter((f) => /\.SYS$/.test(f.name) || ["PIP.SAV", "DUP.SAV", "DIR.SAV", "RESORC.SAV"].includes(f.name));
    const blocks = kit.reduce((a, f) => a + f.blocks, 0);
    const replaced = this.namesOn(to).filter((n) => kit.some((f) => f.name === n));
    const question = `Make ${to.dev} ${to.name} a system volume of ${from.dev} ${from.name} - ${monitor}, ${kit.length} files, ${blocks} blocks?`
                   + (replaced.length ? `  ${replaced.join(", ")} there will be replaced.` : "");
    if (!await this.ask(question, { title: "System" })) return;
    const ok = await this.writable(to, () => api.diskSystem(to.path, to.side, from.path, from.side));
    if (!ok) throw new Error(api.diskError());
    const copied = api.diskText();                  // read before the panes reload (the module's text is one)
    this.refresh();
    this.deps.say(`${to.dev} boots ${monitor} now (${copied}) - mount it in A: and Boot`);
  }

  // Alt+F6: the marked files (or the current one) protected - or, when
  // every one of them is, unprotected - after a word.
  async protect() {
    const list = this.targets();
    if (!list.length) return;
    const on = !list.every((c) => c.file.protected);
    const src = list[0].source, what = list.length === 1 ? list[0].file.name : `${list.length} files`;
    const verb = on ? "Protect" : "Unprotect";
    if (!await this.ask(`${verb} ${what} on ${src.dev}?`, { title: verb })) return;
    const ok = await this.writable(src, () => list.every((c) => this.deps.api.diskProtect(src.path, src.side, src.linear ? 1 : 0, c.file.name, on ? 1 : 0)));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
    this.deps.say(`${what} ${on ? "protected" : "unprotected"} on ${src.dev}`);
  }

  // Alt+F7: a pattern looked for on every mounted disk; the hit picked
  // from the list is shown in the active pane.
  async find() {
    const pattern = await this.ask("Find the files matching (on every mounted disk; * any string, % one character):", { title: "Find", input: "*.*" });
    if (!pattern) return;
    const test = patternsMatcher(pattern), hits = [];
    for (const s of this.deps.sources()) {
      const text = this.deps.api.diskDir(s.path, s.side, s.linear ? 1 : 0);
      if (!text) continue;
      for (const f of JSON.parse(text).files) if (!f.empty && test(f.name)) hits.push({ source: s, file: f });
    }
    const shown = pattern.trim().toUpperCase();
    if (!hits.length) { this.deps.say(`nothing matches ${shown}`); return; }
    const r = await this.showDialog(`${hits.length} file${hits.length === 1 ? "" : "s"} match ${shown}:`, {
      title: "Find", buttons: [["Go to", true], ["Cancel", null]],
      list: hits.map((h) => `${h.source.dev}${h.file.name.padEnd(11)}${String(h.file.blocks).padStart(4)}  ${h.file.date || ""}`) });
    if (r.value === null) return;
    const h = hits[r.index], p = this.panes[this.active];
    p.src.value = h.source.id; p.prev = h.source.id;
    this.load(p);
    this.select(p, Math.max(0, p.files.findIndex((f) => f.name === h.file.name)));
    this.focusList();
  }

  // Alt+F8 on an unused area: the file it was brought back - the OS's
  // DELETE leaves the name, the length and the date in the entry, and the
  // data lies untouched until something is put over the area - or, on an
  // area that was no file, the area made a file of a name given.
  async recover() {
    const p = this.panes[this.active], f = p.files[p.selected];
    if (!p.source || !f) return;
    if (!f.empty || f.up) throw new Error("stand on an unused area to undelete or recover it");
    const question = f.was ? `Undelete ${f.was} (${f.blocks} blocks) on ${p.source.dev} as:`
                           : `Recover the unused area (${f.blocks} blocks) on ${p.source.dev} as a file named:`;
    const answer = await this.ask(question, { title: f.was ? "Undelete" : "Recover", input: this.freeName(p, f.was ?? "AREA.DAT") });
    if (answer === null || answer === "") return;
    const name = answer.trim().toUpperCase();
    if (!LATIN_NAME.test(name)) throw new Error(`${name}: not an RT-11 name (6.3, A-Z 0-9 $)`);
    const src = p.source;
    const ok = await this.writable(src, () => this.deps.api.diskUndelete(src.path, src.side, src.linear ? 1 : 0, f.i, name === f.was ? "" : name));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
    this.select(p, Math.max(0, p.files.findIndex((x) => x.name === name)));
    this.deps.say(f.was ? `${f.was} undeleted on ${src.dev}${name === f.was ? "" : " as " + name}` : `${f.blocks} blocks recovered on ${src.dev} as ${name}`);
  }

  // The name itself when no file of it is on the pane, else NAME1, NAME2 ...
  // (the stem cut to leave room for the digits).
  freeName(p, name) {
    const taken = new Set(p.files.filter((x) => !x.empty).map((x) => x.name));
    if (!taken.has(name)) return name;
    const dot = name.indexOf("."), stem = dot < 0 ? name : name.slice(0, dot), ext = dot < 0 ? "" : name.slice(dot);
    for (let n = 1; n < 100; ++n) {
      const c = stem.slice(0, 6 - String(n).length) + n + ext;
      if (!taken.has(c)) return c;
    }
    return name;
  }

  // F7: the pane's volume initialised - every file on it lost.
  async init() {
    const p = this.panes[this.active];
    if (!p.source) throw new Error("this pane has no disk");
    const files = p.files.filter((f) => !f.empty).length;
    if (await this.initDialog(p.source, p.volume ? { ...p.volume, files } : null)) this.refresh();
  }

  // The INIT dialog, the OS's INITIALIZE: the volume id and the owner (12
  // characters each), the directory segments (1..31, 72 files a segment)
  // and, on a volume that has a directory, "the volume id only" - the home
  // block rewritten, the files kept (INITIALIZE/VOLUMEID:ONLY).  `has`:
  // what the volume is now, null for a blank.  True when done.
  async initDialog(src, has) {
    const blocks = src.linear ? Math.floor(this.deps.module().FS.stat(src.path).size / 512) : 0;
    const text = has ? `INIT ${src.label}${has.files ? ` - its ${has.files} file(s) will be lost` : ""}:` : `${src.label} has no RT-11 directory.  INIT it:`;
    const r = await this.showDialog(text, {
      title: "Init", buttons: [["OK", true], ["Cancel", null]],
      fields: [["Volume ID", has ? has.volumeId : "RT11A", 12], ["Owner", has ? has.owner : "", 12], ["Segments (1..31)", String(has ? has.segments : defaultSegments(src.linear, blocks)), 2]],
      check: has ? "the volume id only - the directory and the files kept" : null });
    if (r.value === null) return false;
    const [volumeId, owner, segs] = r.values.map((s) => s.trim());
    const segments = Number(segs);
    if (!(Number.isInteger(segments) && segments >= 1 && segments <= 31)) throw new Error(`${segs}: the segments are 1..31`);
    const id = homeBytes(volumeId), own = homeBytes(owner);
    if (id.length > 12 || own.length > 12) throw new Error("the volume id and the owner are 12 characters at most");
    const api = this.deps.api, M = this.deps.module();
    const buf = M._malloc(id.length + own.length + 2);
    M.HEAPU8.set(id, buf); M.HEAPU8.set(own, buf + id.length + 1);
    let ok;
    try {
      ok = await this.writable(src, () => r.checked ? api.diskVolumeId(src.path, src.side, src.linear ? 1 : 0, buf, id.length, buf + id.length + 1, own.length)
                                                   : api.diskInit(src.path, src.side, src.linear ? 1 : 0, buf, id.length, buf + id.length + 1, own.length, segments));
    } finally {
      M._free(buf);
    }
    if (!ok) throw new Error(api.diskError());
    this.deps.say(r.checked ? `${src.dev} volume id: ${volumeId || "(blank)"}${owner ? " / " + owner : ""}`
                            : `${src.dev} initialised: ${volumeId || "(blank)"}, ${segments} directory segment(s)`);
    return true;
  }

  // F9: the files packed to the front, the free blocks in one area at the end.
  async squeeze() {
    const p = this.panes[this.active];
    if (!p.source) throw new Error("this pane has no disk");
    if (!await this.ask(`Squeeze ${p.source.label}?`, { title: "Squeeze" })) return;
    const src = p.source;
    const ok = await this.writable(src, () => this.deps.api.diskSqueeze(src.path, src.side, src.linear ? 1 : 0));
    if (!ok) throw new Error(this.deps.api.diskError());
    this.refresh();
  }

  // F2: one file as it is, after a word; the marked files together in a
  // .zip whose name the dialog asks for.
  async download() {
    const list = this.targets();
    if (!list.length) return;
    let name, bytes;
    if (list.length === 1) {
      const c = list[0];
      if (!await this.ask(`Download ${c.file.name} (${c.file.blocks} blocks) to your computer?`, { title: "Download" })) return;
      name = c.file.name;
      bytes = this.bytesOf(c.source, c.file.name);
    } else {
      const src = list[0].source;
      const zipName = await this.ask(`Download ${list.length} files from ${src.dev} as a .zip named:`, { title: "Download", input: src.name.replace(/\.[^.]*$/, "") + ".zip" });
      if (!zipName) return;
      name = /\.zip$/i.test(zipName) ? zipName : zipName + ".zip";
      bytes = makeZip(list.map((c) => ({ name: c.file.name, bytes: this.bytesOf(c.source, c.file.name) })));
    }
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([bytes]));
    a.download = name; a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    this.deps.say(`${name} downloaded (${bytes.length} bytes)`);
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
      if (!await this.ask(`Upload ${f.name} as ${name} to ${p.source.label}?` + (p.files.some((x) => x.name === name) ? `  ${name} there will be replaced.` : ""), { title: "Upload" })) return;
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
    const p = this.panes[this.active], f = p.files[p.selected];
    if (f?.empty && !f.up) {              // an unused area: its bytes viewed, not edited
      if (mode === "edit") throw new Error("an unused area is viewed (F3), not edited");
      const bytes = this.areaBytes(p.source, f.i);
      this.v = { mode, repr: "oct", enc: "ascii", wrap: true, insert: false, source: p.source,
                 file: { name: `< UNUSED >${f.was ? "  " + f.was : ""}`, blocks: f.blocks }, bytes, textarea: null, editor: null, query: "", hit: null };
      this.viewer.hidden = false;
      this.showRepr();
      this.vtext.scrollTop = 0;
      return;
    }
    const c = this.current();
    if (!c) return;
    if (mode === "edit" && c.file.protected) throw new Error(`${c.file.name} is protected`);
    const bytes = this.bytesOf(c.source, c.file.name);
    const text = looksLikeText(bytes);
    this.v = { mode, repr: text ? "text" : "oct", enc: text ? guessEncoding(bytes) : "ascii", wrap: true, insert: false,
               source: c.source, file: c.file, bytes, textarea: null, editor: null, query: "", hit: null };
    this.viewer.hidden = false;
    this.showRepr();
    this.vtext.scrollTop = 0;
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
      this.renderView(bytes);
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
      const { text, breaks } = v.wrap ? wrapColumns(this.asText(bytes), 80) : { text: this.asText(bytes), breaks: [] };
      if (v.hit) {
        const at = v.hit.at + breaks.filter((b) => b <= v.hit.at).length;           // the hit, moved past the breaks put before it
        const end = v.hit.at + v.hit.len + breaks.filter((b) => b < v.hit.at + v.hit.len).length;
        this.vtext.innerHTML = esc(text.slice(0, at)) + "<mark>" + esc(text.slice(at, end)) + "</mark>" + esc(text.slice(end));
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
      [reprLabel, "text, octal, hex in turn", () => this.cycleRepr()],
      view ? (v.repr === "text" ? [v.wrap ? "Unwrap" : "Wrap", "long lines at the machine's 80 columns", () => this.toggleWrap()] : [null])
           : ["Save", "the file written back", () => this.save()],
      view ? ["Quit", "back to the files (Esc too)", () => this.leaveViewer()] : [null],
      [encLabel, "the encoding: ASCII, KOI-8R, KOI-7, KOI-7 with the РУС / ЛАТ shifts, CP866 in turn", () => this.cycleEncoding()],
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
    const acts = { F1: () => this.cycleRepr(),
                   F2: () => view ? (v.repr === "text" && this.toggleWrap()) : this.save(),
                   F3: () => view && this.leaveViewer(), F4: () => this.cycleEncoding(), F5: () => this.goto(), F7: () => this.search(),
                   F8: () => !view && v.repr !== "text" && this.toggleInsert(),
                   F10: () => this.leaveViewer(), Escape: () => this.leaveViewer() };
    const a = acts[e.key];
    if (!a) return;
    e.preventDefault(); e.stopPropagation();
    this.run(a);
  }

  // Another encoding or representation: the content is kept as bytes first.
  // The content shown anew after a change of the encoding or the
  // representation - at the same place: the viewer's scroll kept as a
  // fraction of the way down (exact when the lines stay the same, near
  // enough between a text and its dump); the editor's cursor kept as the
  // byte it stands on - a text's caret counted through the encoding.
  rekey(change) {
    const v = this.v;
    v.bytes = this.currentBytes();
    const was = this.place();
    change();
    v.hit = null;
    this.showRepr();
    this.placeAt(was);
  }

  place() {
    const v = this.v;
    if (v.textarea) {
      const ta = v.textarea;
      return { at: encodeText(ta.value.slice(0, ta.selectionStart).replace(/\n/g, "\r\n"), v.enc).length, column: "num", frac: this.scrolled() };
    }
    if (v.editor) return { at: v.editor.pos, column: v.editor.column, frac: 0 };
    return { at: -1, column: "num", frac: this.scrolled() };
  }

  placeAt({ at, column, frac }) {
    const v = this.v;
    if (v.editor) {
      v.editor.pos = Math.max(0, Math.min(v.editor.bytes.length - (v.editor.insert ? 0 : 1), at));
      v.editor.column = column;
      v.editor.render();
    } else if (v.textarea && at >= 0) {
      const caret = decodeBytes(v.bytes.subarray(0, Math.min(at, v.bytes.length)), v.enc).replace(/\r\n?/g, "\n").length;
      v.textarea.setSelectionRange(caret, caret);
      this.scrollTo(frac);
    } else {
      this.scrollTo(frac);
    }
  }

  scrolled() {
    const box = this.v.textarea ?? this.vtext;
    const range = box.scrollHeight - box.clientHeight;
    return range > 0 ? box.scrollTop / range : 0;
  }

  scrollTo(frac) {
    const box = this.v.textarea ?? this.vtext;
    box.scrollTop = frac * (box.scrollHeight - box.clientHeight);
  }

  cycleEncoding() {
    const ids = ENCODINGS.map(([id]) => id);
    this.rekey(() => { this.v.enc = ids[(ids.indexOf(this.v.enc) + 1) % ids.length]; });
  }

  // Text, octal, hex in turn - a binary starts at octal, so octal, hex, text.
  cycleRepr() {
    const order = ["text", "oct", "hex"];
    this.rekey(() => { this.v.repr = order[(order.indexOf(this.v.repr) + 1) % order.length]; });
  }

  toggleWrap() {
    const frac = this.scrolled();
    this.v.wrap = !this.v.wrap;
    this.renderView(this.currentBytes());
    this.scrollTo(frac);
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
  async goto() {
    const v = this.v;
    if (v.repr === "text") {
      const answer = await this.ask("Go to line:", { title: "Go to", input: "1" });
      const n = parseInt(answer ?? "", 10);
      if (!(n >= 1)) return;
      const text = v.textarea ? v.textarea.value : this.asText(this.currentBytes());
      let at = 0;
      for (let line = 1; line < n && at >= 0; ++line) at = text.indexOf("\n", at) + 1 || -1;
      if (at < 0) throw new Error(`no line ${n}`);
      const end = text.indexOf("\n", at);
      this.showAt(at, (end < 0 ? text.length : end) - at, text);
    } else {
      const base = v.repr === "oct" ? 8 : 16;
      const answer = await this.ask(`Go to offset (${v.repr === "oct" ? "octal" : "hex"}):`, { title: "Go to", input: "0" });
      const at = parseInt(answer ?? "", base);
      const bytes = this.currentBytes();
      if (!(at >= 0 && at < bytes.length)) return;
      this.showAt(at, 1);
    }
  }

  // F7: a string in the encoding, or - in hex / octal - a byte sequence
  // ("101 102 077"); the next hit after the last one, marked and shown.
  async search() {
    const v = this.v;
    const query = await this.ask(v.repr === "text" ? "Search:" : `Search bytes (${v.repr === "oct" ? "octal" : "hex"}, spaces between):`, { title: "Search", input: v.query });
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

  // F2: the file written back after a word; the editing goes on.
  async save() {
    const v = this.v, bytes = this.currentBytes();
    if (!await this.ask(`Save ${v.file.name} (${bytes.length} bytes) to ${v.source.dev}?`, { title: "Save" })) return false;
    return this.write(bytes);
  }

  // The bytes written to the disk; what is shown counts as unchanged from
  // here on.
  async write(bytes) {
    const v = this.v;
    if (!await this.putBytes(v.source, v.file.name, bytes, v.file)) {
      await this.showDialog(`${v.file.name} cannot be saved on ${v.source.dev}: ${this.deps.api.diskError()}.`, { title: "Save", buttons: [["OK", true]] });
      return false;
    }
    v.bytes = bytes;
    if (v.editor) v.editor.changed = false;
    this.deps.say(`${v.file.name} saved (${bytes.length} bytes)`);
    this.refresh();
    return true;
  }

  changed() {
    const v = this.v;
    if (v.mode !== "edit") return false;
    if (v.editor) return v.editor.changed;
    return v.textarea ? v.textarea.value !== this.asText(v.bytes) : false;
  }

  async leaveViewer() {
    if (this.v && this.changed()) {                 // Yes saves and leaves, No leaves, Cancel stays
      const r = await this.showDialog(`Save the changes to ${this.v.file.name}?`, { title: "Edit", buttons: [["Yes", true], ["No", false], ["Cancel", null]] });
      if (r.value === null) return;
      if (r.value === true && !await this.write(this.currentBytes())) return;
    }
    this.v = null;
    this.viewer.hidden = true;
    this.focusList();
  }
}

const CANCEL = Symbol("cancel");     // a guard's "stop the whole operation"

// A home block field (12 bytes: the volume id, the owner) as text: the
// blanks and NULs at the end dropped, the terminal's encoding guessed as
// for a text - KOI-8R above 0x7F, KOI-7 with the shifts on a ^N, else the
// letters as they are; the blank pattern (an INIT that wrote no id) or
// other unreadable bytes give nothing.
// The way back: a field typed in the dialog as the OS's terminal would
// store it - KOI-8R when it has anything beyond ASCII.
function homeBytes(text) {
  return encodeText(text, /[^\x00-\x7F]/.test(text) ? "koi8-r" : "ascii");
}

function homeField(bytes) {
  let end = bytes.length;
  while (end > 0 && (bytes[end - 1] === 0x20 || bytes[end - 1] === 0)) --end;
  const b = Uint8Array.from(bytes.slice(0, end));
  if (!b.length) return "";
  if (b.every((v) => v === 0xB6 || v === 0x6D)) return "";                 // the blank pattern
  if (b.some((v) => v < 0x20 && v !== 0x0E && v !== 0x0F)) return "";      // not a text
  const enc = b.some((v) => v >= 0x80) ? "koi8-r" : b.some((v) => v === 0x0E || v === 0x0F) ? "koi7s" : "ascii";
  return decodeBytes(b, enc).replace(/[\x00-\x1F]/g, "");
}

// The directory segments INIT proposes: 4 for a floppy, as the OS's; for a
// linear image by its size, as the OS does for its disks - 16 from 4096
// blocks, 31 from 16384.
function defaultSegments(linear, blocks) {
  if (!linear) return 4;
  return blocks >= 16384 ? 31 : blocks >= 4096 ? 16 : 4;
}

// The text with every line longer than `cols` broken into pieces of that
// many characters, as the machine's terminal would show it; `breaks` are
// the offsets in the original text where a line break was put in (for
// moving a mark along).
export function wrapColumns(text, cols) {
  const breaks = [], out = [];
  let pos = 0;
  for (const line of text.split("\n")) {
    for (let i = 0; i < line.length || i === 0; i += cols) {
      if (i) breaks.push(pos + i);
      out.push(line.slice(i, i + cols));
    }
    pos += line.length + 1;
  }
  return { text: out.join("\n"), breaks };
}

// A file pattern as the OS reads one (section 2.5 of its manual): the name
// and the type matched apart, * for any string of either, % for one
// character, a part left out is *; a DEV: in front ignored.  Returns the
// test of a NAME.EXT.
export function patternMatcher(pattern) {
  const spec = pattern.trim().toUpperCase().replace(/^[A-Z]+\d*:/, "");
  const dot = spec.indexOf(".");
  const parts = dot < 0 ? [spec || "*", "*"] : [spec.slice(0, dot) || "*", spec.slice(dot + 1) || "*"];
  const rx = (part) => new RegExp("^" + part.replace(/[$]/g, "[$]").replace(/[*]/g, ".*").replace(/%/g, ".") + "$");
  const [stem, ext] = parts.map(rx);
  return (name) => {
    const d = name.indexOf(".");
    return stem.test(d < 0 ? name : name.slice(0, d)) && ext.test(d < 0 ? "" : name.slice(d + 1));
  };
}

// Several patterns, commas or blanks between: a name matching any of them.
export function patternsMatcher(patterns) {
  const tests = patterns.split(/[\s,]+/).filter(Boolean).map(patternMatcher);
  return (name) => tests.some((t) => t(name));
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
