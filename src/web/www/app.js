// app.js — the MS-0515 in the browser.
//
// The module (ms0515.js / .wasm, built from src/web) runs the machine; this
// file is the front-end: it fetches the ROM and the disk images into the
// module's in-memory file system, mounts them the way the desktop
// front-ends do (two floppy drives of two sides each, a paravirtual hard
// disk), runs 50 frames a second (or faster and slower, by the speed
// control), paints each frame on the canvas, hands
// its sound to an AudioWorklet, turns key events into MS7004 keys the way
// the SDL front-end does, and keeps the images the guest writes to in
// IndexedDB so the next visit finds them - nothing is ever written back to
// the host (it is a static site), the originals are one click away.
// "@STAMP@" is the build time (stamp.cmake fills it in dist/): a browser
// never pairs a cached module with a newer page.
import createMs0515 from "./ms0515.js?v=@STAMP@";
import { KEYS, KEY_ID, mapKey, isLetterKey, charToHostKey, shiftedFunctionKey } from "./keys.js?v=@STAMP@";
import { Joystick } from "./joystick.js?v=@STAMP@";
import { SoftKeyboard, isTouchDevice } from "./softkeys.js?v=@STAMP@";
import { Commander, rt11Name } from "./fm.js?v=@STAMP@";
import { encodeText } from "./edit.js?v=@STAMP@";
import * as bugreport from "./bugreport.js?v=@STAMP@";
import { DiskComposer } from "./wizard.js?v=@STAMP@";
import { parseStart, DEFAULT_SOUND } from "./start.js?v=@STAMP@";

// The floppy images offered: the software collection's released disks, as
// its index.json lists them - title, media (a two-sided image takes both
// sides of its drive) and what to do first after a boot.  They are fetched
// from the collection's Pages, which sit beside this site; `disks=URL`
// points elsewhere (a local copy, the CI's fixture).
const DISK_SITE = new URL(new URLSearchParams(location.search).get("disks")
                          ?? "https://vvv104.github.io/ms0515-software/", location.href);
let DISKS = [];                       // { name, sides, title, hint, rom, url }
let SHIPPED = new Map();              // name -> the entry above
let DISK_INDEX = null;                // the collection's index.json: the wizard composes from its files
async function loadDiskList() {
  try {
    const index = DISK_INDEX = JSON.parse(new TextDecoder().decode(await fetchBytes(new URL("index.json", DISK_SITE))));
    DISKS = index.presets.map((p) => ({
      name: p.image.split("/").pop(), sides: p.media === "ss" ? 1 : 2,
      title: p.title, hint: p.hint ?? "", rom: p.rom ?? "", url: new URL(p.image, DISK_SITE).href,
    }));
  } catch (e) {
    say(`no disk list from ${DISK_SITE.href}: ${e?.message ?? e} - Open… takes an image from your computer`);
  }
  SHIPPED = new Map(DISKS.map((d) => [d.name, d]));
}
const sidesLabel = (n) => n === 2 ? "two-sided" : "one-sided";
const ROMS = { a: "rom/ms0515-roma.rom", b: "rom/ms0515-romb.rom" };
// A shipped disk may run on one ROM alone (the collection's index says
// which: the vvv104 ОМЕГА hangs on ROM-A, Rodionov's needs it).  When such
// a disk goes into drive A, the ROM follows it, and the user hears why.
function followRom(name) {
  const need = SHIPPED.get(name)?.rom;
  if (!need || !ROMS[need] || $("rom").value === need) return;
  $("rom").value = need;
  say(`ROM ${need.toUpperCase()}: ${name} runs on it alone`);
}
const SS_SIZE = 409600, DS_SIZE = 2 * SS_SIZE;
const FRAME_MS = 20;
// The speed control: the machine's frames per second of ours, 20% to 500%
// like the SDL front-end's slider.  Sound is only right at 100% - the
// worklet plays real time - so it goes quiet at any other speed.
const SPEED_MIN = 20, SPEED_MAX = 500, SPEED_KEY = "ms0515.speed";
// The event ring the snapshot carries (reg A, the dispatcher, the FDC,
// traps, HALTs): nothing is recorded per instruction, so it can stay on.
const HISTORY_EVENTS = 4096;
let speedPct = 100;

// FDC units: unit = side * 2 + drive (FD0 = DZ0 = drive A side 0, FD1 =
// drive B side 0, FD2 / FD3 the drives' side 1) - core/floppy.c's numbering.
const unitOf = (drive, side) => side * 2 + drive;
const driveOf = (unit) => unit & 1;
const sideOf = (unit) => unit >> 1;

const $ = (id) => document.getElementById(id);
const canvas = $("screen");
const ctx = canvas.getContext("2d");
const status = $("status");

let M, h, api;
let image, pcmBuf;
let running = false, lastTick = 0, acc = 0;
let audio = null, speaker = null, audioStats = null;   // the worklet's counters, for __ms()
let booted = false;                 // the machine has been started, by the page or by hand
const RETURN = String.fromCharCode(13);
const WAIT_FOR_SOUNDS = 8000;       // ms the boot gives the drive's recordings
let frames = 0, speakerTransitions = 0;   // the speaker's level changes, summed over the frames
let halted = false;                        // the CPU stopped on a HALT: the bug-report button says so
let joystick = null;                       // the MS7007-port joystick (joystick.js)
let softkbd = null;                        // the OS's on-screen keyboard (softkeys.js)
let commander = null;                      // the files of the mounted images (fm.js)
const QUERY = new URLSearchParams(location.search);
const EMBED = QUERY.get("embed") === "1";   // the screen alone: the page in somebody's frame
let startFile = null;                      // ?start=URL: the moment the page opens at (start.js)
                                           // ?run=URL: a program run from its card - the same visit, with `run` set
const RUN_DIR = "/run";                    // the program's folder in the module's file system
// The visitor's languages, the first first: what a card's texts are picked by.
const LANGS = [QUERY.get("lang"), ...(navigator.languages ?? []), navigator.language, "en"]
  .filter(Boolean).map((l) => l.toLowerCase().split("-")[0]);
const inLanguage = (texts) => typeof texts === "string" ? texts
  : LANGS.map((l) => texts?.[l]).find(Boolean) ?? Object.values(texts ?? {})[0] ?? "";
const K = (name) => KEY_ID[name];

function say(s) { status.textContent = s; }
const fail = (e) => { say("error: " + (e?.message ?? e)); document.body.classList.add("failed"); };
const hint = (s) => say("hint: " + s);

async function fetchBytes(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

// ── persistence ────────────────────────────────────────────────────────────
// IndexedDB holds image bytes by name: the user's own images, and the
// shipped images the guest has written to (a copy; "Revert" drops it).
// A second store holds the saved state - the machine's memory and its CPU
// as "Save state" left them - so Restore reaches it on a later visit too
// (the module's own file system is the tab's memory and goes with it).
// localStorage holds the small things: the user's image list (name ->
// size) and the mounts.
const DB = "ms0515", STORE = "disks", STATE_STORE = "state", STATE_KEY = "last";
function db() {
  return new Promise((ok, no) => {
    const r = indexedDB.open(DB, 2);
    r.onupgradeneeded = () => {
      const d = r.result;                       // version 1 had `disks` alone
      if (!d.objectStoreNames.contains(STORE)) d.createObjectStore(STORE);
      if (!d.objectStoreNames.contains(STATE_STORE)) d.createObjectStore(STATE_STORE);
    };
    r.onsuccess = () => ok(r.result);
    r.onerror = () => no(r.error);
  });
}
function dbRequest(mode, op, store = STORE) {
  return db().then((d) => new Promise((ok, no) => {
    const r = op(d.transaction(store, mode).objectStore(store));
    r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error);
  }));
}
const dbGet = (key) => dbRequest("readonly", (s) => s.get(key));
const dbPut = (key, value) => dbRequest("readwrite", (s) => s.put(value, key));
const dbDel = (key) => dbRequest("readwrite", (s) => s.delete(key));
const stateGet = () => dbRequest("readonly", (s) => s.get(STATE_KEY), STATE_STORE);
const statePut = (rec) => dbRequest("readwrite", (s) => s.put(rec, STATE_KEY), STATE_STORE);

const own = new Map(Object.entries(JSON.parse(localStorage.getItem("ms0515.images") ?? "{}")));
const saveOwn = () => localStorage.setItem("ms0515.images", JSON.stringify(Object.fromEntries(own)));

// ── the mounts ─────────────────────────────────────────────────────────────
const slots = { fd: ["", "", "", ""], hd: "" };   // image names, "" = empty
const ds = [false, false];                         // the drive holds a double-sided image
const staged = new Map();                          // FS path -> mtime at the last flush
const pathOf = (name) => "/disks/" + name;

