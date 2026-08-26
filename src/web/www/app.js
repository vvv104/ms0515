// app.js — the MS-0515 in the browser.
//
// The module (ms0515.js / .wasm, built from src/web) runs the machine; this
// file is the front-end: it fetches the ROM and the disk images into the
// module's in-memory file system, mounts them the way the desktop
// front-ends do (two floppy drives of two sides each, a paravirtual hard
// disk), runs 50 frames a second, paints each frame on the canvas, hands
// its sound to an AudioWorklet, turns key events into MS7004 keys the way
// the SDL front-end does, and keeps the images the guest writes to in
// IndexedDB so the next visit finds them - nothing is ever written back to
// the host (it is a static site), the originals are one click away.
// "@STAMP@" is the build time (stamp.cmake fills it in dist/): a browser
// never pairs a cached module with a newer page.
import createMs0515 from "./ms0515.js?v=@STAMP@";
import { KEYS, KEY_ID, mapKey, isLetterKey, charToHostKey } from "./keys.js?v=@STAMP@";
import { Joystick } from "./joystick.js?v=@STAMP@";
import { SoftKeyboard, isTouchDevice } from "./softkeys.js?v=@STAMP@";
import { Commander } from "./fm.js?v=@STAMP@";

// The floppy images the site ships (dist/disks/, from assets/disks) and
// their sides (a two-sided image takes both sides of its drive).
const DISKS = [
  { name: "osa.dsk",         sides: 1, title: "[OSA] Games" },
  { name: "omega-games.dsk", sides: 1, title: "[OMEGA] Games" },
  { name: "omega-lang.dsk",  sides: 1, title: "[OMEGA] Development" },
  { name: "mihin.dsk",       sides: 1, title: "[MIHIN] Tools" },
  { name: "rodionov.dsk",    sides: 2, title: "[RODIONOV] Programs" },
  { name: "vvv.dsk",         sides: 1, title: "[VVV] Empty OS with HD.SYS" },
];
const SHIPPED = new Map(DISKS.map((d) => [d.name, d.title]));

// What to do first on each shipped disk, for the status line after a boot.
const HINTS = {
  "osa.dsk":         ". is the OS prompt: DIR lists the files, R FIST runs the game",
  "omega-games.dsk": "the date as dd-mm-yy (22-08-92), Enter at the start file; then DIR lists the files, R NAME runs a .SAV",
  "omega-lang.dsk":  ". is the OS prompt: DIR lists the files; PAS1, MACRO, LINK, FORTRA, BASICO compile, KED edits",
  "mihin.dsk":       "Enter twice (the silent date and time prompts); then DIR lists the files",
  "rodionov.dsk":    "Enter at the date prompt, then ROSA Commander: the arrows move, Enter runs a file",
  "vvv.dsk":         "Enter twice (the silent date and time prompts); DIR lists the files; INIT HD: makes a mounted HD image a volume",
};
const sidesLabel = (n) => n === 2 ? "two-sided" : "one-sided";
const ROMS = { a: "rom/ms0515-roma.rom", b: "rom/ms0515-romb.rom" };
const SS_SIZE = 409600, DS_SIZE = 2 * SS_SIZE;
const FRAME_MS = 20;

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
let frames = 0, speakerTransitions = 0;   // the speaker's level changes, summed over the frames
let joystick = null;                       // the MS7007-port joystick (joystick.js)
let softkbd = null;                        // the OS's on-screen keyboard (softkeys.js)
let commander = null;                      // the files of the mounted images (fm.js)
const K = (name) => KEY_ID[name];

function say(s) { status.textContent = s; }
const fail = (e) => say("error: " + (e?.message ?? e));
const hint = (s) => say("hint: " + s);

