"""Per-frame pairing of our level changes with the reference's, then a least
squares fit of the interval errors to the path pieces (tail of the previous
half period, head, phase) - the corrections the P figures need.
fit_costs.py <ours.txt> <SAB2TN.REF>"""
import bisect
import sys
from collections import defaultdict

import numpy as np

ours = []
frames = []
for l in open(sys.argv[1]):
    a, b = l.split()
    if a == 'F':
        frames.append(int(b))
    else:
        ours.append((int(a), int(b)))
ref = []
for l in open(sys.argv[2]):
    if l[0] == '#':
        continue
    t, lv, tag = l.split()
    ref.append((int(t), int(lv), tag))
FRAME_T = 70003.0
FIRST_T = FRAME_T - 120.0
K = 15 / 7
o = next(i for i, (t, lv) in enumerate(ours) if lv == ref[0][1])
origin = ours[o][0]
our_t = [(c - origin) * 7 / 15 for c, _ in ours[o:]]      # ours in T from the first toggle
our_lv = [lv for _, lv in ours[o:]]
ref_t = [t - ref[0][0] for t, _, _ in ref]
our_ticks = [(f - origin) * 7 / 15 for f in frames if f > origin]
ref_ticks = [FIRST_T + k * FRAME_T - ref[0][0] for k in range(len(our_ticks) + 2)]

rows = []          # (tags, err)
pairs = 0
count_diff = defaultdict(int)
for k in range(min(len(our_ticks), 3000) - 1):
    a0 = bisect.bisect_left(our_t, our_ticks[k]); a1 = bisect.bisect_left(our_t, our_ticks[k + 1])
    r0 = bisect.bisect_left(ref_t, ref_ticks[k]); r1 = bisect.bisect_left(ref_t, ref_ticks[k + 1])
    na, nr = a1 - a0, r1 - r0
    count_diff[na - nr] += 1
    n = min(na, nr)
    for m in range(1, n):
        i, j = r0 + m, a0 + m
        if ref[i][1] != our_lv[j]:
            break
        tag = ref[i][2]
        if tag == 'drum' or ref[i - 1][2] == 'drum' or tag[0] == '-':
            continue
        dr = ref_t[i] - ref_t[i - 1]
        dm = our_t[j] - our_t[j - 1]
        err = (dm - dr) * K
        if abs(err) > 600:
            continue
        rows.append((tag, err))
        pairs += 1

print('frames paired; per-frame count differences:', dict(sorted(count_diff.items())))
print('interval pairs:', pairs)
tails = sorted(set(t[0] for t, _ in rows)); heads = sorted(set(t[1] for t, _ in rows)); phases = sorted(set(t[2] for t, _ in rows))
cols = [('tail', x) for x in tails] + [('head', x) for x in heads] + [('phase', x) for x in phases]
idx = {c: n for n, c in enumerate(cols)}
A = np.zeros((len(rows), len(cols)))
b = np.zeros(len(rows))
for n, (tag, err) in enumerate(rows):
    A[n, idx[('tail', tag[0])]] = 1
    A[n, idx[('head', tag[1])]] = 1
    A[n, idx[('phase', tag[2])]] = 1
    b[n] = err
# the split between the three groups is arbitrary: pin head 'n' and phase 'H' to 0
fix = [idx[('head', 'n')], idx[('phase', 'H')]]
keep = [c for c in range(len(cols)) if c not in fix]
x, *_ = np.linalg.lstsq(A[:, keep], b, rcond=None)
sol = {cols[c]: 0.0 for c in fix}
for c, v in zip(keep, x):
    sol[cols[c]] = v
print('fit (clocks our interval is too long by, per piece; head n and phase H pinned at 0):')
for c in cols:
    n = int(A[:, idx[c]].sum())
    print(f'  {c[0]:5s} {c[1]}: {sol[c]:7.1f}   (n {n})')
res = b - A @ np.array([sol[c] for c in cols])
print(f'residual rms {np.sqrt(np.mean(res**2)):.1f} clocks; raw rms {np.sqrt(np.mean(b**2)):.1f}')
by = defaultdict(list)
for tag, err in rows:
    by[tag].append(err)
for tag in sorted(by):
    v = by[tag]
    print(f'  tag {tag} n {len(v):6d} mean {np.mean(v):7.1f}')
