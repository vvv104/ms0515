// start_check.mjs — www/start.js under Node, against the module: a start
// file made the way a tool makes one (the machine booted from the test
// disk, DIR typed, the moment kept), read back, and left in dist/ as
// test-start.zip for the browser check to open with ?start=.
//
//   node src/web/start_check.mjs <dist dir> [disk image]
import { readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import { KEY_ID, charToHostKey, mapKey } from "./www/keys.js";
import { parseStart, buildStart } from "./www/start.js";

const dist = process.argv[2] ?? "build/emscripten-release/web/dist";
const disk = process.argv[3] ?? join(dirname(fileURLToPath(import.meta.url)), "../lib/tests/disks/originals/test_osa_games.dsk");
const { default: createMs0515 } = await import(pathToFileURL(join(dist, "ms0515.js")).href);
const M = await createMs0515();
const api = {
  create:  M.cwrap("ms_create", "number", []),
  loadRom: M.cwrap("ms_load_rom", "number", ["number", "string"]),
  mount:   M.cwrap("ms_mount", "number", ["number", "number", "string"]),
  reset:   M.cwrap("ms_reset", null, ["number"]),
  frame:   M.cwrap("ms_frame", "number", ["number"]),
  render:  M.cwrap("ms_render", "number", ["number"]),
  key:     M.cwrap("ms_key", null, ["number", "number", "number"]),
  save:    M.cwrap("ms_save_state", "number", ["number", "string"]),
  load:    M.cwrap("ms_load_state", "number", ["number", "string"]),
  width:   M.cwrap("ms_width", "number", []),
  height:  M.cwrap("ms_height", "number", []),
};
const fail = (what) => { throw new Error(what); };
const BLACK = 0xff000000;
const CURSOR = 128;                   // pixels a blinking cursor cell accounts for

const picture = (h) => {
  const w = api.width(), hgt = api.height();
  const ptr = api.render(h);
  return Uint32Array.from(M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + w * hgt));
};
const WHITE = 0xffffffff;
const differ = (a, b) => { let n = 0; for (let i = 0; i < a.length; ++i) if (a[i] !== b[i]) ++n; return n; };
const count = (p, v) => { let n = 0; for (const x of p) if (x === v) ++n; return n; };
const histogram = (p) => { const hist = {}; for (const v of p) hist[v] = (hist[v] ?? 0) + 1; return hist; };
const mostly = (p) => +Object.entries(histogram(p)).sort((a, b) => b[1] - a[1])[0][0];

function run(h, frames) {
  for (let i = 0; i < frames; ++i) if (api.frame(h) === 0) fail("CPU halted");
}
// Until the picture holds still (a blinking cursor apart) for thirty frames
// and `ok` is happy with it; the frames it took.
function settle(h, what, ok = () => true, limit = 2000) {
  let prev = picture(h), still = 0;
  for (let f = 1; f <= limit; ++f) {
    run(h, 1);
    const p = picture(h);
    still = differ(prev, p) <= CURSOR ? still + 1 : 0;
    prev = p;
    if (still >= 30 && ok(p)) return f;
  }
  fail(`the screen never settled on ${what}`);
}
// Typing the page's way: the host key a character needs, the MS7004 key it
// maps to, three frames down, three up.
function type(h, text) {
  for (const ch of text) {
    const host = charToHostKey(ch) ?? fail(`no key for ${JSON.stringify(ch)}`);
    const { key, withShift } = mapKey(host.code, host.shift, false);
    if (withShift) api.key(h, KEY_ID.ShiftL, 1);
    api.key(h, KEY_ID[key], 1);
    run(h, 3);
    api.key(h, KEY_ID[key], 0);
    if (withShift) api.key(h, KEY_ID.ShiftL, 0);
    run(h, 3);
  }
}

M.FS.writeFile("/rom.bin", readFileSync(join(dist, "rom/ms0515-roma.rom")));
M.FS.mkdirTree("/disks");
const name = "test-start.dsk";
M.FS.writeFile("/disks/" + name, readFileSync(disk));
const h = api.create();
api.loadRom(h, "/rom.bin") || fail("ROM load failed");
api.mount(h, 0, "/disks/" + name) || fail("mount failed");
api.reset(h);
const booted = settle(h, "the boot", (p) => mostly(p) === BLACK);   // the ROM's grey test screen gone
type(h, "\r");                                                     // the date prompt
settle(h, "the monitor's prompt", (p) => count(p, WHITE) > 500);
const atPrompt = count(picture(h), WHITE);
type(h, "DIR\r");                                                  // something a boot alone does not show
// A still screen is not the listing yet: the directory is read at the
// drive's pace, and the screen holds still for longer than that first.
settle(h, "the listing", (p) => count(p, WHITE) > atPrompt * 2);
const before = picture(h);
api.save(h, "/start.state") || fail("save state failed");

const meta = {
  title: "the test disk after DIR", rom: "a",
  disks: { fd: [name, "", "", ""], hd: "" },
  sound: { speaker: true, drive: false, kbd: false }, speed: 100,
  made: { by: "start_check.mjs", command: "DIR" },
};
const zip = await buildStart({ meta, state: M.FS.readFile("/start.state"), disks: new Map([[name, M.FS.readFile("/disks/" + name)]]) });

// Read back: the parts, and the moment brought to a fresh machine - the
// images mounted by the file's word, as the page does it.
const start = await parseStart(zip);
start.meta.title === meta.title || fail("the title did not survive");
start.meta.disks.fd[0] === name || fail("drive A is not named");
start.disks.get(name)?.length === 409600 || fail("the image did not survive");
start.state.length > 100000 || fail(`the snapshot is ${start.state.length} bytes`);
const h2 = api.create();
api.loadRom(h2, "/rom.bin") || fail("ROM load failed (second machine)");
M.FS.writeFile("/start2.state", start.state);
M.FS.writeFile("/disks/again-" + name, start.disks.get(name));
api.load(h2, "/start2.state") || fail("the snapshot did not load");
api.mount(h2, 0, "/disks/again-" + name) || fail("mount after load failed");
const off = differ(before, picture(h2));
off <= CURSOR || fail(`the restored screen differs from the saved one in ${off} pixels`);
type(h2, "DIR\r");                    // and the machine goes on from there, disk and all
// The second listing scrolls the first: a different picture, still the monitor's.
settle(h2, "a listing after the restore", (p) => differ(before, p) > 4 * CURSOR);
const again = picture(h2);
mostly(again) === BLACK || fail("the restored machine's screen is not the monitor's");

writeFileSync(join(dist, "test-start.zip"), zip);
writeFileSync(join(dist, "test-start.json"), JSON.stringify({ hist: histogram(before), cursor: CURSOR }));
console.log(`start check OK: boot ${booted} frames, file ${zip.length} bytes, snapshot ${start.state.length}, `
            + `restored within ${off} pixels`);