async function fetchBytes(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

// ── persistence ────────────────────────────────────────────────────────────
// IndexedDB holds image bytes by name: the user's own images, and the
// shipped images the guest has written to (a copy; "Revert" drops it).
// localStorage holds the small things: the user's image list (name ->
// size) and the mounts.
const DB = "ms0515", STORE = "disks";
function db() {
  return new Promise((ok, no) => {
    const r = indexedDB.open(DB, 1);
    r.onupgradeneeded = () => r.result.createObjectStore(STORE);
    r.onsuccess = () => ok(r.result);
    r.onerror = () => no(r.error);
  });
}
function dbRequest(mode, op) {
  return db().then((d) => new Promise((ok, no) => {
    const r = op(d.transaction(STORE, mode).objectStore(STORE));
    r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error);
  }));
}
const dbGet = (key) => dbRequest("readonly", (s) => s.get(key));
const dbPut = (key, value) => dbRequest("readwrite", (s) => s.put(value, key));
const dbDel = (key) => dbRequest("readwrite", (s) => s.delete(key));

const own = new Map(Object.entries(JSON.parse(localStorage.getItem("ms0515.images") ?? "{}")));
const saveOwn = () => localStorage.setItem("ms0515.images", JSON.stringify(Object.fromEntries(own)));

// ── the mounts ─────────────────────────────────────────────────────────────
const slots = { fd: ["", "", "", ""], hd: "" };   // image names, "" = empty
const ds = [false, false];                         // the drive holds a double-sided image
const staged = new Map();                          // FS path -> mtime at the last flush
const pathOf = (name) => "/disks/" + name;

function saveMounts() {
  localStorage.setItem("ms0515.mounts", JSON.stringify({ rom: $("rom").value, fd: slots.fd, hd: slots.hd }));
}
function loadMounts() {
  const q = new URLSearchParams(location.search);
  let m = { rom: "a", fd: ["osa.dsk", "", "", ""], hd: "" };
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
  return fetchBytes("disks/" + name);
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
  if (name) row.append(...imageButtons(name));
  return row;
}

// ── the commander: the files of the mounted images, in place of the screen ─
// Its sources are what the drives hold - each side of a floppy image, the
// HD image; a write goes around the FDC: the image is unmounted, changed
// in the module's file system, mounted again (the guest sees a changed
// disk at its next directory read).
function fileSources() {
  const out = [];
  for (let unit = 0; unit < 4; ++unit) {
    const drive = driveOf(unit), side = sideOf(unit);
    const name = side === 1 && ds[drive] ? slots.fd[unitOf(drive, 0)] : slots.fd[unit];
    if (!name) continue;
    out.push({ id: `fd${unit}`, label: `${"AB"[drive]}:${side} ${name}`, path: pathOf(name), side, linear: false, name, unit });
  }
  if (slots.hd) out.push({ id: "hd", label: `HD ${slots.hd}`, path: pathOf(slots.hd), side: 0, linear: true, name: slots.hd });
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
  if (open) commander.open(); else canvas.focus();
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
  await addOwn(name, new Uint8Array(size));
  await mountFd(unit, name);
  hint(`${name} is in drive ${"AB"[drive]}: INIT DZ${unit}: in the guest makes it a volume` + (sides === 2 ? ` (side 1 is DZ${unit + 2}:, its own)` : ""));
}

function newFloppyRow(drive) {
  const make = el("div", "row");
  make.append(el("span", "side", "new"));
  const sides = document.createElement("select");
  for (const n of [1, 2]) { const o = document.createElement("option"); o.value = n; o.textContent = sidesLabel(n); sides.appendChild(o); }
  make.append(sides);
  make.append(button("Create blank", () => newFloppy(drive, +sides.value), "a zero-filled image; the guest initialises it (INIT DZn:)"));
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
    hint(`${name} is the HD now: Boot, then INIT HD: in the guest makes it a volume`);
  }, "a zero-filled image; the guest initialises it"));
  const hint = el("div", "hint", "RT-11 installs HD.SYS at boot: mount, then Boot (vvv.dsk has the handler)");
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
  say("loading…");
  stop();
  keyboard.reset();
  const rom = $("rom").value;
  M.FS.writeFile("/rom.bin", await fetchBytes(ROMS[rom]));
  if (!api.loadRom(h, "/rom.bin")) throw new Error("ROM load failed");
  api.reset(h);
  saveMounts();
  const disk = slots.fd[unitOf(0, 0)];
  if (!disk) hint("nothing in drive A: open its panel, pick an image, Boot again");
  else hint(HINTS[disk] ?? "the machine boots from drive A side 0");
  canvas.focus();
  start();
}

