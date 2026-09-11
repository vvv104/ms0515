// wizard_check.mjs — the browser's disk wizard under Node: the module's
// wiz_* API over a small collection made of the OSA test fixture, from the
// rows to a built disk that boots in the same module.
//
//   node src/web/wizard_check.mjs <dist dir>
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ?? "build/emscripten-release/web/dist";
const { default: createMs0515 } = await import(pathToFileURL(join(dist, "ms0515.js")).href);
const M = await createMs0515();
const c = (name, ret, args) => M.cwrap(name, ret, args);
const api = {
  open: c("wiz_open", "number", ["string", "string", "string"]),
  error: c("wiz_error", "string", []),
  state: c("wiz_state", "string", []),
  setMedia: c("wiz_set_media", "string", ["string"]),
  setSystem: c("wiz_set_system", "string", ["string"]),
  fold: c("wiz_fold", null, ["string"]),
  setField: c("wiz_set_field", "string", ["string", "string"]),
  toggle: c("wiz_toggle", "string", ["string"]),
  needed: c("wiz_needed", "string", []),
  plan: c("wiz_plan", "string", []),
  build: c("wiz_build", "number", ["string"]),
  save: c("wiz_save", "string", []),
  load: c("wiz_load", "number", ["string"]),
  details: c("wiz_details", "string", ["string"]),
  diskGet: c("ms_disk_get", "number", ["string", "number", "number", "string"]),
  diskData: c("ms_disk_data", "number", []),
  create: c("ms_create", "number", []),
  loadRom: c("ms_load_rom", "number", ["number", "string"]),
  mount: c("ms_mount", "number", ["number", "number", "string"]),
  reset: c("ms_reset", null, ["number"]),
  frame: c("ms_frame", "number", ["number"]),
  render: c("ms_render", "number", ["number"]),
  keyTick: c("ms_key_tick", null, ["number", "number"]),
  width: c("ms_width", "number", []),
  height: c("ms_height", "number", []),
};
const fail = (what) => { throw new Error(what); };

// The collection: the fixture as the exemplar, its handlers and DIR loose.
const fixture = readFileSync(join(here, "../lib/tests/disks/test_osa.dsk"));
M.FS.mkdirTree("/software/systems");
M.FS.writeFile("/software/systems/test_osa.dsk", fixture);
const files = { "systems/test_osa.dsk": fixture.length };
for (const [dir, name] of [["h", "DZ.SYS"], ["h", "TT.SYS"], ["u", "DIR.SAV"], ["u2", "DIR.SAV"]]) {
  const n = api.diskGet("/software/systems/test_osa.dsk", 0, 0, name);
  if (n < 0) fail(`the fixture has no ${name}`);
  M.FS.mkdirTree(`/software/${dir}`);
  M.FS.writeFile(`/software/${dir}/${name}`, M.HEAPU8.slice(api.diskData(), api.diskData() + n));
  files[`${dir}/${name}`] = n;
}
const manifest = `
format  = 1
version = "check-1"

[system.osa]
title    = "OSA"
image    = "systems/test_osa.dsk"
media    = ["ss", "dz"]
requires = ["dz", "tt"]
startup  = ["SET TT QUIET"]

[bundle.dz]
title = "DZ.SYS"
group = "System"
files = ["h/DZ.SYS"]

[bundle.tt]
title = "TT.SYS"
group = "System"
files = ["h/TT.SYS"]

[bundle.dir]
title    = "DIR (first)"
group    = "Utilities"
provides = ["dir"]
files    = ["u/DIR.SAV"]

[bundle.dir2]
title    = "DIR (second)"
group    = "Utilities"
provides = ["dir"]
files    = ["u2/DIR.SAV"]
`;
if (!api.open(manifest, Object.keys(files).join("\n"), Object.values(files).join("\n"))) fail("wiz_open: " + api.error());

let state = JSON.parse(api.state());
const row = (key, kind = "bundle") => state.rows.find((r) => r.key === key && r.kind === kind);
// The steps: the diskette, then the system on it, then the groups - folded.
if (state.version !== "check-1" || state.ready || state.system !== "" || state.media !== "") fail("the state: " + JSON.stringify(state).slice(0, 200));
if (!row("#diskette", "group").open || !row("ss", "media")) fail("the diskette is not the first step");
if (row("#system", "group").available) fail("the system is open before the diskette");
if (api.setSystem("osa") !== "choose the diskette first") fail("a system chosen before the diskette");
if (api.setMedia("dv") !== "") fail("wiz_set_media");
state = JSON.parse(api.state());
if (row("osa", "system").available || row("osa", "system").why !== "only on ss, dz") fail("OSA offered on dv: " + JSON.stringify(row("osa", "system")));
if (api.setMedia("ss") !== "" || api.setSystem("osa") !== "") fail("the first two steps");
state = JSON.parse(api.state());
if (!state.ready || state.system !== "osa" || state.media !== "ss") fail("the state: " + JSON.stringify(state).slice(0, 200));
if (row("#system", "group").summary !== "OSA" || row("dz")) fail("the steps did not fold");
api.fold("System");
api.fold("Utilities");
state = JSON.parse(api.state());
if (row("dz").mark !== "system") fail("DZ.SYS is not the system's");
if (!state.rows.some((r) => r.kind === "radio" && r.key === "dir")) fail("no radio group for dir");
if (!(row("dir").radio && row("dir2").radio)) fail("the DIRs are not radio buttons");
if (row("dz").blocks < 1) fail("no block count");