function saveMounts() {
  if (startFile) return;                   // a start visit leaves the remembered mounts alone
  localStorage.setItem("ms0515.mounts", JSON.stringify({ rom: $("rom").value, fd: slots.fd, hd: slots.hd }));
}
function loadMounts() {
  const q = new URLSearchParams(location.search);
  let m = { rom: "a", fd: [DISKS[0]?.name ?? "", "", "", ""], hd: "" };
  try { m = { ...m, ...JSON.parse(localStorage.getItem("ms0515.mounts") ?? "{}") }; } catch {}
  if (q.get("rom")) m.rom = q.get("rom");
  if (q.has("disk")) m.fd[0] = q.get("disk");
  if (q.has("disk1")) m.fd[1] = q.get("disk1");
  if (q.has("hd")) m.hd = q.get("hd");
  return m;
}

// The image's bytes: the user's copy or own image from IndexedDB, else the
// shipped original.
async function imageBytes(name) {
  const local = await dbGet(name);
  if (local) return local;
  if (!SHIPPED.has(name)) throw new Error(`${name}: no such image`);
  return fetchBytes(SHIPPED.get(name).url);
}

// Into the module's file system, once (fresh = replace what is there).
async function stage(name, fresh = false) {
  const path = pathOf(name);
  if (fresh || !M.FS.analyzePath(path).exists) {
    M.FS.mkdirTree("/disks");
    M.FS.writeFile(path, await imageBytes(name));
    staged.set(path, M.FS.stat(path).mtime.getTime());
  }
  return path;
}

function unmountFd(unit) {
  const drive = driveOf(unit);
  if (ds[drive]) {
    api.unmount(h, unitOf(drive, 0));
    api.unmount(h, unitOf(drive, 1));
    slots.fd[unitOf(drive, 0)] = "";
    ds[drive] = false;
  } else if (slots.fd[unit]) {
    api.unmount(h, unit);
    slots.fd[unit] = "";
  }
}

function mountedWhere(name) {
  const u = slots.fd.indexOf(name);
  if (u >= 0) return `drive ${"AB"[driveOf(u)]} side ${sideOf(u)}`;
  return slots.hd === name ? "HD" : null;
}

// A 400 KB image is one side; an 800 KB one takes both sides of its drive.
async function mountFd(unit, name) {
  unmountFd(unit);
  if (name) {
    const where = mountedWhere(name);
    if (where) throw new Error(`${name} is already in ${where}`);
    const path = await stage(name);
    const size = M.FS.stat(path).size;
    if (size !== SS_SIZE && size !== DS_SIZE) throw new Error(`${name}: not a 400 / 800 KB floppy image`);
    const drive = driveOf(unit);
    if (size === DS_SIZE) {
      if (sideOf(unit) === 1) throw new Error("a double-sided image goes on side 0");
      unmountFd(unitOf(drive, 1));
      if (!api.mount(h, unitOf(drive, 0), path) || !api.mount(h, unitOf(drive, 1), path))
        throw new Error(`${name}: mount failed`);
      ds[drive] = true;
    } else if (!api.mount(h, unit, path)) {
      throw new Error(`${name}: mount failed`);
    }
    slots.fd[unit] = name;
    if (unit === unitOf(0, 0)) followRom(name);   // the boot disk picks the ROM
  }
  saveMounts();
  renderDevices();
}

async function mountHd(name) {
  if (slots.hd) { api.unmountHd(h); slots.hd = ""; }
  if (name) {
    const where = mountedWhere(name);
    if (where) throw new Error(`${name} is already in ${where}`);
    const path = await stage(name);
    if (!api.mountHd(h, path)) throw new Error(`${name}: not a HD image (a multiple of 512 bytes)`);
    slots.hd = name;
  }
  saveMounts();
  renderDevices();
}

// The module's files are the live images: an image written since the last
// look goes to IndexedDB.
function flushDisks() {
  for (const [path, seen] of staged) {
    if (!M.FS.analyzePath(path).exists) continue;
    const mtime = M.FS.stat(path).mtime.getTime();
    if (mtime === seen) continue;
    staged.set(path, mtime);
    dbPut(path.slice("/disks/".length), M.FS.readFile(path)).catch(() => {});
  }
}

// Drop what was written to a shipped image and mount the original again.
async function revert(name) {
  const unit = slots.fd.indexOf(name);
  const onHd = slots.hd === name;
  if (unit >= 0) unmountFd(unit); else if (onHd) await mountHd("");
  await dbDel(name);
  await stage(name, true);
  if (unit >= 0) await mountFd(unit, name); else if (onHd) await mountHd(name);
  say(`${name}: the original again`);
}

// The user's own image: unmount, drop it everywhere.
async function deleteOwn(name) {
  if (!confirm(`Delete ${name} from this browser?`)) return;
  const unit = slots.fd.indexOf(name);
  if (unit >= 0) unmountFd(unit); else if (slots.hd === name) await mountHd("");
  await dbDel(name);
  own.delete(name); saveOwn();
  const path = pathOf(name);
  if (M.FS.analyzePath(path).exists) M.FS.unlink(path);
  staged.delete(path);
  saveMounts();
  renderDevices();
}

// Everything the page keeps in the browser, then the page anew.
async function wipe() {
  if (!confirm("Drop every image and setting this page keeps in the browser?")) return;
  stop();
  window.removeEventListener("beforeunload", flushDisks);
  localStorage.removeItem("ms0515.images");
  localStorage.removeItem("ms0515.mounts");
  localStorage.removeItem(SPEED_KEY);
  localStorage.removeItem(SOUND_KEY);
  await new Promise((ok, no) => { const r = indexedDB.deleteDatabase(DB); r.onsuccess = ok; r.onerror = () => no(r.error); r.onblocked = ok; });
  location.href = location.pathname;
}

function download(name) {
  const path = pathOf(name);
  if (!M.FS.analyzePath(path).exists) return;
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([M.FS.readFile(path)]));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}

// A new own image from bytes: stored, listed, in the file system.
async function addOwn(name, bytes) {
  if (bytes.length === 0 || bytes.length % 512) throw new Error(`${name}: not a disk image (a multiple of 512 bytes)`);
  if (SHIPPED.has(name)) throw new Error(`${name}: that is a shipped image's name`);
  if (mountedWhere(name)) throw new Error(`${name} is mounted; unmount it first`);
  await dbPut(name, bytes);
  own.set(name, bytes.length); saveOwn();
  M.FS.mkdirTree("/disks");
  M.FS.writeFile(pathOf(name), bytes);
  staged.set(pathOf(name), M.FS.stat(pathOf(name)).mtime.getTime());
  return name;
}

function pickFile(then) {
  const input = $("file");
  input.value = "";
  input.onchange = async () => {
    const f = input.files[0];
    if (!f) return;
    then(await addOwn(f.name, new Uint8Array(await f.arrayBuffer())));
  };
  input.click();
}

// ── the drives' panels ─────────────────────────────────────────────────────
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text) e.textContent = text; return e; };
function button(label, onclick, title) {
  const b = el("button", "small", label);
  b.onclick = () => Promise.resolve().then(onclick).catch(fail);
  if (title) b.title = title;
  return b;
}
const fmtSize = (n) => n % 1048576 === 0 ? `${n / 1048576} MB` : `${Math.round(n / 1024)} KB`;

function select(kind, value, onchange) {
  const s = document.createElement("select");
  const add = (v, label) => { const o = document.createElement("option"); o.value = v; o.textContent = label; s.appendChild(o); };
  add("", "— empty —");
  if (kind === "fd")
    for (const d of DISKS) add(d.name, `${d.title} (${sidesLabel(d.sides)})`);
  if (startFile) for (const name of startFile.disks.keys()) add(name, `${name} (from the start file)`);
  for (const [name, size] of own) {
    const floppy = size === SS_SIZE || size === DS_SIZE;
    if (floppy === (kind === "fd")) add(name, `${name} (${floppy ? sidesLabel(size / SS_SIZE) : fmtSize(size)}, local copy)`);
  }
  s.value = value;
  s.onchange = () => Promise.resolve(onchange(s.value)).catch((e) => { fail(e); renderDevices(); });
  return s;
}

function imageButtons(name) {
  const b = [button("Download", () => download(name), "save the image as it is now")];
  if (SHIPPED.has(name))
    b.push(button("Revert", () => revert(name), "drop your changes: the shipped original again"));
  else
    b.push(button("Delete", () => deleteOwn(name), "remove the image from this browser"));
  return b;
}

function fdRow(unit) {
  const drive = driveOf(unit), side = sideOf(unit);
  const row = el("div", "row");
  row.append(el("span", "side", `side ${side}`));
  if (side === 1 && ds[drive]) {
    row.append(el("span", "shadow", `side 1 of ${slots.fd[unitOf(drive, 0)]}`));
    return row;
  }
  const name = slots.fd[unit];
  row.append(select("fd", name, (v) => mountFd(unit, v)));
  row.append(button("Open…", () => pickFile((n) => mountFd(unit, n).catch(fail)), "a .dsk from your computer"));
  if (DISK_INDEX && unit === unitOf(0, 0))
    row.append(button("Compose…", () => composeDisk(), "a disk of your own choosing from the software collection"));
  if (name) row.append(...imageButtons(name));
  return row;
}