function start() {
  if (running) return;
  running = true;
  lastTick = performance.now();
  acc = 0;
  requestAnimationFrame(loop);
}

function stop() { running = false; }

function loop(now) {
  if (!running) return;
  acc += Math.min(now - lastTick, 200);
  lastTick = now;
  let n = 0;
  while (acc >= FRAME_MS && n < 4) {
    acc -= FRAME_MS;
    step(now);
    ++n;
  }
  if (n) { paint(); lamps(); }
  requestAnimationFrame(loop);
}

function step(now) {
  typing.tick(now);
  api.keyTick(h, now >>> 0);
  const cycles = api.frame(h);
  ++frames;
  speakerTransitions += api.transitions(h);
  if (cycles === 0) { say("CPU halted"); stop(); return; }
  if (speaker) queueAudio();
  if ((frames & 63) === 0) flushDisks();
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

async function toggleSound() {
  if (audio) {
    speaker = null;
    await audio.close();
    audio = null;
    $("sound").textContent = "Sound: off";
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
  if (audio.state !== "running") await audio.resume().catch(() => {});
  $("sound").textContent = "Sound: on";
}

// ── keyboard: the SDL front-end's PhysicalKeyboard, host codes in ──────────
// A host key maps by character (mapKey) to an MS7004 key plus the Shift it
// needs there; the difference with the host's Shift is made up with a
// synthetic Shift that is undone at release; CAPS + Shift inverts a
// letter's case; the numpad / * + and a few РУС-mode symbols are handled
// as special cases, as in PhysicalKeyboard.cpp.
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
    transitions: c("ms_transitions", "number", ["number"]),
    regC:    c("ms_reg_c", "number", ["number"]),
    key:     c("ms_key", null, ["number", "number", "number"]),
    keyTick: c("ms_key_tick", null, ["number", "number"]),
    keyMax:  c("ms_key_max", "number", []),
    keyHeld: c("ms_key_held", "number", ["number", "number"]),
    ruslat:  c("ms_ruslat", "number", ["number"]),
    caps:    c("ms_caps", "number", ["number"]),
    releaseAll: c("ms_key_release_all", null, ["number"]),
    joystick: c("ms_joystick", null, ["number", "number"]),
    diskDir:    c("ms_disk_dir", "string", ["string", "number", "number"]),
    diskError:  c("ms_disk_error", "string", []),
    diskGet:    c("ms_disk_get", "number", ["string", "number", "number", "string"]),
    diskData:   c("ms_disk_data", "number", []),
    diskPut:    c("ms_disk_put", "number", ["string", "number", "number", "string", "number", "number", "number", "number", "number", "number"]),
    diskRm:     c("ms_disk_rm", "number", ["string", "number", "number", "string"]),
    diskRename: c("ms_disk_rename", "number", ["string", "number", "number", "string", "string"]),
    diskInit:   c("ms_disk_init", "number", ["string", "number", "number"]),
    diskSqueeze: c("ms_disk_squeeze", "number", ["string", "number", "number"]),
    save:    c("ms_save_state", "number", ["number", "string"]),
    load:    c("ms_load_state", "number", ["number", "string"]),
  };
  if (api.keyMax() !== KEYS.length - 1)
    throw new Error(`key table drift: module ${api.keyMax()}, page ${KEYS.length - 1}`);
}

