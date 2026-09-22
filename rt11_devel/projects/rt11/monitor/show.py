"""show.py - one difference, both sides: Omega's code disassembled and the
DEC source lines the build has there.

    python show.py BUILDDIR OMEGA.SYS OSTART OEND BSTART BEND [PAD]   (octal)

PAD (default 6) words of context are added on both sides.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    build, omega = sys.argv[1], sys.argv[2]
    o1, o2, b1, b2 = (int(x, 8) for x in sys.argv[3:7])
    pad = 2 * (int(sys.argv[7]) if len(sys.argv) > 7 else 6)
    print('--- omega %06o..%06o' % (o1, o2))
    subprocess.run([sys.executable, str(HERE / 'dis.py'), omega,
                    '%o' % max(0, o1 - pad), '%o' % (o2 + pad)])
    print('--- DEC source, built %06o..%06o' % (b1, b2))
    out = subprocess.run([sys.executable, str(HERE / 'where.py'), build,
                          '%o' % max(0, b1 - pad), '%o' % (b2 + pad)],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        if ' RTDATA ' in line and '|' in line and '=' in line.split('|', 1)[1][:40]:
            continue                     # the symbol definitions listed at 0
        print(line[:130])


if __name__ == '__main__':
    main()
