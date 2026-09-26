"""pack.py - the tune into the game: the repaired bodies of the software
collection get the blob appended and two jumps patched.

    python source/pack.py [--check]

Works on `software/games/sabot2/fixed/{osa,omega}/SABOT2.DAT` of the
collection (MS0515_SOFTWARE_DIR, else `ms0515-software` next to this
repository), idempotently: whatever is beyond the 85 blocks of the body
is dropped first, so running it again replaces the blob.  --check only
reports what it would do and whether the bodies carry the blob already.

The blob is SAB2G.SAV from 1000 to its high limit (word 050 of block 0),
padded to whole blocks; the game's loader loads the whole file, so the
blob lands at 125000, right after the body.  The two patches, both four
bytes for four bytes:

  the start: the JMP after the screen clear (1220 osa, 1250 omega) goes
      to the blob at 125000 instead of the intro; the blob copies itself
      to 140000, plays the tune until a key and jumps on to the intro
  the high-score table: the wait-for-a-key at its end (112016 osa, 112136
      omega) becomes a JSR to the blob's second entry at 140002, which
      plays the tune once and waits for the key only if the tune ended
      by itself

The blob's head takes the game's own wait-for-a-key and the continuation
address, so one build of the engine serves both bodies.
"""
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
REPO_ROOT = HERE.parents[3]
COLLECTION = Path(os.environ.get("MS0515_SOFTWARE_DIR") or (REPO_ROOT.parent / "ms0515-software"))
FIXED = COLLECTION / "software" / "games" / "sabot2" / "fixed"

BLOCK = 512
BODY_BLOCKS = 85
LOAD = 0o125000                 # where the blob lands: BODY_BLOCKS * BLOCK
HOME = 0o140000                 # where it runs from
JMP_ABS = 0o000137              # JMP @#addr
JSR_ABS = 0o004737              # JSR PC,@#addr

# per build: the start hook (a PC-relative JMP), the key-wait hook (a
# PC-relative CALL), the routines the blob calls, the intro's address
BUILDS = {
    "osa":   dict(start=0o1220, cont=0o6660, wait_hook=0o112016, waitk=0o113100),
    "omega": dict(start=0o1250, cont=0o6740, wait_hook=0o112136, waitk=0o113220),
}

assert LOAD == BODY_BLOCKS * BLOCK


def word(data, off):
    return struct.unpack_from("<H", data, off)[0]


def put_word(data, off, value):
    struct.pack_into("<H", data, off, value & 0xFFFF)


def blob():
    sav = (PROJECT / "SAB2G.SAV").read_bytes()
    top = word(sav, 0o50)
    code = bytearray(sav[0o1000:top + 1])
    code += bytes(-len(code) % BLOCK)
    return code


def expect(data, off, words, what):
    have = [word(data, off + 2 * i) for i in range(len(words))]
    if have != list(words):
        raise SystemExit(f"{what}: expected {[oct(w) for w in words]} at {oct(off)}, "
                         f"found {[oct(w) for w in have]} - not the body this packer knows")


def pack(name, spec, check):
    path = FIXED / name / "SABOT2.DAT"
    body = bytearray(path.read_bytes())
    had_blob = len(body) > BODY_BLOCKS * BLOCK
    body = body[:BODY_BLOCKS * BLOCK]
    # the start: JMP cont (PC-relative, 000167 offset) or our patch already
    if word(body, spec["start"]) == 0o000167:
        expect(body, spec["start"], [0o000167, (spec["cont"] - spec["start"] - 4) & 0xFFFF], f"{name} start")
    else:
        expect(body, spec["start"], [JMP_ABS, LOAD], f"{name} start")
    if word(body, spec["wait_hook"]) == 0o004767:
        expect(body, spec["wait_hook"], [0o004767, (spec["waitk"] - spec["wait_hook"] - 4) & 0xFFFF],
               f"{name} key wait")
    else:
        expect(body, spec["wait_hook"], [JSR_ABS, HOME + 2], f"{name} key wait")
    code = blob()
    put_word(code, 4, spec["waitk"])
    put_word(code, 6, spec["cont"])
    put_word(code, 8, len(code))
    put_word(body, spec["start"], JMP_ABS)
    put_word(body, spec["start"] + 2, LOAD)
    put_word(body, spec["wait_hook"], JSR_ABS)
    put_word(body, spec["wait_hook"] + 2, HOME + 2)
    out = bytes(body) + bytes(code)
    state = "has the blob" if had_blob else "is the body alone"
    print(f"{path}: {state}; blob {len(code)} bytes ({len(code) // BLOCK} blocks) -> "
          f"{len(out) // BLOCK} blocks{' (not written)' if check else ''}")
    if not check:
        path.write_bytes(out)


def main(argv):
    check = "--check" in argv
    if not FIXED.is_dir():
        raise SystemExit(f"{FIXED} not found: set MS0515_SOFTWARE_DIR")
    if not (PROJECT / "SAB2G.SAV").exists():
        raise SystemExit("SAB2G.SAV not built: run the build first")
    for name, spec in BUILDS.items():
        pack(name, spec, check)


if __name__ == "__main__":
    main(sys.argv[1:])
