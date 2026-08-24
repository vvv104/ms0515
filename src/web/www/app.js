// app.js — the MS-0515 in the browser.
//
// The module (ms0515.js / .wasm, built from src/web) runs the machine; this
// file is the front-end: it fetches the ROM and a disk into the module's
// in-memory file system, mounts them, runs 50 frames a second, paints
// each frame on the canvas, hands its sound to an AudioWorklet, turns key
// events into MS7004 keys the way the SDL front-end does, and keeps the
// disks the user changes in IndexedDB so the next visit finds them -
// nothing is ever written back to the host (it is a static site), the
// originals are one click away.
import createMs0515 from "./ms0515.js";
import { KEYS, KEY_ID, mapKey, isLetterKey, charToHostKey } from "./keys.js";

const DISKS = [
  { name: "osa.dsk",         title: "OSA — RT-11 with SABOT2 and FIST (R FIST)" },
  { name: "omega-games.dsk", title: "Omega — games" },
  { name: "omega-lang.dsk",  title: "Omega — languages" },
  { name: "mihin.dsk",       title: "Mihinsoft OS-16SJ" },
  { name: "rodionov.dsk",    title: "Rodionov (ROM A)" },
  { name: "vvv.dsk",         title: "vvv — RT-11 with HD.SYS" },
];
const ROMS = { a: "rom/ms0515-roma.rom", b: "rom/ms0515-romb.rom" };
const SS_SIZE = 409600;
const FRAME_MS = 20;

const $ = (id) => document.getElementById(id);
const canvas = $("screen");
const ctx = canvas.getContext("2d");
const status = $("status");

let M, h, api;
let image, pcmBuf;
let running = false, lastTick = 0, acc = 0;
let audio = null, speaker = null;
let frames = 0;
const mounted = {};           // unit -> FS path
const K = (name) => KEY_ID[name];

function say(s) { status.textContent = s; }