// ── the disk wizard: a system and bundles of the collection, composed in
// the module (wizard.js / src/wizard_web.cpp); the disk lands among the
// user's own images, in drive A, and boots.
let composer = null;
function composeDisk() {
  document.querySelectorAll("details.dev[open]").forEach((d) => d.removeAttribute("open"));
  composer ??= new DiskComposer({
    M, site: DISK_SITE, index: DISK_INDEX, fetchBytes,
    onDisk: async (name, bytes) => {
      for (let unit = 0; unit < 4; ++unit) if (slots.fd[unit] === name) unmountFd(unit);
      if (slots.hd === name) await mountHd("");
      await addOwn(name, bytes);
      await mountFd(unitOf(0, 0), name);
      await boot();
    },
  });
  composer.open().catch(fail);
}

// ── the commander: the files of the mounted images, in place of the screen ─
// Its sources are what the drives hold - each side of a floppy image, the
// HD image; a write goes around the FDC: the image is unmounted, changed
// in the module's file system, mounted again (the guest sees a changed
// disk at its next directory read).
function fileSources() {
  const out = [];
  // An 800 KB image can be one whole-disk DV:/MZ: volume instead of two DZ
  // sides - the content decides (the DV/MZ handlers' formats, told by the
  // home block + directory).
  const whole = {};   // drive -> 2 (DV) / 3 (MZ): one whole-disk volume
  const uninit = {};  // drive -> true: no volume of any kind on the image yet
  for (let drive = 0; drive < 2; ++drive) {
    const name = ds[drive] && slots.fd[unitOf(drive, 0)];
    if (!name) continue;
    try {
      const det = JSON.parse(api.diskDetect(pathOf(name)) || "[]");
      const w = det.find((s) => s.vol >= 2);
      if (w && !det.some((s) => s.vol === 0)) whole[drive] = w.vol;
      else if (!det.length) uninit[drive] = true;
    } catch { /* an unreadable image stays a pair of DZ sides */ }
  }
  for (let unit = 0; unit < 4; ++unit) {
    const drive = driveOf(unit), side = sideOf(unit);
    const name = side === 1 && ds[drive] ? slots.fd[unitOf(drive, 0)] : slots.fd[unit];
    if (!name) continue;
    if (whole[drive] !== undefined) {
      if (side === 1) continue;                 // one volume, not two sides
      const dev = `${whole[drive] === 2 ? "DV" : "MZ"}${drive}:`;
      out.push({ id: `fd${unit}`, dev, label: `${dev} ${name}`, path: pathOf(name), side: 0, vol: whole[drive], ds: true, linear: false, name, unit });
      continue;
    }
    if (uninit[drive]) {
      // Not a volume of any kind yet: one entity, not a pair of DZ sides -
      // the INIT dialog names what it becomes (DZ a side, DV or MZ).
      if (side === 1) continue;
      const dev = `${"AB"[drive]}:`;
      out.push({ id: `fd${unit}`, dev, label: `${dev} ${name} - uninitialised`, path: pathOf(name), side: 0, vol: 0, ds: true, uninit: true, linear: false, name, unit });
      continue;
    }
    // The image's side: a two-sided image has the unit's, a one-sided image
    // has only side 0 whichever unit it sits on.
    out.push({ id: `fd${unit}`, dev: `DZ${unit}:`, label: `DZ${unit}: ${name}`, path: pathOf(name), side: ds[drive] ? side : 0, vol: 0, ds: !!ds[drive], linear: false, name, unit });
  }
  if (slots.hd) out.push({ id: "hd", dev: "HD0:", label: `HD0: ${slots.hd}`, path: pathOf(slots.hd), side: 0, vol: 1, linear: true, name: slots.hd });
  return out;
}

let commanderHold = false;                 // a write in progress: the panes keep still
async function writableImage(source, op) {
  const name = source.name;
  const unit = slots.fd.indexOf(name);
  const onHd = slots.hd === name;
  commanderHold = true;
  try {
    if (unit >= 0) unmountFd(unit); else if (onHd) await mountHd("");
    const ok = op();
    staged.set(pathOf(name), 0);                        // written outside the FDC: flush it
    if (unit >= 0) await mountFd(unit, name); else if (onHd) await mountHd(name);
    return ok;
  } finally {
    commanderHold = false;
  }
}

function toggleCommander() {
  const open = $("fm").hidden;
  $("fm").hidden = !open;
  canvas.hidden = open;
  $("files").textContent = open ? "Files: close" : "Files";
  if (open) commander.open();
}


// A blank floppy for the drive: one-sided into its first empty side,
// two-sided into an empty drive.
async function newFloppy(drive, sides) {
  const size = sides * SS_SIZE;
  let unit = unitOf(drive, 0);
  if (sides === 2) {
    if (slots.fd[unitOf(drive, 0)] || slots.fd[unitOf(drive, 1)]) throw new Error(`empty drive ${"AB"[drive]} first`);
  } else if (slots.fd[unit]) {
    unit = unitOf(drive, 1);
    if (ds[drive] || slots.fd[unit]) throw new Error(`drive ${"AB"[drive]} has no empty side`);
  }
  let name = `blank${sides}s.dsk`;
  for (let i = 2; own.has(name); ++i) name = `blank${sides}s-${i}.dsk`;
  const n = api.diskBlank(sides === 2 ? 1 : 0);          // the formatting pattern, not zeros
  if (n !== size) throw new Error(`a blank of ${n} bytes for ${size}`);
  await addOwn(name, M.HEAPU8.slice(api.diskData(), api.diskData() + n));
  await mountFd(unit, name);
  hint(`${name} is in drive ${"AB"[drive]}: INIT DZ${unit}: in the guest makes it a volume` + (sides === 2 ? ` (side 1 is DZ${unit + 2}:, its own)` : ""));
}

function newFloppyRow(drive) {
  const make = el("div", "row");
  make.append(el("span", "side", "new"));
  const sides = document.createElement("select");
  for (const n of [1, 2]) { const o = document.createElement("option"); o.value = n; o.textContent = sidesLabel(n); sides.appendChild(o); }
  make.append(sides);
  make.append(button("Create blank", () => newFloppy(drive, +sides.value), "an image as the machine's formatting leaves it (the 0xB6 0x6D pattern); the guest initialises it (INIT DZn:)"));
  return make;
}

function hdRows() {
  const row = el("div", "row");
  row.append(el("span", "side", "image"));
  row.append(select("hd", slots.hd, (v) => mountHd(v)));
  row.append(button("Open…", () => pickFile((n) => mountHd(n).catch(fail)), "an image from your computer"));
  if (slots.hd) row.append(...imageButtons(slots.hd));
  const make = el("div", "row");
  make.append(el("span", "side", "new"));
  const mb = document.createElement("input");
  mb.type = "number"; mb.min = 1; mb.max = 64; mb.value = 8;
  make.append(mb, el("span", null, "MB"));
  make.append(button("Create blank", async () => {
    const size = Math.max(1, Math.min(64, +mb.value || 8));
    let name = `hd${size}m.img`;
    for (let i = 2; own.has(name); ++i) name = `hd${size}m-${i}.img`;
    await addOwn(name, new Uint8Array(size * 1048576));
    await mountHd(name);
    hint(`${name} is the HD now: Reset, then INIT HD: in the guest makes it a volume`);
  }, "a zero-filled image; the guest initialises it"));
  const hint = el("div", "hint", "RT-11 installs HD.SYS at boot: mount, then Reset (the development disk has the handler)");
  return [row, make, hint];
}

function renderDevices() {
  for (const drive of [0, 1]) {
    const d = $("dev" + drive);
    const names = [0, 1].map((s) => slots.fd[unitOf(drive, s)]).filter(Boolean);
    d.querySelector(".devname").innerHTML = `<b>${"AB"[drive]}</b> ` + (names.length ? names.join(" · ") + (ds[drive] ? " (two-sided)" : "") : "—");
    d.querySelector(".panel").replaceChildren(fdRow(unitOf(drive, 0)), fdRow(unitOf(drive, 1)), newFloppyRow(drive));
  }
  const hd = $("devhd");
  hd.querySelector(".devname").innerHTML = `<b>HD</b> ` + (slots.hd || "—");
  hd.querySelector(".panel").replaceChildren(...hdRows());
  if (commander && !$("fm").hidden && !commanderHold) commander.open();   // a mount changed: the panes' sources follow
}

function lamps() {
  const on = [
    api.diskActive(h, 0) || api.diskActive(h, 2),
    api.diskActive(h, 1) || api.diskActive(h, 3),
    api.hdActive(h),
  ];
  ["dev0", "dev1", "devhd"].forEach((id, i) => $(id).querySelector(".lamp").classList.toggle("on", !!on[i]));
}

// ── the machine ────────────────────────────────────────────────────────────
async function boot() {
  booted = true;
  $("spin").hidden = false;
  say("loading…");
  stop();
  keyboard.reset();
  if (startFile?.run) { startRun(); start(); return; }
  followRom(slots.fd[unitOf(0, 0)]);       // a mount restored from the last visit, or ?disk=
  const rom = $("rom").value;
  M.FS.writeFile("/rom.bin", await fetchBytes(ROMS[rom]));
  if (!api.loadRom(h, "/rom.bin")) throw new Error("ROM load failed");
  api.reset(h);
  setHalted(false);
  saveMounts();
  const disk = slots.fd[unitOf(0, 0)];
  if (startFile) await resumeStart();
  else if (!disk) hint("nothing in drive A: open its panel, pick an image, Reset");
  else hint(SHIPPED.get(disk)?.hint || "the machine boots from drive A side 0");
  start();
}

