// diff.js — what two files have in common, and where they part.
//
// Two ways of looking, because the files are of two kinds.  A text is
// compared line by line: the lines that occur once in each are the ones a
// reader recognises, so those are the anchors (patience diff), and what lies
// between two anchors is compared the same way again.  Bytes have no lines,
// so a run of sixteen equal bytes is the anchor instead, found by hash -
// which finds a run again where it has moved to another offset, and that is
// the point: a file with one word inserted near its start is otherwise
// wholly different from the insertion on.
//
// Both return the same thing: the files cut into chunks,
//
//   { kind: "same" | "change", aStart, aLen, bStart, bLen }
//
// in order, covering both files entirely, where "same" means equal unit for
// unit (aLen === bLen there).  The view collapses a long "same" chunk into
// one line saying how much it skipped.

// Chunks, built in order; two touching chunks of one kind are one chunk.
class Chunks {
  constructor() { this.out = []; }

  push(kind, aStart, aLen, bStart, bLen) {
    if (!aLen && !bLen) return;
    const last = this.out[this.out.length - 1];
    if (last && last.kind === kind) {
      last.aLen += aLen;
      last.bLen += bLen;
      return;
    }
    this.out.push({ kind, aStart, aLen, bStart, bLen });
  }

  get result() { return this.out; }
}

function suffix(a, b, aStart, aEnd, bStart, bEnd) {
  let n = 0;
  const max = Math.min(aEnd - aStart, bEnd - bStart);
  while (n < max && a[aEnd - 1 - n] === b[bEnd - 1 - n]) ++n;
  return n;
}

// ── text: the lines occurring once in both are the anchors ─────────────────

// The longest increasing subsequence of seq, as indices into it.
function longestIncreasing(seq) {
  const tails = [], back = new Array(seq.length).fill(-1), at = [];
  for (let i = 0; i < seq.length; ++i) {
    let lo = 0, hi = tails.length;
    while (lo < hi) {                              // the first tail not below seq[i]
      const mid = (lo + hi) >> 1;
      if (tails[mid] < seq[i]) lo = mid + 1; else hi = mid;
    }
    if (lo) back[i] = at[lo - 1];
    at[lo] = i;
    tails[lo] = seq[i];
  }
  const out = [];
  for (let i = tails.length ? at[tails.length - 1] : -1; i >= 0; i = back[i]) out.push(i);
  return out.reverse();
}

// The lines occurring exactly once in each range, paired and put in an order
// both files agree on.
function anchorPairs(a, b, aStart, aEnd, bStart, bEnd) {
  const once = (arr, start, end) => {
    const m = new Map();
    for (let i = start; i < end; ++i) m.set(arr[i], m.has(arr[i]) ? -1 : i);
    return m;
  };
  const inA = once(a, aStart, aEnd), inB = once(b, bStart, bEnd);
  const pairs = [];
  for (const [line, i] of inA) {
    if (i < 0) continue;
    const j = inB.get(line);
    if (j !== undefined && j >= 0) pairs.push([i, j]);
  }
  pairs.sort((x, y) => x[0] - y[0]);
  return longestIncreasing(pairs.map((p) => p[1])).map((k) => pairs[k]);
}

function textRange(a, b, aStart, aEnd, bStart, bEnd, out, depth) {
  let p = 0;
  while (aStart + p < aEnd && bStart + p < bEnd && a[aStart + p] === b[bStart + p]) ++p;
  if (p) { out.push("same", aStart, p, bStart, p); aStart += p; bStart += p; }
  const s = suffix(a, b, aStart, aEnd, bStart, bEnd);
  aEnd -= s; bEnd -= s;

  const anchors = aStart < aEnd && bStart < bEnd && depth < 32
    ? anchorPairs(a, b, aStart, aEnd, bStart, bEnd) : [];
  if (!anchors.length) {
    out.push("change", aStart, aEnd - aStart, bStart, bEnd - bStart);
  } else {
    let ai = aStart, bi = bStart;
    for (const [i, j] of anchors) {
      textRange(a, b, ai, i, bi, j, out, depth + 1);
      out.push("same", i, 1, j, 1);
      ai = i + 1; bi = j + 1;
    }
    textRange(a, b, ai, aEnd, bi, bEnd, out, depth + 1);
  }
  if (s) out.push("same", aEnd, s, bEnd, s);
}

