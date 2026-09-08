// bugreport.js — everything a hang needs to be reproduced, in one file.
//
// A machine that stops (HALT) or stops answering is nothing to look at
// from the outside: what matters is where the CPU was and what it had in
// memory.  The button packs that into a .zip the user saves and sends:
//
//   report.json     the version, the browser, the mounts, what the page
//                   knew about the machine at that moment, and the note
//   state.ms0515    the snapshot (ms_save_state): CPU, memory, video RAM,
//                   the timer, the FDC, the keyboard - the emulator opens
//                   it with File / Load State
//   rom.bin         the ROM as loaded (the snapshot refuses to load
//                   against another one - it carries its CRC)
//   screen.png      the picture as it was
//   disks/<name>    every mounted image as the guest has left it
//
// The snapshot names the images by their path in the module's file system
// ("/disks/osa.dsk"): report.json carries the same paths, so the files can
// be put back where the snapshot looks for them.
import { makeZipDeflated, crc32 } from "./zip.js?v=@STAMP@";

const SCHEMA = 1;
const STATE_PATH = "/bugreport.state";

const pad = (n) => String(n).padStart(2, "0");

export function reportName(d = new Date()) {
  return `ms0515-bug-${d.getFullYear()}${pad(d.getMonth() + 1)}${pad(d.getDate())}`
       + `-${pad(d.getHours())}${pad(d.getMinutes())}${pad(d.getSeconds())}.zip`;
}

async function screenPng(canvas) {
  if (!canvas?.toBlob) return null;
  const blob = await new Promise((ok) => canvas.toBlob(ok, "image/png"));
  return blob ? new Uint8Array(await blob.arrayBuffer()) : null;
}

function readFile(M, path) {
  try {
    return M.FS.analyzePath(path).exists ? M.FS.readFile(path) : null;
  } catch {
    return null;
  }
}

// The snapshot of the machine now, through the module's file system.
function snapshot(deps) {
  const { api, handle, module: M } = deps;
  if (!handle) return null;
  if (!api.save(handle, STATE_PATH)) return null;
  const bytes = readFile(M, STATE_PATH);
  try { M.FS.unlink(STATE_PATH); } catch { /* it was never written */ }
  return bytes;
}

// deps: { api, handle, module, canvas, mounts, pathOf, rom, machine }
// - mounts() gives { fd: [name x 4], hd: name } (empty strings for empty
//   slots), machine() what the page knows about the run.
export async function collect(deps, note = "") {
  const M = deps.module;
  const entries = [], files = { disks: [] };

  const state = snapshot(deps);
  if (state) { entries.push({ name: "state.ms0515", bytes: state }); files.state = "state.ms0515"; }

  const rom = readFile(M, "/rom.bin");
  if (rom) { entries.push({ name: "rom.bin", bytes: rom }); files.rom = "rom.bin"; }

  const png = await screenPng(deps.canvas);
  if (png) { entries.push({ name: "screen.png", bytes: png }); files.screen = "screen.png"; }

  const mounts = deps.mounts();
  const seen = new Map();                       // name -> the entry's record
  const image = (name) => {
    if (!name) return null;
    if (seen.has(name)) return seen.get(name);
    const path = deps.pathOf(name);
    const bytes = readFile(M, path);
    const rec = { name, path, size: bytes ? bytes.length : 0, crc32: bytes ? crc32(bytes) : null,
                  file: bytes ? "disks/" + name : null };
    if (bytes) { entries.push({ name: rec.file, bytes }); files.disks.push(rec.file); }
    seen.set(name, rec);
    return rec;
  };

  const report = {
    kind: "ms0515-bug-report",
    schema: SCHEMA,
    saved: new Date().toISOString(),
    note,
    emulator: { version: deps.api.version(), rom: deps.rom() },
    page: {
      url: location.href,
      userAgent: navigator.userAgent,
      platform: navigator.platform,
      language: navigator.language,
      screen: `${screen.width}x${screen.height}`,
      pixelRatio: devicePixelRatio,
      cores: navigator.hardwareConcurrency ?? null,
      memoryGb: navigator.deviceMemory ?? null,
    },
    machine: deps.machine(),
    mounts: {
      fd: mounts.fd.map((name, unit) => (name ? { unit, ...image(name) } : null)),
      hd: mounts.hd ? image(mounts.hd) : null,
    },
    files,
  };
  entries.unshift({ name: "report.json", bytes: new TextEncoder().encode(JSON.stringify(report, null, 2) + "\n") });
  return { report, entries };
}

// The report as a .zip: what the button saves, and what a "send" would
// post one day.
export async function build(deps, note = "") {
  const { report, entries } = await collect(deps, note);
  const bytes = await makeZipDeflated(entries);
  return { name: reportName(), bytes, report, entries };
}

export async function save(deps, note = "") {
  const { name, bytes, report } = await build(deps, note);
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bytes], { type: "application/zip" }));
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  return { name, size: bytes.length, report };
}