// ── a start file: the machine at a chosen moment (start.js) ───────────────
// `?start=URL` opens the page at it - a game just started, say.  The file
// carries the snapshot, the images the drives held and the settings; the
// images go into the module's file system under their names, straight
// from the file (not into IndexedDB: a start is the same every time it is
// opened, and what the guest writes stays with the visit), the ROM and the
// settings are the page's for this visit and remembered by nobody, and
// every boot - the first, a Reset - is the snapshot again.
async function loadStart(url) {
  say("loading the start file…");
  const start = await parseStart(await fetchBytes(url));
  M.FS.mkdirTree("/disks");
  for (const [name, bytes] of start.disks) M.FS.writeFile(pathOf(name), bytes);
  return start;
}

// The snapshot in, then the images by the drives start.json names: a
// snapshot mounts by the paths of the machine it was taken on, which are
// not this one's.
async function resumeStart() {
  M.FS.writeFile(STATE_PATH, startFile.state);
  if (!api.load(h, STATE_PATH)) throw new Error("the start file's snapshot does not load on this ROM");
  api.history(h, HISTORY_EVENTS);   // the state brought its own ring: ours again
  for (let unit = 0; unit < 4; ++unit) {
    const name = slots.fd[unit];
    if (!name) continue;
    const path = pathOf(name), drive = driveOf(unit);
    const ok = ds[drive] ? api.mount(h, unitOf(drive, 0), path) && api.mount(h, unitOf(drive, 1), path)
                         : api.mount(h, unit, path);
    if (!ok) throw new Error(`${name}: mount after the snapshot failed`);
  }
  if (slots.hd && !api.mountHd(h, pathOf(slots.hd))) throw new Error(`${slots.hd}: mount after the snapshot failed`);
  say(startFile.meta.title || "the start file's moment");
}

// ── a program run from its card ───────────────────────────────────────────
// `?run=URL` opens the page at a program already running - a game - the way
// ms0515-run starts one on the host: no diskette, no boot, no prompt.  The
// module carries ROM-B, a system and the monitor booted; the card names
// the program and the files it reads, which go into a directory of the
// module's file system and are DK: to the program.  The page is the screen
// and, under it, the card's words on what to do, in the visitor's language:
//
//   { "schema": 1,
//     "title": { "en": "...", "ru": "..." },
//     "program": "BIRDS.SAV",            its name among the files
//     "arguments": "",                   the program's command line, if any
//     "files": ["BIRDS.SAV", ...],       URLs, relative to the card
//     "joystick": false,                 the arrows and Space drive the MS7007 port
//     "em": false,                       the EIS/FIS instruction emulator on
//     "sound": { "speaker": true },      as in a start file
//     "text": { "en": "...", "ru": "..." },    what to do; a blank line parts paragraphs
//     "menu": [ { "name": "LINES", "about": { "en": "...", "ru": "..." },
//                 "type": "LOAD LINES\rRUN\r" }, ... ],
//     "open": { "label": { "en": "...", "ru": "..." }, "accept": ".bas",
//               "text": true, "type": "LOAD {NAME}\r" },
//     "ready": 2500 }
//
// `menu` is for a program that is a place to run other things in - an
// interpreter with its programs: a list under the words, and a click on an
// entry starts the program afresh and types the entry's line at it, `ready`
// milliseconds after the start (when its prompt is there).  `open` adds a
// button for a file of the visitor's own: it goes into the folder under an
// RT-11 name made of its own, and the line is typed with {NAME} standing
// for the name without the extension; `text` says it is a text, to be given
// the machine's line ends and KOI-8 letters.
//
// The visit is a start visit in everything else: nothing of it is
// remembered, and what the program writes stays with the tab.
async function loadRun(url) {
  say("loading the program…");
  if (!api.runBuilt()) throw new Error("this build of the page cannot run a program by itself");
  let card;
  try { card = JSON.parse(new TextDecoder().decode(await fetchBytes(url))); } catch (e) { throw new Error(`${url}: ${e.message}`); }
  if (card.schema !== 1) throw new Error(`the run card's schema is ${card.schema}, this page reads 1`);
  if (typeof card.program !== "string" || !Array.isArray(card.files) || !card.files.length)
    throw new Error("the run card names no program or no files");
  const baseName = (f) => f.split("/").pop();
  M.FS.mkdirTree(RUN_DIR);
  await Promise.all(card.files.map(async (f) =>
    M.FS.writeFile(`${RUN_DIR}/${baseName(f)}`, await fetchBytes(new URL(f, url)))));
  return {
    meta: { title: inLanguage(card.title), rom: "b", disks: { fd: ["", "", "", ""], hd: "" },
            sound: { ...DEFAULT_SOUND, ...(card.sound ?? {}) }, joystick: !!card.joystick, speed: 100 },
    disks: new Map(),
    run: { program: `${RUN_DIR}/${baseName(card.program)}`, arguments: card.arguments ?? "", em: !!card.em,
           text: inLanguage(card.text), over: 0, ready: card.ready ?? 2500,
           menu: (card.menu ?? []).map((m) => ({ name: m.name, about: inLanguage(m.about), type: m.type })),
           open: card.open ? { label: inLanguage(card.open.label), accept: card.open.accept ?? "",
                               text: !!card.open.text, type: card.open.type } : null },
  };
}

// The program afresh, and a line typed at it once it is ready for one.
async function runTyped(text) {
  typing.queue.length = 0;
  await boot();
  typing.type(text, startFile.run.ready);
}

// A file of the visitor's own into the program's folder, and the card's
// line for it typed.  A text gets CR LF line ends and, where it has letters
// past ASCII, KOI-8 for them - what the machine's programs read.
async function runOwnFile(file) {
  const open = startFile.run.open;
  const name = rt11Name(file.name);
  let bytes = new Uint8Array(await file.arrayBuffer());
  if (open.text) {
    let text = null;
    try { text = new TextDecoder("utf-8", { fatal: true }).decode(bytes); } catch { /* not UTF-8: the machine's own bytes */ }
    if (text !== null) bytes = encodeText(text.replace(/\r?\n/g, "\r\n"), "koi8");
  }
  M.FS.writeFile(`${RUN_DIR}/${name}`, bytes);
  say(`${file.name} is ${name} in the program's folder`);
  await runTyped(open.type.replaceAll("{NAME}", name.split(".")[0]));
}

// The program started, on the machine the module carries; every boot of a
// run visit - the first, the one after the program has ended - is this.
function startRun() {
  const run = startFile.run;
  if (!api.run(h, run.program, run.arguments, run.em ? 1 : 0)) throw new Error(api.runError());
  api.history(h, HISTORY_EVENTS);
  run.over = 0;
  setHalted(false);
  $("start").hidden = true;
  say(startFile.meta.title || "the program");
}

// The program has ended - a game left by its own key: the machine stands
// still on its last picture, and the arrow starts it again.
function runOver(how) {
  startFile.run.over = how;
  stop();
  paint();
  say(how === 2 ? "the program has stopped with an error" : "the program has ended");
  $("start").hidden = false;
  $("start").onclick = () => { resumeSound(); boot().catch(fail); };
}

// The card's words under the screen.
function showAbout() {
  document.body.classList.add("run");
  document.title = startFile.meta.title || document.title;
  $("abouttitle").textContent = startFile.meta.title;
  $("abouttext").replaceChildren(...startFile.run.text.split(/\n\s*\n/).map((p) => el("p", null, p.trim())));
  const { menu, open } = startFile.run;
  const list = $("aboutmenu");
  list.replaceChildren();
  if (open) {
    const own = el("button", "own", open.label);
    own.onclick = () => {
      const input = $("file");
      input.value = "";
      input.accept = open.accept;
      input.onchange = () => { if (input.files[0]) runOwnFile(input.files[0]).catch(fail); };
      input.click();
    };
    list.append(own);
  }
  for (const item of menu) {
    const row = el("button", "item");
    row.append(el("b", null, item.name), el("span", null, item.about));
    row.onclick = () => runTyped(item.type).catch(fail);
    list.append(row);
  }
  list.hidden = !open && !menu.length;
  document.body.classList.toggle("menu", !list.hidden);
  $("about").hidden = EMBED;               // in somebody's frame the words are theirs to give
  fit();                                   // the screen has less room now
}

function start() {
  $("spin").hidden = true;                 // whatever was being waited for is here
  if (running) return;
  running = true;
  lastTick = performance.now();
  acc = 0;
  requestAnimationFrame(loop);
}

function stop() { running = false; }

