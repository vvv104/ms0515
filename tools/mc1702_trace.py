#!/usr/bin/env python3
"""Wire tracer for the scanned МС 1702 factory schematic (3.098.002 Э3).

Follows the drawn wires between the symbol boxes listed in a JSON file and
prints the nets it finds, so that unlabelled point-to-point wiring can be
read off the photographed sheet mechanically instead of by eye.

    python tools/mc1702_trace.py SCAN.png BOXES.json OUT_DIR [STUB ...]

SCAN.png   one strip of the drawing (e.g. the left third of Э3, ~3300x4700 px,
           kept outside the repository - see docs/kb/mc1702.md "Sources")
BOXES.json {"roi": [x, y, w, h], "boxes": {"D9": {"rect": [x1, y1, x2, y2],
           "L": ["2@3977", ...], "R": [...]}, "thresh": 9}: rough symbol rectangles in
           scan pixels (snapped to the drawn edges) and the pin numbers of the
           left/right sides top to bottom, optionally with an explicit "@y";
           "loose": true accepts left-side pins whose number is written between
           the wire end and the box edge (CPU-style symbols).
OUT_DIR    receives trace.png (segments, dots, boxes, stub indices), nets.png
           (each multi-pin net in its own colour) and, for every STUB named on
           the command line (e.g. D16.1.3), diag_<stub>.png: a contact sheet of
           the junction events that built that net, for checking at 3x.

How it reads the drawing (rules found by checking against labelled nets):
- wires are the thin orthogonal ink runs (morphological opening); thick runs
  are frame / bus-box lines and are not wires - wires are severed at them;
- two wires connect at an L corner only when both END there (ink does not go
  on past the corner); a wire ending on a passing wire is a T and connects
  only through a junction dot (a round blob thicker than a line);
- a broken run (a digit written across it) is bridged when nothing else ends
  in the gap and no other line passes through it; a bridge across a thick
  frame line is reported as a "frame" event - the labels decide those;
- a pin is a horizontal stub reaching a box edge (on the left side the ink
  must run up to the edge; on the right side the pin number sits on the line).
Everything the tracer joins is listed as an event with coordinates; every
multi-pin net that contradicts a drawn label is to be rejected by the reader.
"""
import cv2
import itertools
import json
import sys
from collections import defaultdict

import numpy as np

THICK = 2.6        # median half-width (px) above which a run is a frame line, not a wire
MIN_RUN = 16       # opening length: shorter ink runs are text
GAP = 40           # longest gap bridged along a broken wire
CORNER_TOL = 6     # px: how close two wire ends must be to form a corner
THRESH_C = 9       # adaptive-threshold offset; lower it (env MC1702_THRESH) for fainter strips


def load(src, x0, y0, w, h):
    img = cv2.imread(src, cv2.IMREAD_GRAYSCALE)
    if img is None:
        sys.exit("cannot read %s" % src)
    roi = img[y0:y0 + h, x0:x0 + w]
    bw = cv2.adaptiveThreshold(roi, 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY_INV, 41, THRESH_C)
    return roi, bw


def lines(bw):
    H = cv2.morphologyEx(bw, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (MIN_RUN, 1)))
    V = cv2.morphologyEx(bw, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_RECT, (1, MIN_RUN)))
    return H, V


def sever(H, V, dt):
    """Cut wires where they touch thick frame / bus-box lines; return masks and the frame mask."""
    frames = []
    for mask in (H, V):
        n, lab, st, _ = cv2.connectedComponentsWithStats((mask > 0).astype(np.uint8), connectivity=8)
        frame = np.zeros(mask.shape, np.uint8)
        for i in range(1, n):
            sel = lab == i
            if np.median(dt[sel]) > THICK:
                frame[sel] = 1
        frames.append(cv2.dilate(frame, np.ones((9, 9), np.uint8)))
    frameH, frameV = frames
    H2 = H.copy()
    H2[frameV > 0] = 0
    V2 = V.copy()
    V2[frameH > 0] = 0
    return H2, V2, (frameH > 0) | (frameV > 0)


