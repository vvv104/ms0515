// zip_check.mjs — www/zip.js under Node: the archive the page hands the
// browser (the commander's marked files, a bug report) is read back here -
// the central directory, every local header, each entry's CRC-32, and the
// deflated entries inflated again and compared byte for byte.
//
//   node src/web/zip_check.mjs
import { makeZip, makeZipDeflated, crc32 } from "./www/zip.js";

const u16 = (b, at) => b[at] | (b[at + 1] << 8);
const u32 = (b, at) => (b[at] | (b[at + 1] << 8) | (b[at + 2] << 16) | (b[at + 3] << 24)) >>> 0;
const dec = new TextDecoder();

function fail(what) { throw new Error(what); }
const eq = (got, want, what) => { if (got !== want) fail(`${what}: ${got}, expected ${want}`); };

async function inflate(bytes) {
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

// The archive read the way a reader reads it: the end record, then the
// central directory, then each entry at the offset it names.
async function unzip(zip) {
  const end = zip.length - 22;
  eq(u32(zip, end), 0x06054B50, "end record signature");
  const count = u16(zip, end + 10);
  let at = u32(zip, end + 16);
  const out = [];
  for (let i = 0; i < count; ++i) {
    eq(u32(zip, at), 0x02014B50, "central header signature");
    const method = u16(zip, at + 10);
    const crc = u32(zip, at + 16);
    const packedSize = u32(zip, at + 20), size = u32(zip, at + 24);
    const nameLen = u16(zip, at + 28);
    const name = dec.decode(zip.subarray(at + 46, at + 46 + nameLen));
    const local = u32(zip, at + 42);
    eq(u32(zip, local), 0x04034B50, `local header signature of ${name}`);
    eq(u16(zip, local + 8), method, `local method of ${name}`);
    eq(u32(zip, local + 14), crc, `local CRC of ${name}`);
    eq(u32(zip, local + 18), packedSize, `local packed size of ${name}`);
    eq(u32(zip, local + 22), size, `local size of ${name}`);
    eq(dec.decode(zip.subarray(local + 30, local + 30 + u16(zip, local + 26))), name, "local name");
    const data = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
    const packed = zip.subarray(data, data + packedSize);
    const bytes = method === 8 ? await inflate(packed) : packed;
    eq(bytes.length, size, `size of ${name}`);
    eq(crc32(bytes), crc, `CRC of ${name}`);
    out.push({ name, bytes, method, packedSize });
    at += 46 + nameLen + u16(zip, at + 30) + u16(zip, at + 32);
  }
  eq(at, u32(zip, end + 16) + u32(zip, end + 12), "central directory size");
  return out;
}

const text = new TextEncoder().encode("MS-0515 bug report\n");
const image = new Uint8Array(409600);                    // a floppy: mostly empty, as they are
for (let i = 0; i < image.length; i += 512) image[i] = i & 0xFF;
const tiny = new Uint8Array([1, 2, 3]);

// Stored: what the commander has always written.
{
  const files = await unzip(makeZip([{ name: "A.MAC", bytes: text }, { name: "B.SAV", bytes: image }]));
  eq(files.length, 2, "entries");
  eq(files[0].name, "A.MAC", "first name");
  eq(files[0].method, 0, "stored method");
  eq(files[1].packedSize, image.length, "a stored entry keeps its size");
  if (dec.decode(files[0].bytes) !== dec.decode(text)) fail("the stored bytes came back changed");
}

// Deflated: the same archive, read the same way, and the disk image is a
// fraction of its length.
{
  const zip = await makeZipDeflated([
    { name: "report.json", bytes: text },
    { name: "disks/osa.dsk", bytes: image },
    { name: "small", bytes: tiny },
  ]);
  const files = await unzip(zip);
  eq(files.length, 3, "entries");
  eq(files[1].method, 8, "the image is deflated");
  eq(files[2].method, 0, "a few bytes are not worth deflating");
  if (!(files[1].packedSize < image.length / 10)) fail(`the image deflated to ${files[1].packedSize} of ${image.length}`);
  for (const [i, want] of [text, image, tiny].entries())
    if (Buffer.compare(Buffer.from(files[i].bytes), Buffer.from(want)) !== 0) fail(`entry ${i} came back changed`);
  if (!(zip.length < image.length / 5)) fail(`the archive is ${zip.length} bytes`);
}

// An archive of nothing is still an archive.
eq((await unzip(await makeZipDeflated([]))).length, 0, "an empty archive");

console.log("zip check OK");