function loop(now) {
  if (!running) return;
  // Real time elapsed, scaled by the speed: at 200% a second of ours is two
  // seconds of the machine's.  A burst is clamped like the SDL front-end
  // does (a tab that was hidden does not replay its absence), and one
  // animation frame runs the machine for 14 ms of our time at most - what
  // the host cannot keep up with is dropped, not queued.
  acc += Math.min(now - lastTick, 200) * (speedPct / 100);
  lastTick = now;
  const budgetEnd = performance.now() + 14;
  let n = 0;
  while (acc >= FRAME_MS && running && performance.now() < budgetEnd) {
    acc -= FRAME_MS;
    step(now);
    ++n;
  }
  if (acc > 4 * FRAME_MS) acc = 4 * FRAME_MS;
  if (n) { paint(); lamps(); }
  requestAnimationFrame(loop);
}

function step(now) {
  typing.tick(now);
  const cycles = api.frame(h);
  ++frames;
  speakerTransitions += api.transitions(h);
  if (cycles === 0) { setHalted(true); say("CPU halted — \"Bug report\" saves everything needed to look into it"); stop(); return; }
  if (startFile?.run) { const over = api.runEnded(h); if (over) { runOver(over); return; } }
  if (speaker && speedPct === 100) queueAudio();
  if ((frames & 63) === 0) flushDisks();
}

// ── the bug report ────────────────────────────────────────────────────────
// A machine that halts or stops answering cannot be looked into from a
// screenshot: the button packs the snapshot, the ROM, the mounted images
// and what the page knows into one .zip (bugreport.js).  The run stops
// while the note is asked, so the state saved is the state of the moment.
function setHalted(on) {
  halted = on;
  $("bug").classList.toggle("alert", on);
}

// `at` is what the page knew the moment the button was pressed: the run
// is stopped and the status line taken over while the report is packed,
// so neither would describe the machine any more.
function bugDeps(at = {}) {
  return {
    api, handle: h, module: M, canvas, pathOf,
    rom: () => $("rom").value,
    mounts: () => ({ fd: [...slots.fd], hd: slots.hd }),
    machine: () => ({
      running: at.running ?? running,
      halted, frames, speedPct,
      status: at.status ?? status.textContent,
      pc: h ? api.pc(h).toString(8).padStart(6, "0") : null,
      regC: h ? api.regC(h).toString(8).padStart(3, "0") : null,
      ruslat: h ? api.ruslat(h) : null,
      caps: h ? api.caps(h) : null,
      speakerTransitions,
      sound: audio ? audio.state : "off",
    }),
  };
}

function askBugReport() {
  const dlg = $("bugdlg");
  if (!dlg.showModal) { saveBugReport("", { running, status: status.textContent }).catch(fail); return; }   // no <dialog> here: the file alone
  const at = { running, status: status.textContent };
  stop();
  $("bugnote").value = "";
  dlg.returnValue = "";
  dlg.showModal();
  dlg.addEventListener("close", () => {
    const note = $("bugnote").value;
    const done = dlg.returnValue === "save" ? saveBugReport(note, at) : Promise.resolve();
    done.catch(fail).finally(() => { if (at.running) start(); });
  }, { once: true });
}

async function saveBugReport(note, at) {
  say("packing the report…");
  const { name, size } = await bugreport.save(bugDeps(at), note);
  say(`${name} saved (${Math.round(size / 1024)} KB): send it with what you were doing`);
}

// ── the saved state ───────────────────────────────────────────────────────
// "Save state" writes the snapshot into the module's file system (the
// tab's memory) and keeps a copy in IndexedDB with the ROM and the mounts
// it was taken with, so "Restore" finds it after a reload as well.  A
// snapshot carries the ROM's CRC and refuses to load against another one,
// so the ROM is checked here to say why rather than just "failed".
const STATE_PATH = "/state.bin";
const shortTime = (iso) => { try { return new Date(iso).toLocaleString(); } catch { return iso; } };

async function saveState() {
  if (!h) return;
  if (!api.save(h, STATE_PATH)) throw new Error("save state failed");
  const bytes = M.FS.readFile(STATE_PATH);
  await statePut({ bytes, saved: new Date().toISOString(), rom: $("rom").value,
                   mounts: { fd: [...slots.fd], hd: slots.hd } });
  say(`state saved (${Math.round(bytes.length / 1024)} KB): Restore brings the machine back to it, this visit or the next`);
}

async function restoreState() {
  if (!h) return;
  const rec = await stateGet();
  if (!rec) { say("no saved state: press \"Save state\" first"); return; }
  if (rec.rom && rec.rom !== $("rom").value) {
    say(`the state was saved with ROM ${rec.rom.toUpperCase()}: pick it, Reset, then Restore`);
    return;
  }
  M.FS.writeFile(STATE_PATH, rec.bytes);
  if (!api.load(h, STATE_PATH)) { say("restore failed: the state does not fit this ROM"); return; }
  api.history(h, HISTORY_EVENTS);   // the state brought its own ring: ours again
  paint();
  // The snapshot mounts the floppies by the paths they had; an image that
  // is not in this session leaves its drive empty.
  const wanted = [...new Set([...(rec.mounts?.fd ?? []), rec.mounts?.hd].filter(Boolean))];
  const gone = wanted.filter((name) => !M.FS.analyzePath(pathOf(name)).exists);
  say(`state restored (saved ${shortTime(rec.saved)})`
      + (gone.length ? ` — ${gone.join(", ")} is not mounted now: mount it and Restore again` : ""));
}

// ── speed ──────────────────────────────────────────────────────────────────
function setSpeed(pct, persist = true) {
  pct = Math.round(Math.min(SPEED_MAX, Math.max(SPEED_MIN, +pct || 100)) / 10) * 10;
  speedPct = pct;
  $("speed").value = pct;
  $("speedv").textContent = pct + "%";
  if (persist) localStorage.setItem(SPEED_KEY, String(pct));
  if (speaker && pct !== 100) hint("sound plays at 100% only - quiet at " + pct + "%");
}

function paint() {
  const ptr = api.render(h);
  image.data.set(M.HEAPU8.subarray(ptr, ptr + image.data.length));
  ctx.putImageData(image, 0, 0);
}

// The style sheet fits the screen to the page; whole pixels when it can
// (a multiple of 320 x 200 keeps the picture crisp).
function fit() {
  canvas.style.width = "";
  const w = canvas.getBoundingClientRect().width;
  if (w >= 640) canvas.style.width = Math.floor(w / 320) * 320 + "px";
}

// ── sound: each frame's PCM to the worklet on the audio thread ─────────────
function queueAudio() {
  const max = 4096;
  if (!pcmBuf) pcmBuf = M._malloc(max * 2);
  const n = api.audio(h, pcmBuf, max, audio.sampleRate);
  if (n <= 0) return;
  const pcm = M.HEAP16.subarray(pcmBuf >> 1, (pcmBuf >> 1) + n);
  const chunk = new Float32Array(n);
  for (let i = 0; i < n; ++i) chunk[i] = pcm[i] / 32768;
  speaker.port.postMessage(chunk, [chunk.buffer]);
}

// Fetched as soon as the drive is to be heard at all - they are bytes for the
// module and want no audio context, so they need not wait for one to be built.
// The boot waits for this, and only this.
function fetchDriveSounds() {
  if (!$("sndDrive").checked || driveSoundsLoaded || driveSoundsPending) return;
  driveSoundsPending = loadDriveSounds()
    .then(() => soundBoxes(!!audio))
    .catch(() => hint("the drive's recordings are not on this page"));
}

// The drive's recordings: a megabyte and a half of WAV files, fetched once the
// drive's box is ticked - which it is when the page opens - and handed to the
// module as bytes: it parses them itself, the same parser the desktop build
// uses.  Nothing waits for them.
let driveSoundsLoaded = false, driveSoundsPending = null;
async function loadDriveSounds() {
  if (driveSoundsLoaded) return true;
  const list = await fetch("sounds/index.json").then((r) => r.ok ? r.json() : []);
  // Asked for together, not one after another.  Ninety-three of them, and
  // over the network a request costs about two hundred milliseconds whatever
  // it carries: in a row that is nineteen seconds, and the machine is waiting
  // on them.  Together they are here in one.  A file that does not come is
  // left out rather than losing the rest.
  const files = await Promise.all(list.map(async (path) => {
    const bytes = await fetch("sounds/" + path)
      .then((r) => r.ok ? r.arrayBuffer() : null).catch(() => null);
    return bytes ? [path, new Uint8Array(bytes)] : null;
  }));
  let got = 0;
  for (const file of files) {
    if (!file) continue;
    const [path, bytes] = file;
    const ptr = M._malloc(bytes.length);
    M.HEAPU8.set(bytes, ptr);
    got += api.driveSound(h, path.slice(path.lastIndexOf("/") + 1), ptr, bytes.length);
    M._free(ptr);
  }
  driveSoundsLoaded = got > 0;
  return driveSoundsLoaded;
}

// The three boxes beside the Sound button: what the machine may make a noise
// with.  They only mean anything while sound is on, so they follow it.
function soundBoxes(on) {
  for (const id of ["sndSpeaker", "sndDrive", "sndKbd"]) $(id).disabled = !on;
  if (!on) return;
  api.speakerSound(h, $("sndSpeaker").checked ? 1 : 0);
  api.kbdSounds(h, $("sndKbd").checked ? 1 : 0);
  api.driveSounds(h, $("sndDrive").checked && driveSoundsLoaded ? 1 : 0);
}

