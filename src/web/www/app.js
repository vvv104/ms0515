// app.js — the MS-0515 in the browser.
//
// The module (ms0515.js / .wasm, built from src/web) runs the machine; this
// file is the front-end: it fetches the ROM and a disk into the module's
// in-memory file system, mounts them, runs 50 frames a second, paints
// each frame on the canvas, queues its sound, turns key events into MS7004
// keys and keeps the disks the user changes in IndexedDB so the next visit
// finds them - nothing is ever written back to the host (it is a static
// site), the originals are one click away.
import createMs0515 from "./ms0515.js";
import { KEYS, KEY_ID, CODE_TO_KEY } from "./keys.js";

const DISKS = [
  { name: "osa.dsk",         title: "OSA — RT-11 with SABOT2 and FIST (R FIST)", ds: false },
  { name: "omega-games.dsk", title: "Omega — games",                             ds: true },
  { name: "omega-lang.dsk",  title: "Omega — languages",                         ds: true },
  { name: "mihin.dsk",       title: "Mihinsoft OS-16SJ",                         ds: true },
  { name: "rodionov.dsk",    title: "Rodionov (ROM A)",                           ds: false },
  { name: "vvv.dsk",         title: "vvv — RT-11 with HD.SYS",                   ds: true },
];
const ROMS = { a: "rom/ms0515-roma.rom", b: "rom/ms0515-romb.rom" };
const SS_SIZE = 409600;
const FRAME_MS = 20;
const AUDIO_RATE = 44100;

const $ = (id) => document.getElementById(id);
const canvas = $("screen");
const ctx = canvas.getContext("2d");
const status = $("status");

let M, h, api;
let image, frameBuf;
let running = false, lastTick = 0, acc = 0;
let audio = null, audioTime = 0;
let frames = 0;
const mounted = {};           // unit -> FS path

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
async function dbGet(key) {
  const d = await db();
  return new Promise((ok, no) => {
    const r = d.transaction(STORE).objectStore(STORE).get(key);
    r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error);
  });
}
async function dbPut(key, value) {
  const d = await db();
  return new Promise((ok, no) => {
    const r = d.transaction(STORE, "readwrite").objectStore(STORE).put(value, key);
    r.onsuccess = () => ok(); r.onerror = () => no(r.error);
  });
}
async function dbDel(key) {
  const d = await db();
  return new Promise((ok, no) => {
    const r = d.transaction(STORE, "readwrite").objectStore(STORE).delete(key);
    r.onsuccess = () => ok(); r.onerror = () => no(r.error);
  });
}

// ── the machine ────────────────────────────────────────────────────────────
async function boot() {
  const romSel = $("rom").value;
  const diskName = $("disk").value;
  say("loading…");
  stop();
  if (h) { flushDisks(); api.destroy(h); h = null; }
  for (const k of Object.keys(mounted)) delete mounted[k];

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
    const name = path.slice("/disks/".length);
    dbPut(name, M.FS.readFile(path)).catch(() => {});
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
  api.keyTick(h, now >>> 0);
  const cycles = api.frame(h);
  ++frames;
  if (cycles === 0) { say("CPU halted"); stop(); return; }
  if (audio) queueAudio();
  if ((frames & 63) === 0) flushDisks();
}

function paint() {
  const ptr = api.render(h);
  image.data.set(M.HEAPU8.subarray(ptr, ptr + image.data.length));
  ctx.putImageData(image, 0, 0);
}

// A peek for scripted checks (the CI's browser run): the frame count, the
// status line, the colours of the picture now.
window.__ms = () => {
  const ptr = h ? api.render(h) : 0;
  const hist = {};
  if (ptr) for (const v of M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + 640 * 400)) hist[v >>> 0] = (hist[v >>> 0] ?? 0) + 1;
  return { frames, running, status: status.textContent, colours: Object.keys(hist).length, hist };
};

// ── sound: each frame's PCM on the AudioContext's clock ────────────────────
function queueAudio() {
  const max = 4096;
  if (!frameBuf) frameBuf = M._malloc(max * 2);
  const n = api.audio(h, frameBuf, max, AUDIO_RATE);
  if (n <= 0) return;
  const pcm = M.HEAP16.subarray(frameBuf >> 1, (frameBuf >> 1) + n);
  const buf = audio.createBuffer(1, n, AUDIO_RATE);
  const ch = buf.getChannelData(0);
  for (let i = 0; i < n; ++i) ch[i] = pcm[i] / 32768;
  const src = audio.createBufferSource();
  src.buffer = buf;
  src.connect(audio.destination);
  const t = Math.max(audio.currentTime + 0.02, audioTime);
  src.start(t);
  audioTime = t + n / AUDIO_RATE;
}

function toggleSound() {
  if (audio) { audio.close(); audio = null; $("sound").textContent = "Sound: off"; return; }
  audio = new AudioContext({ sampleRate: AUDIO_RATE });
  audioTime = 0;
  $("sound").textContent = "Sound: on";
}

// ── keyboard ───────────────────────────────────────────────────────────────
function onKey(e, down) {
  if (!h) return;
  const name = CODE_TO_KEY[e.code];
  if (!name) return;
  e.preventDefault();
  api.key(h, KEY_ID[name], down ? 1 : 0);
}

// ── the page ───────────────────────────────────────────────────────────────
async function main() {
  M = await createMs0515();
  api = {
    create:  M.cwrap("ms_create", "number", []),
    destroy: M.cwrap("ms_destroy", null, ["number"]),
    reset:   M.cwrap("ms_reset", null, ["number"]),
    loadRom: M.cwrap("ms_load_rom", "number", ["number", "string"]),
    mount:   M.cwrap("ms_mount", "number", ["number", "number", "string"]),
    frame:   M.cwrap("ms_frame", "number", ["number"]),
    render:  M.cwrap("ms_render", "number", ["number"]),
    audio:   M.cwrap("ms_audio", "number", ["number", "number", "number", "number"]),
    key:     M.cwrap("ms_key", null, ["number", "number", "number"]),
    keyTick: M.cwrap("ms_key_tick", null, ["number", "number"]),
    keyMax:  M.cwrap("ms_key_max", "number", []),
    save:    M.cwrap("ms_save_state", "number", ["number", "string"]),
    load:    M.cwrap("ms_load_state", "number", ["number", "string"]),
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
  $("sound").onclick = toggleSound;
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
  canvas.addEventListener("keydown", (e) => onKey(e, true));
  canvas.addEventListener("keyup", (e) => onKey(e, false));
  canvas.addEventListener("blur", () => { if (h) api.key(h, 0, 0); });
  window.addEventListener("beforeunload", flushDisks);
  say("ready");
  if (q.get("autostart") !== "0") boot().catch((e) => say("error: " + e.message));
}

window.addEventListener("error", (e) => say("error: " + e.message));
window.addEventListener("unhandledrejection", (e) => say("error: " + (e.reason?.message ?? e.reason)));
main().catch((e) => say("error: " + e.message));