async function fetchBytes(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${url}: ${r.status}`);
  return new Uint8Array(await r.arrayBuffer());
}

// ── persistence: changed disks live in IndexedDB ───────────────────────────
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

// ── the machine ────────────────────────────────────────────────────────────
async function boot() {
  const romSel = $("rom").value;
  const diskName = $("disk").value;
  say("loading…");
  stop();
  if (h) { flushDisks(); api.destroy(h); h = null; }
  for (const k of Object.keys(mounted)) delete mounted[k];
  keyboard.reset();

  const rom = await fetchBytes(ROMS[romSel]);
  M.FS.writeFile("/rom.bin", rom);
  const local = await dbGet(diskName);
  const disk = local || await fetchBytes("disks/" + diskName);
  M.FS.mkdirTree("/disks");
  M.FS.writeFile("/disks/" + diskName, disk);

  h = api.create();
  if (!api.loadRom(h, "/rom.bin")) throw new Error("ROM load failed");
  const units = disk.length > SS_SIZE ? [0, 1] : [0];
  for (const u of units) {
    if (!api.mount(h, u, "/disks/" + diskName)) throw new Error("mount failed");
    mounted[u] = "/disks/" + diskName;
  }
  api.reset(h);
  say(`${diskName}${local ? " (your copy)" : ""} · ROM ${romSel.toUpperCase()} · Enter accepts the date prompts`);
  canvas.focus();
  start();
}

function flushDisks() {
  // The module's file is the live image: keep the user's copy.
  const done = new Set();
  for (const u of Object.keys(mounted)) {
    const path = mounted[u];
    if (done.has(path)) continue;
    done.add(path);
    dbPut(path.slice("/disks/".length), M.FS.readFile(path)).catch(() => {});
  }
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
  if (n) paint();
  requestAnimationFrame(loop);
}

function step(now) {
  typing.tick(now);
  api.keyTick(h, now >>> 0);
  const cycles = api.frame(h);
  ++frames;
  if (cycles === 0) { say("CPU halted"); stop(); return; }
  if (speaker) queueAudio();
  if ((frames & 63) === 0) flushDisks();
}

function paint() {
  const ptr = api.render(h);
  image.data.set(M.HEAPU8.subarray(ptr, ptr + image.data.length));
  ctx.putImageData(image, 0, 0);
}

// The screen fills what the header and the footer leave, at 8:5, whole
// pixels when it can (a multiple of 320 x 200 keeps the picture crisp).
function fit() {
  const box = canvas.parentElement;
  const w = box.clientWidth - 8, hgt = box.clientHeight - 8;
  let width = Math.min(w, hgt * 1.6);
  if (width >= 640) width = Math.floor(width / 320) * 320;
  canvas.style.width = width + "px";
  canvas.style.height = width / 1.6 + "px";
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
  audio = new AudioContext();
  await audio.audioWorklet.addModule("audio-worklet.js");
  speaker = new AudioWorkletNode(audio, "ms0515-speaker");
  speaker.connect(audio.destination);
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
  tick(now) {
    if (!h || now < this.next) return;
    if (this.pending) {                    // release the key pressed last time
      keyboard.up(this.pending.code);
      if (this.pending.shift) keyboard.up("ShiftLeft");
      this.pending = null;
      this.next = now + 60;
      return;
    }
    const k = this.queue.shift();
    if (!k) return;
    if (k.shift) keyboard.down("ShiftLeft");
    keyboard.down(k.code, k.shift);
    this.pending = k;
    this.next = now + 60;
  },
};

// ── the page ───────────────────────────────────────────────────────────────
async function main() {
  fit();
  window.addEventListener("resize", fit);
  M = await createMs0515();
  const c = (name, ret, args) => M.cwrap(name, ret, args);
  api = {
    create:  c("ms_create", "number", []),
    destroy: c("ms_destroy", null, ["number"]),
    reset:   c("ms_reset", null, ["number"]),
    loadRom: c("ms_load_rom", "number", ["number", "string"]),
    mount:   c("ms_mount", "number", ["number", "number", "string"]),
    frame:   c("ms_frame", "number", ["number"]),
    render:  c("ms_render", "number", ["number"]),
    audio:   c("ms_audio", "number", ["number", "number", "number", "number"]),
    key:     c("ms_key", null, ["number", "number", "number"]),
    keyTick: c("ms_key_tick", null, ["number", "number"]),
    keyMax:  c("ms_key_max", "number", []),
    keyHeld: c("ms_key_held", "number", ["number", "number"]),
    ruslat:  c("ms_ruslat", "number", ["number"]),
    caps:    c("ms_caps", "number", ["number"]),
    releaseAll: c("ms_key_release_all", null, ["number"]),
    save:    c("ms_save_state", "number", ["number", "string"]),
    load:    c("ms_load_state", "number", ["number", "string"]),
  };
  if (api.keyMax() !== KEYS.length - 1)
    throw new Error(`key table drift: module ${api.keyMax()}, page ${KEYS.length - 1}`);
  image = ctx.createImageData(canvas.width, canvas.height);

  const sel = $("disk");
  for (const d of DISKS) {
    const o = document.createElement("option");
    o.value = d.name; o.textContent = d.title;
    sel.appendChild(o);
  }
  const q = new URLSearchParams(location.search);
  if (q.get("disk")) sel.value = q.get("disk");
  if (q.get("rom")) $("rom").value = q.get("rom");

  $("boot").onclick = () => boot().catch((e) => say("error: " + e.message));
  $("sound").onclick = () => toggleSound().catch((e) => say("error: " + e.message));
  $("save").onclick = () => { if (h) say(api.save(h, "/state.bin") ? "state saved" : "save failed"); };
  $("restore").onclick = () => { if (h) say(api.load(h, "/state.bin") ? "state restored" : "no saved state"); };
  $("download").onclick = () => {
    const name = sel.value, path = "/disks/" + name;
    if (!h || !M.FS.analyzePath(path).exists) return;
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([M.FS.readFile(path)]));
    a.download = name; a.click();
  };
  $("original").onclick = async () => { await dbDel(sel.value); say("the original will be used at the next boot"); };
  $("file").onchange = async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    const bytes = new Uint8Array(await f.arrayBuffer());
    if (bytes.length !== SS_SIZE && bytes.length !== 2 * SS_SIZE) { say("not a 400 / 800 KB image"); return; }
    await dbPut(f.name, bytes);
    const o = document.createElement("option");
    o.value = f.name; o.textContent = f.name + " (yours)";
    sel.appendChild(o); sel.value = f.name;
    boot().catch((err) => say("error: " + err.message));
  };
  $("typebtn").onclick = () => { typing.type($("typebox").value + "\r"); $("typebox").value = ""; canvas.focus(); };
  $("typebox").addEventListener("keydown", (e) => { if (e.key === "Enter") $("typebtn").onclick(); });
  canvas.addEventListener("keydown", (e) => onKey(e, true));
  canvas.addEventListener("keyup", (e) => onKey(e, false));
  canvas.addEventListener("blur", () => { if (h) { api.releaseAll(h); keyboard.reset(); } });
  window.addEventListener("beforeunload", flushDisks);
  say("ready");
  if (q.get("autostart") !== "0") {
    boot().then(() => {
      // `type=`: a command for the monitor, after the boot (delay= ms, 3000)
      if (q.get("type")) typing.type("\r" + q.get("type") + "\r", +(q.get("delay") ?? 3000));
    }).catch((e) => say("error: " + e.message));
  }
}

// A peek for scripted checks (the CI's browser run): the frame count, the
// status line, the colours of the picture now; `type` drives the typing.
window.__ms = () => {
  const ptr = h ? api.render(h) : 0;
  const hist = {};
  if (ptr) for (const v of M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + 640 * 400)) hist[v >>> 0] = (hist[v >>> 0] ?? 0) + 1;
  return { frames, running, status: status.textContent, colours: Object.keys(hist).length, hist };
};
window.__ms.type = (text) => typing.type(text);

window.addEventListener("error", (e) => say("error: " + e.message));
window.addEventListener("unhandledrejection", (e) => say("error: " + (e.reason?.message ?? e.reason)));
main().catch((e) => say("error: " + e.message));