// What the user last chose, so the page opens the way it was left; a first
// visit takes the defaults from the markup, with the sound on.
const SOUND_KEY = "ms0515.sound";
function soundWish() {
  const d = { on: true, speaker: $("sndSpeaker").checked,
              drive: $("sndDrive").checked, kbd: $("sndKbd").checked };
  try { return { ...d, ...JSON.parse(localStorage.getItem(SOUND_KEY) ?? "{}") }; } catch { return d; }
}
function saveSound() {
  if (startFile) return;                   // the file's choice, for this visit only
  localStorage.setItem(SOUND_KEY, JSON.stringify({
    on: !!audio, speaker: $("sndSpeaker").checked,
    drive: $("sndDrive").checked, kbd: $("sndKbd").checked }));
}

// A browser lets no page make a noise before the user has touched it, so
// "sound on when the page opens" can only mean: build the context at once
// and leave it suspended until the first click or key resumes it.  The
// machine is already running by then, and nothing of it was lost - a
// suspended context swallows the samples, it does not queue them up.
async function toggleSound() {
  if (audio) {
    speaker = null;
    await audio.close();
    audio = null;
    $("sound").textContent = "Sound: off";
    soundBoxes(false);
    saveSound();
    return;
  }
  const ctx = new AudioContext();
  if (!ctx.audioWorklet) {                 // http by an address: not a secure context
    await ctx.close();
    throw new Error("sound needs a secure page (https, or localhost): the AudioWorklet is not available here");
  }
  try {
    await ctx.audioWorklet.addModule("audio-worklet.js?v=@STAMP@");
  } catch (e) {
    await ctx.close();
    throw e;
  }
  audio = ctx;
  speaker = new AudioWorkletNode(audio, "ms0515-speaker");
  speaker.port.onmessage = (e) => { audioStats = e.data; };
  speaker.connect(audio.destination);
  // Not awaited: a browser that is waiting for the user to touch the page
  // leaves this promise pending - not rejected - until they do, and the
  // machine is not going to stand still for that.
  if (audio.state !== "running") audio.resume().catch(() => {});
  $("sound").textContent = "Sound: on";
  soundBoxes(true);
  saveSound();
  // The drive's recordings are a megabyte and a half; the beeper and the
  // keyboard are not made to wait for them, and neither is the boot.
  fetchDriveSounds();
}

// The gesture the browser waits for is any of the ones the machine gets
// anyway.  It stays on for the life of the page: a context can be suspended
// again later (the tab put aside), and the next touch brings it back.
function resumeSound() {
  if (audio && audio.state !== "running") audio.resume().catch(() => {});
}

// ── where the keys go ──────────────────────────────────────────────────────
// To the machine.  Pressing something in the interface - a button, one of the
// sound boxes, the speed slider - must not take the keyboard away from it:
// otherwise the next thing typed is lost, and every control added later has
// to remember to hand the focus back, which is how this page used to work.
//
// What does take the keyboard is what needs it: anything being typed into,
// and the Files panels with everything they open - the viewer, the editor,
// their dialogs, the disk wizard - all of which live inside #fm.
//
// The screen itself is not focusable at all any more: it does not need to be,
// and a canvas that can hold the focus only gives the page one more place for
// it to sit.
const TYPED_INTO = "input:not([type=checkbox]):not([type=radio]):not([type=range])" +
                   ":not([type=button]):not([type=submit]), textarea, select," +
                   " [contenteditable=''], [contenteditable=true]";

function pageTakesKeys() {
  const el = document.activeElement;
  if (!el || el === document.body || el === document.documentElement) return false;
  return el.closest("#fm") !== null || el.matches(TYPED_INTO);
}

// ── keyboard: the SDL front-end's PhysicalKeyboard, host codes in ──────────
// A host key maps by character (mapKey) to an MS7004 key plus the Shift it
// needs there; the difference with the host's Shift is made up with a
// synthetic Shift that is undone at release; CAPS + Shift inverts a
// letter's case; the numpad / * + and a few РУС-mode symbols are handled
// as special cases, as in PhysicalKeyboard.cpp.
const MODIFIER_CODES = new Set(["ShiftLeft", "ShiftRight", "ControlLeft", "ControlRight",
                                "AltLeft", "AltRight", "CapsLock"]);
const keyboard = {
  held: new Map(),        // host code -> MS7004 key id pressed for it
  overrides: new Map(),   // host code -> { added, removedL, removedR }
  reset() { this.held.clear(); this.overrides.clear(); },

  tap(names) {            // an instant press + release of each key in turn
    for (const n of names) { api.key(h, K(n), 1); api.key(h, K(n), 0); }
  },

  down(code, hostShiftHint) {
    if (code === "NumpadDivide") { api.key(h, K("Slash"), 1); return; }
    if (code === "NumpadMultiply") { api.key(h, K("ShiftL"), 1); api.key(h, K("ColonStar"), 1); return; }
    if (code === "NumpadAdd") { api.key(h, K("ShiftL"), 1); api.key(h, K("SemiPlus"), 1); return; }

    // The right Shift with an F-key is a key a PC has no cap for.  The
    // machine does not see that Shift under it - and not after it either:
    // pressed again when the F-key goes up, it is a key to a program that
    // waits for any (ROSA's help closed at once).  It comes back with the
    // next ordinary key, if it is still held.  The left Shift stays the
    // machine's throughout.
    const rightHeld = this.held.has("ShiftRight");
    const fn = rightHeld ? shiftedFunctionKey(code) : null;
    if (fn) {
      api.key(h, K("ShiftR"), 0);
      api.key(h, K(fn), 1);
      this.held.set(code, K(fn));
      return;
    }
    if (rightHeld && !MODIFIER_CODES.has(code) && api.keyHeld(h, K("ShiftR")) !== 1)
      api.key(h, K("ShiftR"), 1);

    const rus = api.ruslat(h) === 1;
    const shiftL = api.keyHeld(h, K("ShiftL")) === 1;
    const shiftR = api.keyHeld(h, K("ShiftR")) === 1;
    const hostShift = shiftL || shiftR || !!hostShiftHint;

    // РУС: the host's \ is Э, its Shift+- is Ъ - switch to ЛАТ for an instant.
    if (rus && !hostShift && code === "Backslash") { this.tap(["RusLat", "Backslash", "RusLat"]); return; }
    if (rus && hostShift && code === "Minus") { this.tap(["RusLat", "Underscore", "RusLat"]); return; }

    const { key, withShift } = mapKey(code, hostShift, rus);
    if (!key) return;
    this.held.set(code, K(key));

    const capsInvert = isLetterKey(key, rus) && api.caps(h) === 1 && hostShift;
    const needShift = capsInvert ? false : withShift;
    if (capsInvert) {
      if (shiftL) api.key(h, K("ShiftL"), 0);
      if (shiftR) api.key(h, K("ShiftR"), 0);
      this.tap(["Caps"]);
      this.tap([key]);
      this.tap(["Caps"]);
      if (shiftL) api.key(h, K("ShiftL"), 1);
      if (shiftR) api.key(h, K("ShiftR"), 1);
      this.held.delete(code);
    } else if (needShift !== hostShift) {
      if (hostShift && !needShift) {
        if (shiftL) api.key(h, K("ShiftL"), 0);
        if (shiftR) api.key(h, K("ShiftR"), 0);
        api.key(h, K(key), 1);
        this.overrides.set(code, { added: false, removedL: shiftL, removedR: shiftR });
      } else {
        api.key(h, K("ShiftL"), 1);
        api.key(h, K(key), 1);
        this.overrides.set(code, { added: true, removedL: false, removedR: false });
      }
    } else {
      api.key(h, K(key), 1);
    }
  },

  up(code) {
    if (code === "NumpadDivide") { api.key(h, K("Slash"), 0); return; }
    if (code === "NumpadMultiply") { api.key(h, K("ColonStar"), 0); api.key(h, K("ShiftL"), 0); return; }
    if (code === "NumpadAdd") { api.key(h, K("SemiPlus"), 0); api.key(h, K("ShiftL"), 0); return; }
    const id = this.held.get(code);
    if (id === undefined) return;
    api.key(h, id, 0);
    const ov = this.overrides.get(code);
    if (ov) {
      const physShift = this.held.has("ShiftLeft") || this.held.has("ShiftRight");
      if (ov.added && !physShift) api.key(h, K("ShiftL"), 0);
      if (ov.removedL && this.held.has("ShiftLeft")) api.key(h, K("ShiftL"), 1);
      if (ov.removedR && this.held.has("ShiftRight")) api.key(h, K("ShiftR"), 1);
      this.overrides.delete(code);
    }
    this.held.delete(code);
  },
};

function onKey(e, down) {
  if (!h) return;
  if (e.repeat) { e.preventDefault(); return; }         // the MS7004 repeats itself
  if (joystick.key(e.code, down)) { e.preventDefault(); return; }   // the arrows and Space are the joystick's while it is on
  if (!mapKey(e.code, false, false).key && !e.code.startsWith("Numpad")) return;
  e.preventDefault();
  if (down) keyboard.down(e.code); else keyboard.up(e.code);
}

