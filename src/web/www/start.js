// start.js — a start file: the machine at a chosen moment, to be opened
// and played from there.
//
// One .zip, plain enough to be made on the host and read back here:
//
//   start.json      what it is (title), the ROM it runs on ("a" / "b"),
//                   the images by drive, the settings the page applies
//                   (which sounds, the joystick, the speed), and how it
//                   was made
//   state.ms0515    the snapshot (ms_save_state) of that moment
//   disks/<name>    the images the drives held - the guest may write them
//                   (a game keeps its scores), so they travel with the file
//   screen.png      the picture at that moment (a preview; not read)
//
// A snapshot names its images by the paths they had when it was taken,
// which are the host's when a tool made it; so the page mounts the images
// itself, by the drives start.json names, after the state is loaded.
import { makeZipDeflated, readZip } from "./zip.js?v=@STAMP@";

export const SCHEMA = 1;
const META = "start.json", STATE = "state.ms0515", SCREEN = "screen.png", DISKS = "disks/";
const enc = new TextEncoder(), dec = new TextDecoder();

// The settings the page applies, with what a game start wants by default:
// the speaker on (the game's own sounds), the drive and the keyboard quiet.
export const DEFAULT_SOUND = { speaker: true, drive: false, kbd: false };

// The file's parts in an object: { meta, state, disks: Map(name -> bytes),
// screen }.  Throws with a reason when it is not a start file.
export async function parseStart(bytes) {
  const files = new Map((await readZip(bytes)).map((f) => [f.name, f.bytes]));
  if (!files.has(META)) throw new Error(`no ${META}: not a start file`);
  let meta;
  try { meta = JSON.parse(dec.decode(files.get(META))); } catch (e) { throw new Error(`${META}: ${e.message}`); }
  if (meta.schema !== SCHEMA) throw new Error(`${META}: schema ${meta.schema}, this page reads ${SCHEMA}`);
  if (!files.has(STATE)) throw new Error(`no ${STATE}: the start file carries no snapshot`);
  if (!["a", "b"].includes(meta.rom)) throw new Error(`${META}: rom must be "a" or "b"`);
  const fd = Array.isArray(meta.disks?.fd) ? meta.disks.fd : [];
  const hd = meta.disks?.hd ?? "";
  const disks = new Map();
  for (const name of [...fd, hd].filter(Boolean)) {
    if (!files.has(DISKS + name)) throw new Error(`${META} mounts ${name}, which is not in the file`);
    disks.set(name, files.get(DISKS + name));
  }
  return {
    meta: { ...meta, disks: { fd: [0, 1, 2, 3].map((u) => fd[u] ?? ""), hd },
            sound: { ...DEFAULT_SOUND, ...(meta.sound ?? {}) }, joystick: !!meta.joystick,
            speed: meta.speed ?? 100 },
    state: files.get(STATE), disks, screen: files.get(SCREEN) ?? null,
  };
}

// The file from its parts: meta as parseStart gives it (disks.fd by unit,
// disks.hd), the snapshot's bytes, the images by name, the picture if any.
export async function buildStart({ meta, state, disks, screen = null }) {
  const named = new Set([...meta.disks.fd, meta.disks.hd].filter(Boolean));
  for (const name of named) if (!disks.has(name)) throw new Error(`${name} is mounted but not given`);
  const entries = [
    { name: META, bytes: enc.encode(JSON.stringify({ schema: SCHEMA, ...meta }, null, 2) + "\n") },
    { name: STATE, bytes: state },
  ];
  for (const name of named) entries.push({ name: DISKS + name, bytes: disks.get(name) });
  if (screen) entries.push({ name: SCREEN, bytes: screen });
  return makeZipDeflated(entries);
}
