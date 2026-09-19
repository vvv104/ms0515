"""koi8mac.py - bytes of a kit's text as MACRO-11 source lines: runs of
printable ASCII as .ASCII, the rest (KOI-8 letters, control bytes) as .BYTE
in octal, for the modules that carry a kit's texts (the sources stay ASCII).

    python koi8mac.py FILE START END      (octal offsets, END exclusive)
"""
import sys
from pathlib import Path


def lines(data: bytes, indent='\t') -> list[str]:
    out, i = [], 0
    while i < len(data):
        j = i
        while j < len(data) and 0o40 <= data[j] < 0o177:
            j += 1
        if j - i >= 2:
            text = data[i:j].decode('ascii')
            delim = next(d for d in '/"\\|!%&^' if d not in text)
            out.append(f'{indent}.ASCII\t{delim}{text}{delim}')
            i = j
            continue
        j = i
        while j < len(data) and not (0o40 <= data[j] < 0o177 and j + 1 < len(data)
                                     and 0o40 <= data[j + 1] < 0o177):
            j += 1
        j = max(j, i + 1)
        chunk = data[i:j]
        for k in range(0, len(chunk), 12):
            out.append(f'{indent}.BYTE\t' + ','.join(f'{b:o}' for b in chunk[k:k + 12]))
        i = j
    return out


def main():
    data = Path(sys.argv[1]).read_bytes()[int(sys.argv[2], 8):int(sys.argv[3], 8)]
    print('\n'.join(lines(data)))


if __name__ == '__main__':
    main()
