// run_check.mjs — one program run in the browser module, under Node: what
// ms0515-run does on the host (src/tools/run), through the module's calls.
// The program's folder is a directory of the module's file system; DIR
// lists it and ends, and its text is on the picture.  Then a run card is
// left in dist/ for the browser check to open with ?run=.
//
//   node src/web/run_check.mjs <dist dir>
import { mkdirSync, readFileSync, writeFileSync, copyFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const dist = process.argv[2] ?? "build/emscripten-release/web/dist";
const system = join(here, "../../rt11_devel/toolset/system");
const { default: createMs0515 } = await import(pathToFileURL(join(dist, "ms0515.js")).href);
const M = await createMs0515();
const api = {
  create:   M.cwrap("ms_create", "number", []),
  frame:    M.cwrap("ms_frame", "number", ["number"]),
  render:   M.cwrap("ms_render", "number", ["number"]),
  audio:    M.cwrap("ms_audio", "number", ["number", "number", "number", "number"]),
  runBuilt: M.cwrap("ms_run_built", "number", []),
  run:      M.cwrap("ms_run", "number", ["number", "string", "string", "number"]),
  runError: M.cwrap("ms_run_error", "string", []),
  runEnded: M.cwrap("ms_run_ended", "number", ["number"]),
};
const fail = (why) => { console.error("FAIL: " + why); process.exit(1); };

if (!api.runBuilt()) fail("the module was built without the run machine (no software collection at build time)");

M.FS.mkdirTree("/run");
M.FS.writeFile("/run/DIR.SAV", readFileSync(join(system, "DIR.SAV")));
M.FS.writeFile("/run/NOTE.TXT", new Uint8Array(700).fill(0x4e));

// The picture's lit pixels: a blank screen is none, a directory listing
// a thousand and more.
const lit = (h) => {
  const ptr = api.render(h);
  const px = M.HEAPU32.subarray(ptr >> 2, (ptr >> 2) + 640 * 400);
  const hist = new Map();
  for (const v of px) hist.set(v, (hist.get(v) ?? 0) + 1);
  return 640 * 400 - Math.max(...hist.values());
};

const h = api.create();
if (api.run(h, "/nowhere/GAME.SAV", "", 0)) fail("a program that is not there was started");
if (!api.runError().includes("no such program")) fail(`the refusal says "${api.runError()}"`);

// DIR with a file named: the listing, then the monitor back - the end.
const runDir = () => {
  if (!api.run(h, "/run/DIR.SAV", "NOTE.TXT", 0)) fail("DIR does not start: " + api.runError());
  if (api.runEnded(h)) fail("ended before a frame was run");
  let frames = 0;
  while (!api.runEnded(h) && frames < 3000) { api.frame(h); ++frames; }
  if (api.runEnded(h) !== 1) fail(`DIR: ended = ${api.runEnded(h)} after ${frames} frames (1 is a good end)`);
  return frames;
};
const first = runDir();
const pixels = lit(h);
if (pixels < 500) fail(`DIR printed nothing to speak of: ${pixels} lit pixels`);
// Again on the same handle: every run starts from the carried state.
const second = runDir();
if (lit(h) !== pixels) fail(`the second run's picture differs: ${lit(h)} lit pixels against ${pixels}`);

// The sound path is the page's own after a run as before it.
const pcm = M._malloc(4096 * 2);
if (api.audio(h, pcm, 4096, 44100) <= 0) fail("no sound samples after a run");

// What the browser check opens: a card beside its program.
mkdirSync(join(dist, "test-run"), { recursive: true });
copyFileSync(join(system, "DIR.SAV"), join(dist, "test-run/DIR.SAV"));
writeFileSync(join(dist, "test-run/NOTE.TXT"), new Uint8Array(700).fill(0x4e));
writeFileSync(join(dist, "test-run/dir.json"), JSON.stringify({
  schema: 1,
  title: { en: "DIR", ru: "DIR" },
  program: "DIR.SAV",
  arguments: "NOTE.TXT",
  files: ["DIR.SAV", "NOTE.TXT"],
  text: { en: "The directory of the folder.", ru: "Каталог папки." },
  sources: { files: ["NOTE.TXT"], type: "\r", accept: ".txt",
             load: { en: "Load", ru: "Загрузить" }, own: { en: "Your own file", ru: "Свой файл" } },
}, null, 1));

console.log(`OK: DIR ran and ended in ${first} frames (${second} the second time), ${pixels} lit pixels; test-run/dir.json written`);