// Two arrays of lines, compared.
export function lineChunks(a, b) {
  const out = new Chunks();
  textRange(a, b, 0, a.length, 0, b.length, out, 0);
  return out.result;
}

// ── bytes: an equal run is the anchor, wherever it has moved to ────────────

const WINDOW = 16;                     // the shortest run worth anchoring on
const BUCKET = 48;                     // offsets kept per hash: enough to choose from

function hashAt(buf, i, w) {
  let h = 2166136261;
  for (let k = 0; k < w; ++k) { h ^= buf[i + k]; h = Math.imul(h, 16777619); }
  return h >>> 0;
}

// Where every window of a sits, by its hash.  A window of one byte repeated
// is no anchor - a file of zeros would match everywhere and say nothing -
// and a hash seen too often is dropped for the same reason.
function windows(a, w) {
  const map = new Map();
  for (let i = 0; i + w <= a.length; ++i) {
    let flat = true;
    for (let k = 1; k < w; ++k) if (a[i + k] !== a[i]) { flat = false; break; }
    if (flat) continue;
    const h = hashAt(a, i, w);
    const list = map.get(h);
    if (list === undefined) map.set(h, [i]);
    else if (list.length < BUCKET) list.push(i);
  }
  return map;
}

function equalAt(a, i, b, j, n) {
  for (let k = 0; k < n; ++k) if (a[i + k] !== b[j + k]) return false;
  return true;
}

// Two byte strings, compared.
export function byteChunks(a, b, w = WINDOW) {
  const out = new Chunks();
  const map = windows(a, w);
  let ai = 0, bi = 0, i = 0;
  while (i + w <= b.length) {
    let best = null;
    for (const j of map.get(hashAt(b, i, w)) ?? []) {
      if (j < ai || !equalAt(a, j, b, i, w)) continue;
      let back = 0;                                  // how far back the run reaches
      while (j - back > ai && i - back > bi && a[j - back - 1] === b[i - back - 1]) ++back;
      let len = w;
      while (j + len < a.length && i + len < b.length && a[j + len] === b[i + len]) ++len;
      len += back;
      if (!best || len > best.len) best = { j: j - back, i: i - back, len };
    }
    if (!best) { ++i; continue; }
    out.push("change", ai, best.j - ai, bi, best.i - bi);
    out.push("same", best.j, best.len, best.i, best.len);
    ai = best.j + best.len;
    bi = best.i + best.len;
    i = bi;
  }
  out.push("change", ai, a.length - ai, bi, b.length - bi);
  return out.result;
}

// ── what the view needs ────────────────────────────────────────────────────

// The chunks regrouped for a view that sets the two files side by side.  A
// "pair" run is a stretch over which both sides advance together, so their
// lines (or their rows of bytes) can be put beside each other and read
// across; a "gap" is where one side has more than the other, and there the
// rows stop pairing - that is the shift itself, and the only place the view
// has to show a side on its own.
export function alignedRuns(chunks) {
  const out = [];
  let run = null;
  for (const c of chunks) {
    if (c.aLen === c.bLen) {
      if (!run) run = { kind: "pair", aStart: c.aStart, bStart: c.bStart, len: 0 };
      run.len += c.aLen;
      continue;
    }
    if (run) { out.push(run); run = null; }
    out.push({ kind: "gap", aStart: c.aStart, aLen: c.aLen, bStart: c.bStart, bLen: c.bLen });
  }
  if (run) out.push(run);
  return out;
}

// How much of the two is common, 0..1 - so that "the same file but for one
// byte" is visible in the heading without any scrolling.
export function sameFraction(chunks, aLen, bLen) {
  let same = 0;
  for (const c of chunks) if (c.kind === "same") same += c.aLen;
  const total = Math.max(aLen, bLen);
  return total ? same / total : 1;
}

// Where two lines part: the common start and end trimmed, so that the view
// can mark what differs rather than the whole line.
export function within(x, y) {
  const max = Math.min(x.length, y.length);
  let head = 0;
  while (head < max && x[head] === y[head]) ++head;
  let tail = 0;
  while (tail < max - head && x[x.length - 1 - tail] === y[y.length - 1 - tail]) ++tail;
  return { head, aEnd: x.length - tail, bEnd: y.length - tail };
}
