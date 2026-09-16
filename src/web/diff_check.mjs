// diff_check.mjs — www/diff.js under Node: what the compare view (Alt+F3 in
// the commander) is built on.  The chunks must cover both files exactly, in
// order; equal parts must be found again where they have moved to another
// line or another offset, which is the whole reason the two sides can be put
// beside each other at all.
//
//   node src/web/diff_check.mjs
import { lineChunks, byteChunks, alignedRuns, sameFraction, within } from "./www/diff.js";

let checks = 0;
function fail(what) { throw new Error(what); }
const eq = (got, want, what) => { ++checks; if (got !== want) fail(`${what}: ${got}, expected ${want}`); };
const ok = (cond, what) => { ++checks; if (!cond) fail(what); };

// Every chunk list must account for every unit of both files, in order.
function covers(chunks, a, b, what) {
  let ai = 0, bi = 0;
  for (const c of chunks) {
    eq(c.aStart, ai, `${what}: a runs on`);
    eq(c.bStart, bi, `${what}: b runs on`);
    ok(c.aLen >= 0 && c.bLen >= 0, `${what}: a chunk of negative length`);
    if (c.kind === "same") {
      eq(c.aLen, c.bLen, `${what}: an equal chunk of unequal length`);
      for (let k = 0; k < c.aLen; ++k)
        if (a[c.aStart + k] !== b[c.bStart + k]) fail(`${what}: "same" at ${c.aStart + k} is not`);
    }
    ai += c.aLen; bi += c.bLen;
  }
  eq(ai, a.length, `${what}: a covered`);
  eq(bi, b.length, `${what}: b covered`);
  for (let k = 1; k < chunks.length; ++k)
    if (chunks[k].kind === chunks[k - 1].kind) fail(`${what}: two ${chunks[k].kind} chunks in a row`);
}

const lines = (s) => s.split("\n");
const bytes = (...parts) => {
  const all = [];
  for (const p of parts) {
    if (typeof p === "string") for (const ch of p) all.push(ch.charCodeAt(0));
    else if (typeof p === "number") all.push(p);
    else all.push(...p);
  }
  return Uint8Array.from(all);
};
const kinds = (chunks) => chunks.map((c) => c.kind).join(",");

// ── text ───────────────────────────────────────────────────────────────────

{                                                   // the same file: one chunk
  const a = lines("START:\t.PRINT\t#MSG\n\t.EXIT\nMSG:\t.ASCIZ\t/hello/");
  const c = lineChunks(a, a.slice());
  covers(c, a, a, "identical text");
  eq(kinds(c), "same", "identical text is one equal chunk");
  eq(sameFraction(c, a.length, a.length), 1, "identical text is wholly common");
}

{                                                   // one line changed in the middle
  const a = lines("one\ntwo\nthree\nfour");
  const b = lines("one\nTWO\nthree\nfour");
  const c = lineChunks(a, b);
  covers(c, a, b, "one line changed");
  eq(kinds(c), "same,change,same", "a change between two equal runs");
  eq(c[1].aLen, 1, "one line on the left");
  eq(c[1].bLen, 1, "one line on the right");
  eq(c[0].aLen, 1, "one line of common head");
}

{                                                   // a line inserted: the rest must line up again
  const a = lines("a\nb\nc\nd\ne\nf");
  const b = lines("a\nb\nNEW\nc\nd\ne\nf");
  const c = lineChunks(a, b);
  covers(c, a, b, "a line inserted");
  eq(kinds(c), "same,change,same", "an insertion is a chunk of its own");
  eq(c[1].aLen, 0, "nothing on the left of an insertion");
  eq(c[1].bLen, 1, "the new line on the right");
  eq(c[2].aLen, 4, "the four lines after it are found again");
}

{                                                   // a block moved far down still matches
  const head = Array.from({ length: 40 }, (_, i) => `line ${i}`);
  const a = [...head, "TAIL A"];
  const b = ["NEW FIRST", ...head, "TAIL A"];
  const c = lineChunks(a, b);
  covers(c, a, b, "a line prepended");
  eq(kinds(c), "change,same", "everything after the new line is common");
  eq(c[1].aLen, 41, "all 41 lines found again at an offset of one");
}

{                                                   // nothing in common
  const a = lines("alpha\nbeta");
  const b = lines("gamma\ndelta\nepsilon");
  const c = lineChunks(a, b);
  covers(c, a, b, "nothing in common");
  eq(kinds(c), "change", "one change covering both");
  eq(sameFraction(c, a.length, b.length), 0, "nothing common");
}

{                                                   // an empty side
  const a = lines("only");
  const c = lineChunks(a, []);
  covers(c, a, [], "against an empty file");
  eq(kinds(c), "change", "an empty side leaves one change");
}

{                                                   // repeated lines are no anchors, and must still line up
  const a = lines("x\nx\nx\nkeep\nx\nx");
  const b = lines("x\nx\nkeep\nx\nx");
  const c = lineChunks(a, b);
  covers(c, a, b, "repeated lines");
  ok(c.some((k) => k.kind === "same" && k.aLen >= 2), "the repeated lines still match somewhere");
}