def segments(mask, horizontal, dt):
    n, lab, st, _ = cv2.connectedComponentsWithStats((mask > 0).astype(np.uint8), connectivity=8)
    segs = []
    for i in range(1, n):
        x, y, w, h, a = st[i]
        if (w if horizontal else h) < 12:
            continue
        ys, xs = np.where(lab == i)
        if float(np.median(dt[ys, xs])) > THICK:
            continue  # frame line
        if horizontal:
            x1, x2 = xs.min(), xs.max()
            y1 = int(np.median(ys[xs <= x1 + 3]))
            y2 = int(np.median(ys[xs >= x2 - 3]))
            segs.append({"h": True, "p1": (int(x1), y1), "p2": (int(x2), y2)})
        else:
            y1, y2 = ys.min(), ys.max()
            x1 = int(np.median(xs[ys <= y1 + 3]))
            x2 = int(np.median(xs[ys >= y2 - 3]))
            segs.append({"h": False, "p1": (x1, int(y1)), "p2": (x2, int(y2))})
    return segs


def dots(bw, H, V, dt, rects):
    """Junction dots: round ink blobs thicker than a line, on a line, away from the pin-number zones."""
    D = cv2.morphologyEx(bw, cv2.MORPH_OPEN, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))
    n, lab, st, cent = cv2.connectedComponentsWithStats(D)
    M = cv2.dilate(((H > 0) | (V > 0)).astype(np.uint8), np.ones((5, 5), np.uint8))
    out = []
    for i in range(1, n):
        a, ww, hh = st[i, cv2.CC_STAT_AREA], st[i, cv2.CC_STAT_WIDTH], st[i, cv2.CC_STAT_HEIGHT]
        if not (60 <= a < 180 and 8 <= ww <= 16 and 8 <= hh <= 16 and abs(ww - hh) <= 4 and a > 0.6 * ww * hh):
            continue
        cx, cy = int(cent[i][0]), int(cent[i][1])
        if not M[cy, cx] or dt[max(0, cy - 3):cy + 4, max(0, cx - 3):cx + 4].max() < 4.0:
            continue
        if any(r[1] - 25 <= cy <= r[3] + 25 and (abs(cx - r[0]) <= 25 or abs(cx - r[2]) <= 25) for r in rects):
            continue
        out.append((cx, cy))
    return out


def near(p, q, t):
    return abs(p[0] - q[0]) <= t and abs(p[1] - q[1]) <= t


def on_seg(seg, p, t=4):
    (x1, y1), (x2, y2) = seg["p1"], seg["p2"]
    if seg["h"]:
        if not (x1 - t <= p[0] <= x2 + t):
            return False
        return abs(p[1] - (y1 + (y2 - y1) * (p[0] - x1) / max(1, x2 - x1))) <= t
    if not (y1 - t <= p[1] <= y2 + t):
        return False
    return abs(p[0] - (x1 + (x2 - x1) * (p[1] - y1) / max(1, y2 - y1))) <= t


def is_outline(s, rects):
    """A segment lying on a symbol box edge (edges may be shared by stacked gates) or inside a box."""
    for r in rects:
        bh, bw_ = r[3] - r[1], r[2] - r[0]
        if s["h"] and min(abs(s["p1"][1] - r[1]), abs(s["p1"][1] - r[3]), abs(s["p2"][1] - r[1]), abs(s["p2"][1] - r[3])) <= 12:
            if min(s["p2"][0], r[2]) - max(s["p1"][0], r[0]) >= min(40, 0.5 * bw_):
                return True
        if not s["h"] and min(abs(s["p1"][0] - r[0]), abs(s["p1"][0] - r[2]), abs(s["p2"][0] - r[0]), abs(s["p2"][0] - r[2])) <= 12:
            if min(s["p2"][1], r[3]) - max(s["p1"][1], r[1]) >= min(40, 0.5 * bh):
                return True
        if s["p1"][0] >= r[0] - 6 and s["p2"][0] <= r[2] + 6 and s["p1"][1] >= r[1] - 6 and s["p2"][1] <= r[3] + 6:
            return True
    return False


