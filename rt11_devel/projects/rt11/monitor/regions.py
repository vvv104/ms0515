"""regions.py - every substantial difference of one monitor part, both
sides (show.py for each), into one report.

    python regions.py BUILDDIR OMEGA.SYS PART [MINWORDS]

PART is a name compare.py prints (BSTRAP, KMON, USR, RMON, "KMON overlays").
A difference counts when the two sides' word counts differ (code added or
taken out) or it spans at least MINWORDS (default 3) words; equal-length
runs of two words are mostly relocated addresses.
"""
import difflib
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from compare import part, words  # noqa: E402


def main():
    build, omega, which = sys.argv[1], sys.argv[2], sys.argv[3]
    minw = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    a, b = words(str(Path(build) / 'RT11SJ.SYG')), words(omega)
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal' or part(j1 * 2) != which:
            continue
        if (i2 - i1) == (j2 - j1) and (j2 - j1) < minw:
            continue
        print('=' * 20, tag, 'omega %06o (%d w)  built %06o (%d w)' % (j1 * 2, j2 - j1, i1 * 2, i2 - i1))
        sys.stdout.flush()
        subprocess.run([sys.executable, str(HERE / 'show.py'), build, omega,
                        '%o' % (j1 * 2), '%o' % (j2 * 2), '%o' % (i1 * 2), '%o' % (i2 * 2), '3'])
        sys.stdout.flush()


if __name__ == '__main__':
    main()
