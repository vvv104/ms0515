"""Side by side: our level changes (clocks) vs the reference (T x 15/7).
diff_ours.py <ours.txt> <SAB2TN.REF> <from index> <count>"""
import sys

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
o = next(i for i, (t, lv) in enumerate(ours) if lv == ref[0][1])
origin = ours[o][0]
start = int(sys.argv[3])
count = int(sys.argv[4])
print(f'origin at our change {o}, cycle {origin}')
for k in range(start, start + count):
    a = ours[o + k] if o + k < len(ours) else None
    r = ref[k] if k < len(ref) else None
    da = (a[0] - ours[o + k - 1][0]) if a and k > 0 else 0
    dr = (r[0] - ref[k - 1][0]) * 15 / 7 if r and k > 0 else 0
    fr = ''
    if a:
        for fs in frames:
            if ours[o + k - 1][0] < fs <= a[0]:
                fr = 'F'
    print(f'{k:5d} ours {a[1] if a else "-"} {da:8.0f} {fr:1s}  ref {r[1] if r else "-"} {dr:8.0f} {r[2] if r else ""}')