// ── bytes ──────────────────────────────────────────────────────────────────

{                                                   // the same bytes: one chunk
  const a = bytes("the quick brown fox jumps over the lazy dog");
  const c = byteChunks(a, a.slice());
  covers(c, a, a, "identical bytes");
  eq(kinds(c), "same", "identical bytes are one equal chunk");
}

{                                                   // one byte changed in the middle of a long file
  const a = bytes("0123456789abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJ");
  const b = a.slice();
  b[30] = 0x21;
  const c = byteChunks(a, b);
  covers(c, a, b, "one byte changed");
  eq(kinds(c), "same,change,same", "a change between two equal runs");
  ok(c[1].aLen <= 2 && c[1].bLen <= 2, `the change is the one byte, not ${c[1].aLen}`);
}

{                                                   // bytes inserted: everything after must be found at its new offset
  const tail = bytes("abcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmnop");
  const a = bytes("HEADER----------", tail);
  const b = bytes("HEADER----------", "INSERTED-BYTES--", tail);
  const c = byteChunks(a, b);
  covers(c, a, b, "bytes inserted");
  const moved = c.find((k) => k.kind === "same" && k.aStart !== k.bStart);
  ok(moved && moved.aLen >= tail.length - 1, "the tail is found again at its new offset");
  ok(sameFraction(c, a.length, b.length) > 0.7, "most of it is common");
}

{                                                   // a file of zeros is no anchor: no false matches claimed
  const a = new Uint8Array(200);
  const b = new Uint8Array(200);
  b.set(bytes("XYZ"), 100);
  const c = byteChunks(a, b);
  covers(c, a, b, "zeros");
}

{                                                   // .SAV block 0: the words a linker fills in, one differing
  const a = new Uint8Array(1024), b = new Uint8Array(1024);
  const put = (buf, at, w) => { buf[at] = w & 0xFF; buf[at + 1] = w >> 8; };
  for (const buf of [a, b]) {
    put(buf, 0o40, 0o1000); put(buf, 0o42, 0o1000); put(buf, 0o360, 0o300);
    buf.set(bytes("Hello, world!", 0), 0o1010);
  }
  put(a, 0o50, 0o1024); put(b, 0o50, 0o1026);
  const c = byteChunks(a, b);
  covers(c, a, b, "two .SAV images");
  ok(sameFraction(c, a.length, b.length) > 0.9, "two .SAV images differing in one word are nearly all common");
}

{                                                   // an empty side
  const a = bytes("something");
  const c = byteChunks(a, new Uint8Array(0));
  covers(c, a, new Uint8Array(0), "bytes against nothing");
  eq(kinds(c), "change", "an empty side leaves one change");
}

// ── the runs the view is laid out in ───────────────────────

{                                                   // a substitution keeps the two sides in step
  const a = ["a", "b", "c"];
  const b = ["a", "B", "c"];
  const runs = alignedRuns(lineChunks(a, b));
  eq(runs.length, 1, "a substitution is one run");
  eq(runs[0].kind, "pair", "the sides stay beside each other");
  eq(runs[0].len, 3, "over the whole file");
}

{                                                   // an insertion breaks the step, once
  const a = ["a", "b", "c"];
  const b = ["a", "NEW", "b", "c"];
  const runs = alignedRuns(lineChunks(a, b));
  eq(runs.map((r) => r.kind).join(","), "pair,gap,pair", "a gap between two paired runs");
  eq(runs[1].aLen, 0, "nothing on the left of the gap");
  eq(runs[1].bLen, 1, "the new line on the right");
  eq(runs[2].len, 2, "the rest is paired again");
}

{                                                   // the runs must cover both files
  const a = ["one", "two", "three", "four", "five"];
  const b = ["one", "two prime", "three", "EXTRA", "four", "five"];
  let ai = 0, bi = 0;
  for (const r of alignedRuns(lineChunks(a, b))) {
    eq(r.aStart, ai, "a run starts where the last ended (left)");
    eq(r.bStart, bi, "a run starts where the last ended (right)");
    ai += r.kind === "pair" ? r.len : r.aLen;
    bi += r.kind === "pair" ? r.len : r.bLen;
  }
  eq(ai, a.length, "the runs cover the left file");
  eq(bi, b.length, "the runs cover the right file");
}

// ── within a line ──────────────────────────────────────────────────────────

{
  const w = within("MOV #1010,R0", "MOV #1012,R0");
  eq(w.head, 8, "the common start of two lines");        // "MOV #101"
  eq(w.aEnd, 9, "where the left line stops differing");  // the one digit
  eq(w.bEnd, 9, "where the right line stops differing");
}

{
  const w = within("same", "same");
  eq(w.head, 4, "two equal lines differ nowhere");
  ok(w.aEnd <= w.head, "nothing to mark");
}

{
  const w = within("abc", "abcdef");
  eq(w.head, 3, "the shorter line is a prefix of the longer");
  eq(w.bEnd, 6, "the tail of the longer is what differs");
}

console.log(`diff check OK (${checks} checks)`);
