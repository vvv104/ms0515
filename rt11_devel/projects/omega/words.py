"""words.py - the words of two monitor files side by side.

    python words.py BUILT OMEGA START END        (octal offsets)
"""
import struct
import sys

a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
for off in range(int(sys.argv[3], 8), int(sys.argv[4], 8), 2):
    x = struct.unpack_from('<H', a, off)[0] if off + 2 <= len(a) else None
    y = struct.unpack_from('<H', b, off)[0] if off + 2 <= len(b) else None
    mark = '' if x == y else '   <>'
    print('%06o  built %s  omega %s%s' % (off, '%06o' % x if x is not None else '------',
                                          '%06o' % y if y is not None else '------', mark))
