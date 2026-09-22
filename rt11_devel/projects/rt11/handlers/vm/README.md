# VM.SYS — the memory disk of the MS 0515

Seven of the machine's extra 8 KB banks served as a volume of 112 blocks.
The kits carry it as a binary only (ОМЕГА's, ОСА's and Rodionov's are one
file); `VM.MAC` is its source, written back from that binary with
`tools/rt11_handler.py`, and built from it the handler is the kits'
`VM.SYS` **to the byte**:

    python ../../kit/build_handler.py --source . VM OUTDIR

What the binary showed:

* it never touches the memory dispatcher.  The ROM has a pair of routines
  for the banks — `160020` puts the word in `R3`, `160024` gets the next
  one — and the handler only feeds them: `R0` the bank as a mask with one
  bit set, `R1` the dispatcher as the ROM's copy (`157700`) had it, `R2`
  where in the disk, which the ROM steps;
* a bank holds sixteen blocks, so the mask is 1 shifted left by the block
  over sixteen, worked out again at every block boundary;
* the transfer runs at priority 7: the ROM has the dispatcher turned to a
  bank while it works;
* it has no interrupt entry of its own — `VMINT` is the `.DRFIN` — and a
  short write fills the rest of its block with zeros;
* the three words at offset 110 of the header, which every kit handler
  has, are `.MODULE ... AUDIT=YES`: RAD50 `V05`, the version (8 here) and
  -1.

`validate.py` is the oracle, and the only proof for a build the kits never
had: a file goes onto `VM:` and back to a diskette, and the host reads the
copy out of the image identical — for the plain build, and for one with
`ERL$G` under a monitor, `TT` and `DZ` built the same way.

Mihin's `VM.SYS` is another build (118 blocks, `TIM$IT`, six bytes more
code) and is not reproduced here.
