// smoke.mjs — the browser module under Node: boot the OSA disk, run three
// seconds, expect a picture.  The same oracle the native tests use (a
// blank / trapped screen is a few hundred non-background pixels; RT-11's
// date prompt on the ROM's boot screen is thousands).
//
//   node src/web/smoke.mjs <dist dir>
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const dist = process.argv[2] ?? "build/emscripten-release/web/dist";
const { default: createMs0515 } = await import(pathToFileURL(join(dist, "ms0515.js")).href);
const M = await createMs0515();
const api = {
  create:  M.cwrap("ms_create", "number", []),
  loadRom: M.cwrap("ms_load_rom", "number", ["number", "string"]),
  mount:   M.cwrap("ms_mount", "number", ["number", "number", "string"]),
  reset:   M.cwrap("ms_reset", null, ["number"]),
  frame:   M.cwrap("ms_frame", "number", ["number"]),
  render:  M.cwrap("ms_render", "number", ["number"]),
  keyTick: M.cwrap("ms_key_tick", null, ["number", "number"]),
  key:     M.cwrap("ms_key", null, ["number", "number", "number"]),
  audio:   M.cwrap("ms_audio", "number", ["number", "number", "number", "number"]),
  width:   M.cwrap("ms_width", "number", []),
  height:  M.cwrap("ms_height", "number", []),
  keyMax:  M.cwrap("ms_key_max", "number", []),
};

M.FS.writeFile("/rom.bin", readFileSync(join(dist, "rom/ms0515-roma.rom")));
M.FS.mkdirTree("/disks");
M.FS.writeFile("/disks/osa.dsk", readFileSync(join(dist, "disks/osa.dsk")));

const h = api.create();
if (!api.loadRom(h, "/rom.bin")) throw new Error("ROM load failed");
if (!api.mount(h, 0, "/disks/osa.dsk")) throw new Error("mount failed");
api.reset(h);

const pcm = M._malloc(4096 * 2);
let cycles = 0, samples = 0;
for (let i = 0; i < 150; ++i) {
  api.keyTick(h, i * 20);
  const c = api.frame(h);
  if (c === 0) throw new Error(`CPU halted at frame ${i}`);
  cycles += c;
  samples += api.audio(h, pcm, 4096, 44100);
  if (i === 100) { api.key(h, 51, 1); }            // Return: accept the date prompt
  if (i === 103) { api.key(h, 51, 0); }
}
const w = api.width(), hgt = api.height();
const ptr = api.render(h);
const px = M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + w * hgt);
const hist = new Map();
for (const v of px) hist.set(v, (hist.get(v) ?? 0) + 1);
const sorted = [...hist.entries()].sort((a, b) => b[1] - a[1]);
const background = sorted[0][1];
const foreground = w * hgt - background;
console.log(`frames 150, cycles ${cycles}, audio samples ${samples}, ${w}x${hgt}, ` +
            `${sorted.length} colours, foreground pixels ${foreground}, key max ${api.keyMax()}`);
if (foreground < 2000) throw new Error("the screen shows nothing: expected RT-11's boot text");
if (samples < 100) throw new Error("no audio samples");
console.log("smoke OK");