if (api.toggle("dir") !== "") fail("toggle dir");
if (api.toggle("dir2") !== "") fail("toggle dir2");
state = JSON.parse(api.state());
if (row("Utilities", "group").summary !== "1 chosen") fail("the group's count: " + row("Utilities", "group").summary);
if (row("dir").mark !== "off" || row("dir2").mark !== "on") fail("the radio did not switch: " + row("dir").mark + " " + row("dir2").mark);
if (api.toggle("dz") === "") fail("the system's part was taken away");

const needed = JSON.parse(api.needed());
for (const p of ["systems/test_osa.dsk", "h/DZ.SYS", "h/TT.SYS", "u2/DIR.SAV"])
  if (!needed.includes(p)) fail("wiz_needed misses " + p);
if (needed.includes("u/DIR.SAV")) fail("wiz_needed names the DIR not chosen");

const plan = JSON.parse(api.plan());
if (!plan.ok || plan.volumes.length !== 1 || plan.volumes[0].name !== "DZ0:") fail("the plan: " + JSON.stringify(plan));
if (plan.startup[0] !== "SET TT QUIET") fail("the startup: " + plan.startup);
if (!JSON.parse(api.details("dir2")).files.includes("DIR.SAV")) fail("the details");

// The label and START.COM: fields in the list, START.COM at the end of System.
if (api.setField("#volume-id", "check") !== "") fail("wiz_set_field volume id");
if (api.setField("#volume-id-2", "two") === "") fail("a second side's label on a one-sided disk");
if (api.setField("#startup:0", "DIR") !== "") fail("wiz_set_field startup");
state = JSON.parse(api.state());
if (row("#volume-id", "field")?.value !== "CHECK") fail("the volume id field: " + JSON.stringify(row("#volume-id", "field")));
if (row("SET TT QUIET", "line")?.requiredBy !== "OSA") fail("the system's START.COM line");
if (row("#startup:0", "field")?.value !== "DIR" || !row("#startup:1", "field")) fail("the START.COM fields");
if (!JSON.parse(api.plan()).startup.includes("DIR")) fail("the plan's START.COM misses the typed line");
api.setField("#startup:0", "");

const saved = api.save();
if (!/collection\s*=\s*"check-1"/.test(saved) || !/"dir2"/.test(saved)) fail("the saved choice:\n" + saved);
api.toggle("dir2");
if (!api.load(saved)) fail("wiz_load: " + api.error());
api.fold("Utilities");                                  // a loaded choice starts folded
state = JSON.parse(api.state());
if (row("dir2").mark !== "on") fail("the loaded choice");

// The disk, and the machine booting it.
if (!api.build("/composed.dsk")) fail("wiz_build: " + api.error());
if (M.FS.stat("/composed.dsk").size !== 409600) fail("the image is not a 400 KB side");
M.FS.writeFile("/rom.bin", readFileSync(join(dist, "rom/ms0515-roma.rom")));
const h = api.create();
if (!api.loadRom(h, "/rom.bin") || !api.mount(h, 0, "/composed.dsk")) fail("mount");
api.reset(h);
for (let i = 0; i < 400; ++i) { api.keyTick(h, i * 20); if (api.frame(h) === 0) fail("halted at frame " + i); }
const w = api.width(), hgt = api.height(), ptr = api.render(h);
const px = M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + w * hgt);
const counts = new Map();
for (const v of px) counts.set(v, (counts.get(v) ?? 0) + 1);
const foreground = w * hgt - Math.max(...counts.values());
console.log(`wizard: ${state.rows.length} rows, plan ${plan.volumes[0].used}/${plan.volumes[0].capacity}, booted foreground ${foreground}`);
// A quiet boot (SET TT QUIET, no date asked) leaves a banner and a prompt -
// some 1800 pixels; a blank or trapped screen is a few hundred.
if (foreground < 1200) fail("the composed disk does not boot to RT-11's text");
console.log("wizard check OK");