def build(segs, dotlist, rects, ink, Hm, Vm):
    """Union-find over wire segments; returns keep flags, the find() function and the event log."""
    parent = list(range(len(segs)))
    events = []

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a, b, kind, at):
        if find(a) != find(b):
            events.append((kind, at, a, b))
        parent[find(a)] = find(b)

    keep = [not is_outline(s, rects) for s in segs]
    hs = [i for i, s in enumerate(segs) if keep[i] and s["h"]]
    vs = [i for i, s in enumerate(segs) if keep[i] and not s["h"]]

    def continues(i, end):
        """The ink goes on beyond this end of segment i: a wire passing through (T), not a corner."""
        a = segs[i]
        x, y = a[end]
        h, w = ink.shape
        if a["h"]:
            cols = [ink[max(0, y - 3):y + 4, c].any() for c in (range(x + 4, x + 14) if end == "p2" else range(x - 14, x - 4)) if 0 <= c < w]
            return len(cols) >= 6 and sum(cols) >= 0.7 * len(cols)
        rows = [ink[r, max(0, x - 3):x + 4].any() for r in (range(y + 4, y + 14) if end == "p2" else range(y - 14, y - 4)) if 0 <= r < h]
        return len(rows) >= 6 and sum(rows) >= 0.7 * len(rows)

    for i in hs:
        a = segs[i]
        for j in vs:
            b = segs[j]
            for ea in ("p1", "p2"):
                for eb in ("p1", "p2"):
                    if near(a[ea], b[eb], CORNER_TOL) and not continues(i, ea) and not continues(j, eb):
                        union(i, j, "corner", a[ea])
    for (cx, cy) in dotlist:
        touching = [i for i in hs + vs if on_seg(segs[i], (cx, cy), 5)]
        for a, b in zip(touching, touching[1:]):
            union(a, b, "dot", (cx, cy))

    def at_corner(p, horizontal):
        return any(near(segs[k]["p1"], p, 8) or near(segs[k]["p2"], p, 8) for k in (vs if horizontal else hs))

    def crossed(p, q, horizontal):
        if horizontal:
            band = Vm[max(0, p[1] - 3):p[1] + 4, min(p[0], q[0]):max(p[0], q[0]) + 1]
        else:
            band = Hm[min(p[1], q[1]):max(p[1], q[1]) + 1, max(0, p[0] - 3):p[0] + 4]
        return band.size > 0 and band.any()

    def bridge(i, j, p, q):
        mid = ((p[0] + q[0]) // 2, (p[1] + q[1]) // 2)
        horizontal = segs[i]["h"]
        if at_corner(p, horizontal) or at_corner(q, horizontal):
            events.append(("gap-vetoed", mid, i, j))
        elif crossed(p, q, horizontal):
            events.append(("bus-vetoed", mid, i, j))
        else:
            union(i, j, "frame" if build.frame[min(mid[1], build.frame.shape[0] - 1), min(mid[0], build.frame.shape[1] - 1)] else "gap", mid)

    for group, horizontal in ((hs, True), (vs, False)):
        k = 1 if horizontal else 0   # coordinate that must match; the other one is along the wire
        for i, j in itertools.combinations(group, 2):
            a, b = segs[i], segs[j]
            for first, second in ((a, b), (b, a)):
                if abs(first["p2"][k] - second["p1"][k]) <= 3 and 0 <= second["p1"][1 - k] - first["p2"][1 - k] <= GAP:
                    bridge(i, j, first["p2"], second["p1"])
    return keep, find, events


def snap(rect, segs, x0, y0):
    """Move a rough box rectangle's edges onto the nearest long drawn lines (within 18 px)."""
    x1, y1, x2, y2 = rect[0] - x0, rect[1] - y0, rect[2] - x0, rect[3] - y0
    h, w = y2 - y1, x2 - x1
    best = {"x1": (18, x1), "x2": (18, x2), "y1": (18, y1), "y2": (18, y2)}
    for s in segs:
        if not s["h"]:
            if s["p2"][1] - s["p1"][1] < 0.5 * h or s["p1"][1] > y2 - 0.3 * h or s["p2"][1] < y1 + 0.3 * h:
                continue
            x = (s["p1"][0] + s["p2"][0]) // 2
            for key, xe in (("x1", x1), ("x2", x2)):
                if abs(x - xe) < best[key][0]:
                    best[key] = (abs(x - xe), x)
        else:
            if s["p2"][0] - s["p1"][0] < 0.5 * w or s["p1"][0] > x2 - 0.3 * w or s["p2"][0] < x1 + 0.3 * w:
                continue
            y = (s["p1"][1] + s["p2"][1]) // 2
            for key, ye in (("y1", y1), ("y2", y2)):
                if abs(y - ye) < best[key][0]:
                    best[key] = (abs(y - ye), y)
    return [best["x1"][1], best["y1"][1], best["x2"][1], best["y2"][1]]


def stubs(segs, keep, rects, ink, loose=()):
    """Horizontal wire ends touching a box's left/right edge = pins, indexed top to bottom per side."""
    def reaches(xa, xb, y):
        if xb - xa < 3:
            return True
        cols = [ink[max(0, y - 3):y + 4, c].any() for c in range(xa, xb)]
        return sum(cols) >= 0.8 * len(cols)
    out = []
    for bi, r in enumerate(rects):
        for i, s in enumerate(segs):
            if not keep[i] or not s["h"] or s["p2"][0] - s["p1"][0] < 18:
                continue
            y = s["p1"][1]
            if not (r[1] - 4 <= y <= r[3] + 4):
                continue
            if abs(s["p2"][0] - r[0]) <= (80 if bi in loose else 26) and s["p1"][0] < r[0] - 8 and (bi in loose or reaches(s["p2"][0], r[0] + 2, s["p2"][1])):
                out.append({"box": bi, "side": "L", "y": y, "seg": i})
            elif abs(s["p1"][0] - r[2]) <= 45 and s["p2"][0] > r[2] + 8:
                out.append({"box": bi, "side": "R", "y": y, "seg": i})
    for bi in range(len(rects)):
        for side in "LR":
            for k, s in enumerate(sorted([s for s in out if s["box"] == bi and s["side"] == side], key=lambda s: s["y"])):
                s["idx"] = k + 1
    return out


def pin_name(s, names, pins, y0):
    lst = pins[names[s["box"]]].get(s["side"], [])
    if lst and "@" in lst[0]:
        best = None
        for e in lst:
            nm, yy = e.split("@")
            d = abs(int(yy) - (s["y"] + y0))
            if d <= 9 and (best is None or d < best[0]):
                best = (d, nm)
        return best[1] if best else "%s%d?" % (s["side"], s["idx"])
    return lst[s["idx"] - 1] if s["idx"] - 1 < len(lst) else "%s%d?" % (s["side"], s["idx"])


def draw(out, roi, segs, keep, dl, rects, names, st, nets, find):
    ov = (cv2.cvtColor(roi, cv2.COLOR_GRAY2BGR) * 0.6 + 100).astype(np.uint8)
    for i, s in enumerate(segs):
        if keep[i]:
            cv2.line(ov, s["p1"], s["p2"], (0, 0, 255) if s["h"] else (255, 0, 0), 1)
    for (cx, cy) in dl:
        cv2.circle(ov, (cx, cy), 7, (0, 180, 0), 2)
    for bi, r in enumerate(rects):
        cv2.rectangle(ov, (r[0], r[1]), (r[2], r[3]), (0, 160, 255), 2)
        cv2.putText(ov, names[bi], (r[0] + 4, r[1] - 6), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 120, 255), 2)
    for s in st:
        x = rects[s["box"]][0] - 14 if s["side"] == "L" else rects[s["box"]][2] + 3
        cv2.putText(ov, str(s["idx"]), (x, s["y"] - 2), cv2.FONT_HERSHEY_PLAIN, 0.9, (160, 0, 160), 1)
    cv2.imwrite(out + "/trace.png", ov)
    rng = np.random.RandomState(3)
    col = (cv2.cvtColor(roi, cv2.COLOR_GRAY2BGR) * 0.5 + 128).astype(np.uint8)
    comp = defaultdict(list)
    for i, s in enumerate(segs):
        if keep[i]:
            comp[find(i)].append(s)
    for root, v in nets.items():
        if len(v) < 2:
            continue
        c = tuple(int(x) for x in rng.randint(0, 200, 3))
        for sg in comp[root]:
            cv2.line(col, sg["p1"], sg["p2"], c, 3)
    for bi, r in enumerate(rects):
        cv2.rectangle(col, (r[0], r[1]), (r[2], r[3]), (0, 160, 255), 1)
        cv2.putText(col, names[bi], (r[0] + 4, r[1] - 6), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 120, 255), 2)
    cv2.imwrite(out + "/nets.png", col)


