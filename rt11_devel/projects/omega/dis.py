"""dis.py - disassemble a stretch of a monitor file (file offset = link
address, as in the RT11SJ link map).

    python dis.py FILE START END        (octal offsets)
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from pdp11_disasm import Disassembler  # noqa: E402


def main():
    data = Path(sys.argv[1]).read_bytes()
    start, end = int(sys.argv[2], 8), int(sys.argv[3], 8)
    d = Disassembler(data[start:end], start)
    for addr, text in d.disassemble_all():
        print(f'{addr}: {text}')


if __name__ == '__main__':
    main()