// ── typing: a string as key presses, 60 ms a key (the `type=` parameter) ──
const typing = {
  queue: [], next: 0, pending: null,
  type(text, delayMs = 0) {
    for (const ch of text) { const k = charToHostKey(ch); if (k) this.queue.push(k); }
    this.next = performance.now() + delayMs;
  },
  // Items { code, shift, rus }: `rus` names the mode the machine must be in
  // for the key (a letter from the on-screen keyboard); the queue switches
  // РУС/ЛАТ on its way when the lamp says otherwise.
  push(items) { this.queue.push(...items); },
  tick(now) {
    if (!h || now < this.next) return;
    if (this.pending) {                    // release the key pressed last time
      keyboard.up(this.pending.code);
      if (this.pending.shift) keyboard.up("ShiftLeft");
      this.pending = null;
      this.next = now + 60;
      return;
    }
    let k = this.queue.shift();
    if (!k) return;
    if (k.rus !== undefined && (api.ruslat(h) === 1) !== k.rus) {
      this.queue.unshift(k);               // the mode first: РУС/ЛАТ, then the key
      k = { code: "AltRight", settle: 200 };
    }
    if (k.shift) keyboard.down("ShiftLeft");
    keyboard.down(k.code, k.shift);
    this.pending = k;
    this.next = now + (k.settle ?? 60);
  },
};

// ── full screen: the picture alone, the toolbar and the status gone ────────
function fullscreenOn() { return !!(document.fullscreenElement || document.webkitFullscreenElement); }
function toggleFullscreen() {
  const el = document.querySelector("main");
  if (fullscreenOn()) {
    (document.exitFullscreen || document.webkitExitFullscreen).call(document);
  } else {
    const req = el.requestFullscreen || el.webkitRequestFullscreen;
    if (req) Promise.resolve(req.call(el)).catch(fail);
  }
}

// ── the page ───────────────────────────────────────────────────────────────
function bindApi() {
  const c = (name, ret, args) => M.cwrap(name, ret, args);
  api = {
    create:  c("ms_create", "number", []),
    reset:   c("ms_reset", null, ["number"]),
    loadRom: c("ms_load_rom", "number", ["number", "string"]),
    mount:   c("ms_mount", "number", ["number", "number", "string"]),
    unmount: c("ms_unmount", null, ["number", "number"]),
    diskActive: c("ms_disk_active", "number", ["number", "number"]),
    mountHd:   c("ms_mount_hd", "number", ["number", "string"]),
    unmountHd: c("ms_unmount_hd", null, ["number"]),
    hdActive:  c("ms_hd_active", "number", ["number"]),
    frame:   c("ms_frame", "number", ["number"]),
    render:  c("ms_render", "number", ["number"]),
    audio:   c("ms_audio", "number", ["number", "number", "number", "number"]),
    driveSound:  c("ms_drive_sound", "number", ["number", "string", "number", "number"]),
    driveSounds: c("ms_drive_sounds", null, ["number", "number"]),
    kbdSounds:   c("ms_keyboard_sounds", null, ["number", "number"]),
    speakerSound: c("ms_speaker_sound", null, ["number", "number"]),
    transitions: c("ms_transitions", "number", ["number"]),
    regC:    c("ms_reg_c", "number", ["number"]),
    key:     c("ms_key", null, ["number", "number", "number"]),
    keyMax:  c("ms_key_max", "number", []),
    keyHeld: c("ms_key_held", "number", ["number", "number"]),
    ruslat:  c("ms_ruslat", "number", ["number"]),
    caps:    c("ms_caps", "number", ["number"]),
    releaseAll: c("ms_key_release_all", null, ["number"]),
    joystick: c("ms_joystick", null, ["number", "number"]),
    diskDir:    c("ms_disk_dir", "string", ["string", "number", "number"]),
    diskDetect: c("ms_disk_detect", "string", ["string"]),
    version:    c("ms_version", "string", []),
    diskError:  c("ms_disk_error", "string", []),
    diskGet:    c("ms_disk_get", "number", ["string", "number", "number", "string"]),
    diskData:   c("ms_disk_data", "number", []),
    diskArea:   c("ms_disk_area", "number", ["string", "number", "number", "number"]),
    diskBlank:  c("ms_disk_blank", "number", ["number"]),
    diskPut:    c("ms_disk_put", "number", ["string", "number", "number", "string", "number", "number", "number", "number", "number", "number"]),
    diskRm:     c("ms_disk_rm", "number", ["string", "number", "number", "string"]),
    diskRename: c("ms_disk_rename", "number", ["string", "number", "number", "string", "string"]),
    diskInit:   c("ms_disk_init", "number", ["string", "number", "number", "number", "number", "number", "number", "number"]),
    diskVolumeId: c("ms_disk_volume_id", "number", ["string", "number", "number", "number", "number", "number", "number"]),
    diskSqueeze: c("ms_disk_squeeze", "number", ["string", "number", "number"]),
    diskProtect: c("ms_disk_protect", "number", ["string", "number", "number", "string", "number"]),
    diskGrow:    c("ms_disk_grow", "number", ["string", "number"]),
    diskBooted:  c("ms_disk_booted", "string", ["string", "number", "number"]),
    diskKit:     c("ms_disk_kit", "string", ["string", "number", "number", "number"]),
    diskSystem:  c("ms_disk_system", "number", ["string", "number", "number", "string", "number", "number", "string"]),
    diskText:    c("ms_disk_text", "string", []),
    diskUndelete: c("ms_disk_undelete", "number", ["string", "number", "number", "number", "string"]),
    ldCreate: c("ms_ld_create", "number", ["number", "number", "string"]),
    ldPut:    c("ms_ld_put", "number", ["string", "number", "number", "number", "number", "number", "number"]),
    ldData:   c("ms_ld_data", "number", []),
    ldSize:   c("ms_ld_size", "number", []),
    history: c("ms_history", null, ["number", "number"]),
    pc:      c("ms_pc", "number", ["number"]),
    save:    c("ms_save_state", "number", ["number", "string"]),
    load:    c("ms_load_state", "number", ["number", "string"]),
    runBuilt: c("ms_run_built", "number", []),
    run:      c("ms_run", "number", ["number", "string", "string", "number"]),
    runError: c("ms_run_error", "string", []),
    runEnded: c("ms_run_ended", "number", ["number"]),
  };
  if (api.keyMax() !== KEYS.length - 1)
    throw new Error(`key table drift: module ${api.keyMax()}, page ${KEYS.length - 1}`);
}

// The drives' panels: one open at a time; the ROM and the buttons.
function setJoystick(on) {
  joystick.enable(on);
  $("joystick").textContent = on ? "Joystick: on" : "Joystick: off";
}

function bindControls() {
  const panels = [...document.querySelectorAll("details.dev")];
  for (const d of panels)
    d.addEventListener("toggle", () => { if (d.open) for (const o of panels) if (o !== d) o.open = false; });
  document.addEventListener("click", (e) => { if (!e.target.closest("details.dev")) for (const o of panels) o.open = false; });
  $("rom").onchange = saveMounts;
  // A click on a toolbar button must not keep the focus: the keys are the
  // Full screen: the button, F11; hidden where the API is not there (an iPhone).
  $("fullscreen").hidden = !(document.fullscreenEnabled || document.webkitFullscreenEnabled);
  $("fullscreen").onclick = toggleFullscreen;
  document.addEventListener("keydown", (e) => {     // (the right Shift's F11 is the machine's F13)
    if (e.key === "F11" && !keyboard.held.has("ShiftRight")) { e.preventDefault(); toggleFullscreen(); }
  });
  document.addEventListener("fullscreenchange", fit);
  document.addEventListener("webkitfullscreenchange", fit);
  // The OS's on-screen keyboard on a touch device; the page shrinks to what
  // the keyboard leaves (the visual viewport) so the picture stays in view.
  softkbd = new SoftKeyboard($("softkey"), (items) => typing.push(items), onKey, charToHostKey);
  $("softkbd").hidden = !isTouchDevice();
  $("softkbd").onclick = () => softkbd.toggle();
  commander = new Commander($("fm"), { sources: fileSources, api, module: () => M, writable: writableImage, say,
                                       onClose: () => { if (!$("fm").hidden) toggleCommander(); } });
  $("files").onclick = toggleCommander;
  if (window.visualViewport) {
    visualViewport.addEventListener("resize", () => {
      const shrunk = visualViewport.height < innerHeight - 100;
      document.body.style.height = shrunk ? visualViewport.height + "px" : "";
      fit();
    });
  }
  joystick = new Joystick((bits) => { if (h) api.joystick(h, bits); }, $("joy"));
  $("joystick").onclick = () => setJoystick(!joystick.enabled);
  $("boot").onclick = () => boot().catch(fail);
  $("sound").onclick = () => toggleSound().catch(fail);
  $("sndSpeaker").onchange = () => { soundBoxes(!!audio); saveSound(); };
  $("sndKbd").onchange = () => { soundBoxes(!!audio); saveSound(); };
  $("sndDrive").onchange = async () => {
    if ($("sndDrive").checked && !driveSoundsLoaded) {
      $("sndDrive").disabled = true;
      const ok = await loadDriveSounds().catch(() => false);
      $("sndDrive").disabled = false;
      if (!ok) { $("sndDrive").checked = false; hint("the drive's recordings are not on this page"); }
    }
    soundBoxes(!!audio);
    saveSound();
  };
  window.addEventListener("pointerdown", resumeSound, true);
  window.addEventListener("keydown", resumeSound, true);
  $("speed").oninput = (e) => setSpeed(e.target.value);
  $("speed").ondblclick = () => setSpeed(100);     // a double click on the slider: back to 100%
  $("speedv").onclick = () => setSpeed(100);
  setSpeed(localStorage.getItem(SPEED_KEY) ?? 100, false);
  $("save").onclick = () => saveState().catch(fail);
  $("restore").onclick = () => restoreState().catch(fail);
  $("bug").onclick = () => askBugReport();
  $("wipe").onclick = () => wipe().catch(fail);
  document.addEventListener("keydown", (e) => { if (!pageTakesKeys()) onKey(e, true); });
  document.addEventListener("keyup", (e) => { if (!pageTakesKeys()) onKey(e, false); });
  // Whatever the page took the keyboard for, the machine must not be left
  // holding keys it will never see released.
  document.addEventListener("focusin", () => { if (pageTakesKeys() && h) { api.releaseAll(h); keyboard.reset(); } });
  window.addEventListener("blur", () => { if (h) { api.releaseAll(h); keyboard.reset(); } });
  // A press is a press, not a claim on the keyboard: a control that was
  // clicked lets it go again at once, and the machine keeps typing.  Inside
  // the Files panels nothing is dropped - there the keyboard is the point.
  document.addEventListener("click", () => {
    const el = document.activeElement;
    if (el && el !== document.body && !el.closest("#fm") && !el.matches(TYPED_INTO)) el.blur();
  });
  window.addEventListener("beforeunload", flushDisks);
}