def contact_sheet(out, roi, events, x0, y0, want, segs):
    tiles = []
    for k, (kind, at, a, b) in enumerate(events):
        cx, cy = at
        x1, y1 = max(0, cx - 60), max(0, cy - 60)
        crop = cv2.cvtColor(cv2.resize(roi[y1:y1 + 120, x1:x1 + 120], (360, 360), interpolation=cv2.INTER_CUBIC), cv2.COLOR_GRAY2BGR)
        cv2.circle(crop, (int((cx - x1) * 3), int((cy - y1) * 3)), 14, (0, 0, 255), 2)
        cv2.putText(crop, "%d %s (%d,%d)" % (k, kind, cx + x0, cy + y0), (4, 18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 0, 0), 1)
        tiles.append(crop)
        sa, sb = segs[a], segs[b]
        print("  ev%d %s at (%d,%d)  A=%s(%d,%d)-(%d,%d)  B=%s(%d,%d)-(%d,%d)" % (
            k, kind, cx + x0, cy + y0, "H" if sa["h"] else "V", sa["p1"][0] + x0, sa["p1"][1] + y0, sa["p2"][0] + x0, sa["p2"][1] + y0,
            "H" if sb["h"] else "V", sb["p1"][0] + x0, sb["p1"][1] + y0, sb["p2"][0] + x0, sb["p2"][1] + y0))
    if tiles:
        blank = np.full((360, 360, 3), 255, np.uint8)
        rows = [np.hstack(tiles[i:i + 5] + [blank] * (5 - len(tiles[i:i + 5]))) for i in range(0, len(tiles), 5)]
        cv2.imwrite(out + "/diag_%s.png" % want.replace(".", "_"), np.vstack(rows))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    src, cfg, out = sys.argv[1], json.load(open(sys.argv[2], encoding="utf-8")), sys.argv[3]
    global THRESH_C
    THRESH_C = int(cfg.get("thresh", THRESH_C))
    x0, y0, w, h = cfg["roi"]
    roi, bw = load(src, x0, y0, w, h)
    H, V = lines(bw)
    dt = cv2.distanceTransform(bw, cv2.DIST_L2, 5)
    H, V, frame = sever(H, V, dt)
    build.frame = frame
    segs = segments(H, True, dt) + segments(V, False, dt)
    names = list(cfg["boxes"])
    pins = cfg["boxes"]
    rects = [snap(pins[n]["rect"], segs, x0, y0) for n in names]
    dl = dots(bw, H, V, dt, rects)
    keep, find, events = build(segs, dl, rects, bw > 0, (H > 0) & ~frame, (V > 0) & ~frame)
    st = stubs(segs, keep, rects, bw > 0, loose={i for i, n in enumerate(names) if pins[n].get("loose")})
    nets = defaultdict(list)
    for s in st:
        nets[find(s["seg"])].append(s)
    draw(out, roi, segs, keep, dl, rects, names, st, nets, find)

    def label(s):
        return "%s.%s" % (names[s["box"]], pin_name(s, names, pins, y0))

    print("segments %d  dots %d  stubs %d" % (sum(keep), len(dl), len(st)))
    for bi, n in enumerate(names):
        r = rects[bi]
        ls = [str(s["y"] + y0) for s in sorted(st, key=lambda s: s["y"]) if s["box"] == bi and s["side"] == "L"]
        rs = [str(s["y"] + y0) for s in sorted(st, key=lambda s: s["y"]) if s["box"] == bi and s["side"] == "R"]
        eL, eR = len(pins[n].get("L", [])), len(pins[n].get("R", []))
        flag = "" if (len(ls) == eL or eL == 0) and (len(rs) == eR or eR == 0) else "  <-- MISMATCH (expected L%d R%d)" % (eL, eR)
        print("%-6s x=%d..%d y=%d..%d  L[%s]  R[%s]%s" % (n, r[0] + x0, r[2] + x0, r[1] + y0, r[3] + y0, " ".join(ls), " ".join(rs), flag))
    comp = defaultdict(list)
    for i, s in enumerate(segs):
        if keep[i]:
            comp[find(i)].append(s)
    for root, v in nets.items():
        if len(v) > 1:
            fr = [e for e in events if find(e[2]) == root and e[0] == "frame"]
            if fr:
                print("  frame crossings:", " ".join("(%d,%d)" % (e[1][0] + x0, e[1][1] + y0) for e in fr))
            print("NET:", "  ".join(label(s) for s in sorted(v, key=lambda s: (s["box"], s["side"], s["idx"]))))
        else:
            s = v[0]
            pts = [p for sg in comp[root] for p in (sg["p1"], sg["p2"])]
            edge = rects[s["box"]][0] if s["side"] == "L" else rects[s["box"]][2]
            far = max(pts, key=lambda p: abs(p[0] - edge) + abs(p[1] - s["y"]))
            print("END: %s -> (%d,%d) via %d segs" % (label(s), far[0] + x0, far[1] + y0, len(comp[root])))
    for want in sys.argv[4:]:
        target = [s for s in st if label(s) == want]
        if not target:
            print("no stub", want)
            continue
        root = find(target[0]["seg"])
        ev = [e for e in events if find(e[2]) == root]
        print("EVENTS for", want, ":", len(ev))
        contact_sheet(out, roi, ev, x0, y0, want, segs)


if __name__ == "__main__":
    main()
