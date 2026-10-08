"""patch-gcc.py - the fix of GCC's pdp11 backend for signed comparisons
of longs, applied to an unpacked source tree (build-toolchain.sh runs it;
it is written as a script so that it can regenerate gcc-pdp11-cmpsi.patch
when the GCC version moves).

    patch-gcc.py <gcc source tree>

The fault (README.md, "A trap in the compiler"): cmpsi and cmpdi compare
the high words, and when those are equal the low words, and the one
conditional branch that follows reads the flags of whichever compare
ran last.  A signed branch (bgt, bge, blt, ble) on the low words'
flags is wrong: with equal high words the order of two longs is the
UNSIGNED order of their low words.  After the last compare, five words
rewrite N and V from C, so that N xor V = C: the signed branches then
read the unsigned order, and the unsigned branches, which read C and Z
only, see what they saw before.  Z and C are not touched.

      clv
      bcs 1f
      cln
      br  2f
  1:  sen
  2:

The insn's length grows by those ten bytes (pdp11_cmp_length), which
the branch-range computation needs."""
import re
import sys
from pathlib import Path

FIXUP = r'''  output_asm_insn ("clv", exops[0]);
  output_asm_insn ("bcs\t%l0", fx);
  output_asm_insn ("cln", exops[0]);
  output_asm_insn ("br\t%l0", fx + 1);
  output_asm_label (fx[0]);
  fputs (":\n", asm_out_file);
  output_asm_insn ("sen", exops[0]);
  output_asm_label (fx[1]);
  fputs (":\n", asm_out_file);
'''

MD_SI_OLD = '''   output_asm_insn ("cmp\\t%0,%1", exops[1]);
  output_asm_label (lb[0]);
  fputs (":\\n", asm_out_file);

  return "";
}
  [(set (attr "length")
	(symbol_ref "pdp11_cmp_length (operands, 2)"))'''
MD_SI_NEW = '''   output_asm_insn ("cmp\\t%0,%1", exops[1]);
''' + FIXUP + '''  output_asm_label (lb[0]);
  fputs (":\\n", asm_out_file);

  return "";
}
  [(set (attr "length")
	(symbol_ref "pdp11_cmp_length (operands, 2)"))'''

MD_DI_OLD = '''   output_asm_insn ("cmp\\t%0,%1", exops[3]);
  output_asm_label (lb[0]);
   fputs (":\\n", asm_out_file);'''
MD_DI_NEW = '''   output_asm_insn ("cmp\\t%0,%1", exops[3]);
''' + FIXUP + '''  output_asm_label (lb[0]);
   fputs (":\\n", asm_out_file);'''

LABELS_OLD = "  lb[0] = gen_label_rtx ();\n"
LABELS_NEW = "  lb[0] = gen_label_rtx ();\n  fx[0] = gen_label_rtx ();\n  fx[1] = gen_label_rtx ();\n"
DECL_OLD = "  rtx lb[1];\n"
DECL_NEW = "  rtx lb[1];\n  rtx fx[2];\n"

CC_OLD = '''  /* Deduct one word because there is no branch at the end.  */
  return len - 2;'''
CC_NEW = '''  /* Deduct one word because there is no branch at the end, and add the
     five words that set N and V from C after the last compare, so that
     a signed branch reads the unsigned order of the low words.  */
  return len - 2 + 10;'''


def edit(path, replacements):
    text = path.read_text()
    for old, new in replacements:
        count = text.count(old)
        if count == 0:
            raise SystemExit(f"{path}: pattern not found:\n{old}")
        text = text.replace(old, new)
    path.write_text(text)


def main():
    tree = Path(sys.argv[1])
    md = tree / "gcc/config/pdp11/pdp11.md"
    cc = tree / "gcc/config/pdp11/pdp11.cc"
    if "fx[0] = gen_label_rtx" in md.read_text():
        print("pdp11.md: already patched")
        return
    edit(md, [(MD_SI_OLD, MD_SI_NEW), (MD_DI_OLD, MD_DI_NEW),
              (DECL_OLD, DECL_NEW), (LABELS_OLD, LABELS_NEW)])
    edit(cc, [(CC_OLD, CC_NEW)])
    print("patched pdp11.md and pdp11.cc")


if __name__ == "__main__":
    main()
