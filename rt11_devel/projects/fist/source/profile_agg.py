"""Aggregate a PC-sample histogram (test "fist: PC profile") by routine, using
the symbol table a FIST_SYMTAB=1 build embeds (marker 125252,52525, count,
addresses; names in symtab.json).   python profile_agg.py FIST.SAV prof.txt"""
import bisect, json, struct, sys
from pathlib import Path

sav = Path(sys.argv[1]).read_bytes()
names = json.loads((Path(__file__).resolve().parent.parent / "symtab.json").read_text())
i = sav.find(struct.pack("<HH", 0o125252, 0o52525))
n = struct.unpack_from("<H", sav, i + 4)[0]
assert n == len(names), (n, len(names))
syms = sorted(zip(struct.unpack_from(f"<{n}H", sav, i + 6), names))
keys = [a for a, _ in syms]
tot, agg = 0, {}
for line in open(sys.argv[2]):
    pc, c = line.split(); pc, c = int(pc, 8), int(c)
    k = bisect.bisect_right(keys, pc) - 1
    name = syms[k][1] if k >= 0 else "?"
    agg[name] = agg.get(name, 0) + c; tot += c
for name, c in sorted(agg.items(), key=lambda x: -x[1])[:int(sys.argv[3]) if len(sys.argv) > 3 else 30]:
    print(f"{100*c/tot:6.2f}%  {c:8d}  {name}")