async function main() {
  if (EMBED || QUERY.get("run")) document.body.classList.add("embed");
  fit();
  window.addEventListener("resize", fit);
  M = await createMs0515({ locateFile: (f) => f + "?v=@STAMP@" });
  bindApi();
  $("ver").textContent = "v" + api.version();
  image = ctx.createImageData(canvas.width, canvas.height);
  h = api.create();
  api.history(h, HISTORY_EVENTS);   // the machine's own trail, for a bug report

  await loadDiskList();
  if (QUERY.get("start")) startFile = await loadStart(new URL(QUERY.get("start"), location.href));
  else if (QUERY.get("run")) { startFile = await loadRun(new URL(QUERY.get("run"), location.href)); showAbout(); }
  const m = startFile ? { rom: startFile.meta.rom, fd: [...startFile.meta.disks.fd], hd: startFile.meta.disks.hd }
                      : loadMounts();
  $("rom").value = m.rom;
  bindControls();
  if (startFile) setJoystick(startFile.meta.joystick);   // the game's word: the arrows and Space are the port's
  renderDevices();
  // A remembered image that is neither offered any more nor the user's own
  // (the disks the site used to carry) gives way to the first one offered.
  const known = async (name) => SHIPPED.has(name) || own.has(name) || !!(await dbGet(name));
  if (!startFile) for (let unit = 0; unit < 4; ++unit)
    if (m.fd[unit] && !(await known(m.fd[unit]))) m.fd[unit] = unit === 0 ? DISKS[0]?.name ?? "" : "";
  for (let unit = 0; unit < 4; ++unit)
    if (m.fd[unit]) await mountFd(unit, m.fd[unit]).catch(fail);
  if (m.hd) await mountHd(m.hd).catch(fail);

  const wish = soundWish();
  if (startFile) Object.assign(wish, startFile.meta.sound);   // the file's word on which sounds, the visitor's on whether
  if (startFile?.run) wish.on = true;      // a game page has no sound button to find
  $("sndSpeaker").checked = wish.speaker;
  $("sndDrive").checked = wish.drive;
  $("sndKbd").checked = wish.kbd;
  // The recordings first, so that the boot below has something to wait for:
  // building the audio context takes a turn of its own, and the boot used to
  // win that race and run silently.
  if (wish.on) fetchDriveSounds();
  // Started, not awaited, and not fail(): a page served over plain http has no
  // AudioWorklet, and that is a reason for the button to stay off rather than
  // for an error on the screen - and nothing here may hold up the boot.
  if (wish.on) toggleSound().catch((e) => say("no sound: " + e.message));

  say("ready");
  const q = QUERY;
  if (startFile) setSpeed(startFile.meta.speed, false);
  if (q.get("speed")) setSpeed(q.get("speed"), false);   // `speed=200`: for this visit only
  if (q.get("autostart") !== "0") {
    autostart().then(() => {
      // `type=`: a command for the monitor, after the boot (delay= ms, 3000)
      if (q.get("type")) typing.type(RETURN + q.get("type") + RETURN, +(q.get("delay") ?? 3000));
    }).catch(fail);
  }
}

// The machine is not started before its sounds are there.  The drive's
// recordings are a megabyte and a half and arrive a moment after the page
// does; a machine started ahead of them did its whole boot - the seek, the
// Restore - with nothing to play it through, and the visitor pressed Reset a
// second time to hear it.  So the boot waits for them, and for nothing else:
// the browser's own rule, that a page makes no noise until it has been
// touched, is not something to keep the machine waiting on.
async function autostart() {
  $("spin").hidden = false;
  if (driveSoundsPending) {
    say("the drive's recordings are on their way…");
    // Waited for, but not indefinitely: on a line slow enough that they take
    // this long, a machine that never starts is worse than one that starts
    // quietly, and they will be in by the next boot anyway.
    await Promise.race([driveSoundsPending, new Promise((go) => setTimeout(go, WAIT_FOR_SOUNDS))]);
  }
  // A browser lets no page make a sound until it has been touched, so a
  // machine started on opening does the whole of its boot - the seek, the
  // Restore, the ROM's beep - in silence.  Rather than start it deaf, the
  // page says it is ready and asks for that one touch; a visitor the browser
  // already trusts (it does once they have been here before) never sees this,
  // because the sound is running before the question arises.
  if (audio && audio.state !== "running") {
    $("spin").hidden = true;
    $("start").hidden = false;
    say("ready: one click starts the machine, so that you hear it as well as see it");
    await new Promise((go) => {
      const look = () => {
        if (!audio || audio.state === "running" || booted) { audio?.removeEventListener("statechange", look); go(); }
      };
      audio.addEventListener("statechange", look);
      $("start").onclick = () => { resumeSound(); look(); };
      look();
    });
    $("start").hidden = true;
    $("spin").hidden = false;
  }
  if (!booted) await boot();
}

// A peek for scripted checks (the CI's browser run): the frame count, the
// status line, the colours of the picture now; `type` drives the typing.
window.__ms = () => {
  const ptr = h ? api.render(h) : 0;
  const hist = {};
  if (ptr) for (const v of M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + 640 * 400)) hist[v >>> 0] = (hist[v >>> 0] ?? 0) + 1;
  return { frames, running, speed: speedPct, status: status.textContent, colours: Object.keys(hist).length, hist,
           mounts: { fd: [...slots.fd], hd: slots.hd }, audio: audioStats && { ...audioStats, rate: audio?.sampleRate, state: audio?.state },
           sound: audio ? audio.state : "off", driveSounds: driveSoundsLoaded,
           speakerTransitions, regC: h ? api.regC(h).toString(8).padStart(3, "0") : null,
           joystick: joystick ? { on: joystick.enabled, bits: joystick.keyBits | joystick.touchBits } : null,
           fullscreen: fullscreenOn(), softkbd: softkbd ? softkbd.open : false, ruslat: h ? api.ruslat(h) : null,
           halted, embed: EMBED,
           start: startFile ? { title: startFile.meta.title, disks: [...startFile.disks.keys()] } : null,
           run: startFile?.run ? { program: startFile.run.program, over: startFile.run.over,
                                   about: $("abouttext").textContent, menu: startFile.run.menu.length,
                                   open: !!startFile.run.open } : null };
};
window.__ms.type = (text) => typing.type(text);
window.__ms.speed = (pct) => setSpeed(pct);   // the control, for scripted checks
window.__ms.api = () => api;                 // the module's calls, for scripted checks
window.__ms.sources = fileSources;           // the commander's disks, for scripted checks
window.__ms.module = () => M;
// The bug report without the download: the entries and the JSON, for scripted checks.
window.__ms.bugreport = async (note = "") => {
  const { report, entries, bytes } = await bugreport.build(bugDeps(), note);
  return { report, entries: entries.map((e) => ({ name: e.name, size: e.bytes.length })), zip: bytes.length,
           magic: String.fromCharCode(bytes[0], bytes[1]) };
};

window.addEventListener("error", (e) => say("error: " + e.message));
window.addEventListener("unhandledrejection", (e) => say("error: " + (e.reason?.message ?? e.reason)));
main().catch(fail);