// The drives' panels: one open at a time; the ROM and the buttons.
function bindControls() {
  const panels = [...document.querySelectorAll("details.dev")];
  for (const d of panels)
    d.addEventListener("toggle", () => { if (d.open) for (const o of panels) if (o !== d) o.open = false; });
  document.addEventListener("click", (e) => { if (!e.target.closest("details.dev")) for (const o of panels) o.open = false; });
  $("rom").onchange = saveMounts;
  // A click on a toolbar button must not keep the focus: the keys are the
  // machine's (the keyboard button is the exception: it hands the focus to
  // the hidden field the OS keyboard types into).
  for (const b of document.querySelectorAll("header > button"))
    if (b.id !== "softkbd" && b.id !== "files") b.addEventListener("click", () => canvas.focus());
  // Full screen: the button, F11; hidden where the API is not there (an iPhone).
  $("fullscreen").hidden = !(document.fullscreenEnabled || document.webkitFullscreenEnabled);
  $("fullscreen").onclick = toggleFullscreen;
  document.addEventListener("keydown", (e) => { if (e.key === "F11") { e.preventDefault(); toggleFullscreen(); } });
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
  $("joystick").onclick = () => {
    joystick.enable(!joystick.enabled);
    $("joystick").textContent = joystick.enabled ? "Joystick: on" : "Joystick: off";
  };
  $("boot").onclick = () => boot().catch(fail);
  $("sound").onclick = () => toggleSound().catch(fail);
  $("save").onclick = () => { if (h) say(api.save(h, "/state.bin") ? "state saved: Restore brings the machine back to it" : "save failed"); };
  $("restore").onclick = () => { if (h) say(api.load(h, "/state.bin") ? "state restored" : "no saved state"); };
  $("wipe").onclick = () => wipe().catch(fail);
  canvas.addEventListener("keydown", (e) => onKey(e, true));
  canvas.addEventListener("keyup", (e) => onKey(e, false));
  canvas.addEventListener("blur", () => { if (h) { api.releaseAll(h); keyboard.reset(); } });
  window.addEventListener("beforeunload", flushDisks);
}

async function main() {
  fit();
  window.addEventListener("resize", fit);
  M = await createMs0515({ locateFile: (f) => f + "?v=@STAMP@" });
  bindApi();
  image = ctx.createImageData(canvas.width, canvas.height);
  h = api.create();

  const m = loadMounts();
  $("rom").value = m.rom;
  bindControls();
  renderDevices();
  for (let unit = 0; unit < 4; ++unit)
    if (m.fd[unit]) await mountFd(unit, m.fd[unit]).catch(fail);
  if (m.hd) await mountHd(m.hd).catch(fail);

  say("ready");
  const q = new URLSearchParams(location.search);
  if (q.get("autostart") !== "0") {
    boot().then(() => {
      // `type=`: a command for the monitor, after the boot (delay= ms, 3000)
      if (q.get("type")) typing.type("\r" + q.get("type") + "\r", +(q.get("delay") ?? 3000));
    }).catch(fail);
  }
}

// A peek for scripted checks (the CI's browser run): the frame count, the
// status line, the colours of the picture now; `type` drives the typing.
window.__ms = () => {
  const ptr = h ? api.render(h) : 0;
  const hist = {};
  if (ptr) for (const v of M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + 640 * 400)) hist[v >>> 0] = (hist[v >>> 0] ?? 0) + 1;
  return { frames, running, status: status.textContent, colours: Object.keys(hist).length, hist,
           mounts: { fd: [...slots.fd], hd: slots.hd }, audio: audioStats && { ...audioStats, rate: audio?.sampleRate, state: audio?.state },
           speakerTransitions, regC: h ? api.regC(h).toString(8).padStart(3, "0") : null,
           joystick: joystick ? { on: joystick.enabled, bits: joystick.keyBits | joystick.touchBits } : null,
           fullscreen: fullscreenOn(), softkbd: softkbd ? softkbd.open : false, ruslat: h ? api.ruslat(h) : null };
};
window.__ms.type = (text) => typing.type(text);
window.__ms.api = () => api;                 // the module's calls, for scripted checks
window.__ms.module = () => M;

window.addEventListener("error", (e) => say("error: " + e.message));
window.addEventListener("unhandledrejection", (e) => say("error: " + (e.reason?.message ?? e.reason)));
main().catch(fail);
