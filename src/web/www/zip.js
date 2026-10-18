// zip.js — a .zip of a few files: what the commander hands the browser
// when several marked files are downloaded at once, and what a bug report
// is packed into.  Local headers, a central directory, the end record;
// CRC-32 per entry; names in UTF-8 (RT-11 names are ASCII anyway).
// Entries are stored as they are (makeZip) or deflated where the browser
// has a CompressionStream (makeZipDeflated): a disk image is mostly empty
// blocks and shrinks to a fraction of its megabytes.
const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; ++n) {
    let c = n;
    for (let k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

export function crc32(bytes) {
  let c = 0xFFFFFFFF;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

// The DOS date / time of "now", for the entries.
function dosStamp() {
  const d = new Date();
  const time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
  const date = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
  return { time, date };
}

// entries: [{ name, bytes }] -> a Uint8Array of the archive, stored.
export function makeZip(entries) {
  return build(entries.map(({ name, bytes }) => ({ name, bytes, packed: bytes, method: 0 })));
}

// The same, deflated where the browser can do it (and where deflating
// wins): the entries keep their names, a reader sees a plain .zip.
export async function makeZipDeflated(entries) {
  const out = [];
  for (const { name, bytes } of entries) {
    let packed = bytes, method = 0;
    if (typeof CompressionStream === "function" && bytes.length > 512) {
      try {
        const stream = new Blob([bytes]).stream().pipeThrough(new CompressionStream("deflate-raw"));
        const deflated = new Uint8Array(await new Response(stream).arrayBuffer());
        if (deflated.length < bytes.length) { packed = deflated; method = 8; }
      } catch { /* no CompressionStream for this format: stored, then */ }
    }
    out.push({ name, bytes, packed, method });
  }
  return build(out);
}

// entries: [{ name, bytes, packed, method }] - `bytes` gives the CRC and
// the uncompressed size, `packed` is what goes into the file.
function build(entries) {
  const enc = new TextEncoder();
  const { time, date } = dosStamp();
  const parts = [], central = [];
  let offset = 0;
  const u16 = (v) => [v & 0xFF, (v >> 8) & 0xFF];
  const u32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
  for (const { name, bytes, packed, method } of entries) {
    const n = enc.encode(name), crc = crc32(bytes);
    const local = Uint8Array.from([
      0x50, 0x4B, 0x03, 0x04, ...u16(20), ...u16(0x0800), ...u16(method), ...u16(time), ...u16(date),
      ...u32(crc), ...u32(packed.length), ...u32(bytes.length), ...u16(n.length), ...u16(0), ...n,
    ]);
    central.push(Uint8Array.from([
      0x50, 0x4B, 0x01, 0x02, ...u16(20), ...u16(20), ...u16(0x0800), ...u16(method), ...u16(time), ...u16(date),
      ...u32(crc), ...u32(packed.length), ...u32(bytes.length), ...u16(n.length), ...u16(0), ...u16(0),
      ...u16(0), ...u16(0), ...u32(0), ...u32(offset), ...n,
    ]));
    parts.push(local, packed);
    offset += local.length + packed.length;
  }
  const centralSize = central.reduce((a, c) => a + c.length, 0);
  const end = Uint8Array.from([
    0x50, 0x4B, 0x05, 0x06, ...u16(0), ...u16(0), ...u16(entries.length), ...u16(entries.length),
    ...u32(centralSize), ...u32(offset), ...u16(0),
  ]);
  const total = offset + centralSize + end.length;
  const out = new Uint8Array(total);
  let at = 0;
  for (const p of [...parts, ...central, end]) { out.set(p, at); at += p.length; }
  return out;
}
