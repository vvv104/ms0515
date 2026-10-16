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
the branch-range computation needs.

The second change is speed: without ASH the backend writes a shift by
a constant up to 3 as single shifts and a longer one as a loop of four
words, three instructions a step, so a shift by 5 costs seventeen
instructions.  pdp11_small_shift, which decides, and the predicate
that matches it now write shifts out up to 8 when speed is wanted and
keep the loop from 4 under -Os.

The third is a fault again: a long read through a pointer into the
register pair the pointer was in (a pointer in r0, the long for r0:r1)
was loaded high word first - over the pointer - and the low word read
through what had just come in: mov 4(r0),r0 / mov 6(r0),r1.  Every -O0
build had it, -Os where a function gave the pointer back.
pdp11_expand_operands, which orders the words of a two- or four-word
move, ordered them for a register pair overlapping another, not for one
overlapping the address; such a move now loads the address register
last.

The fourth is the price of a multiply.  Without EIS (-m10) MUL, DIV and
the rest are calls of libgcc's helpers, but pdp11_rtx_costs priced a
multiply as one MUL, and under -Os as any instruction: a loop walking a
table by a pointer was rewritten as a count times the element's size, a
call of __mulhi3 for each pointer a step.  They are priced now as the
calls they are."""
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

SHIFT_OLD = '''bool
pdp11_small_shift (int n)
{
  return (unsigned) n < 4;
}'''
SHIFT_NEW = '''bool
pdp11_small_shift (int n)
{
  /* A shift by n with no ASH is n single shifts in a row, or a loop of
     one shift and a count - four words, three instructions a step.  The
     row is faster for any n and shorter up to 4; when speed is wanted
     it is used up to 8 (MS 0515: a shift by 5 is in every rotation).  */
  return (unsigned) n < (optimize_size ? 4u : 9u);
}'''

PRED_OLD = '''(define_predicate "expand_shift_operand"
  (and (match_code "const_int")
       (match_test "(unsigned) INTVAL (op) < 4")))'''
PRED_NEW = '''(define_predicate "expand_shift_operand"
  (and (match_code "const_int")
       (match_test "pdp11_small_shift (INTVAL (op))")))'''

CC_OLD = '''  /* Deduct one word because there is no branch at the end.  */
  return len - 2;'''
CC_NEW = '''  /* Deduct one word because there is no branch at the end, and add the
     five words that set N and V from C after the last compare, so that
     a signed branch reads the unsigned order of the low words.  */
  return len - 2 + 10;'''

ORDER_OLD = ("\t       REGNO (operands[0]) < REGNO (operands[1]) + words)\n"
             "\t    useorder = little;\n")
ORDER_NEW = ORDER_OLD + (
    "      /* A register pair loaded from memory addressed by its first\n"
    "\t register: that register last, else the first word loaded is\n"
    "\t the address the second is read through.  */\n"
    "      else if (order == either\n"
    "\t       && REG_P (operands[0]) && MEM_P (operands[1])\n"
    "\t       && reg_overlap_mentioned_p (gen_rtx_REG (HImode, REGNO (operands[0])),\n"
    "\t\t\t\t\t  XEXP (operands[1], 0)))\n"
    "\tuseorder = little;\n")

COST_OLD = ("  /* If optimizing for size, claim everything costs 2 per word, plus\n"
            "     whatever the operands require.  */\n")
COST_NEW = (
    "  /* Without EIS (-m10) an integer multiply, divide or remainder is no\n"
    "     instruction but a call of libgcc's helper: the arguments pushed,\n"
    "     the call and the stack put back - eight words - and for a multiply\n"
    "     a loop of up to sixteen steps.  Priced as one MUL, or under -Os as\n"
    "     any one instruction, it was chosen over a pointer stepped by an\n"
    "     add: a table walked by a pointer became a count times the size of\n"
    "     its element.  */\n"
    "  if (TARGET_10 && !FLOAT_MODE_P (mode)\n"
    "      && (code == MULT || code == DIV || code == UDIV\n"
    "\t  || code == MOD || code == UMOD))\n"
    "    {\n"
    "      *total = speed ? 40 * asize * asize : 8 * asize;\n"
    "      return false;\n"
    "    }\n"
    "\n") + COST_OLD


def edit(path, replacements):
    """Each replacement made once; one made already is passed by, so that
    a tree patched by an older version of the patch takes the rest."""
    text = path.read_text()
    for old, new in replacements:
        if new in text:
            continue
        if text.count(old) == 0:
            raise SystemExit(f"{path}: pattern not found:\n{old}")
        text = text.replace(old, new)
    path.write_text(text)


def main():
    tree = Path(sys.argv[1])
    md = tree / "gcc/config/pdp11/pdp11.md"
    cc = tree / "gcc/config/pdp11/pdp11.cc"
    pred = tree / "gcc/config/pdp11/predicates.md"
    if "fx[0] = gen_label_rtx" not in md.read_text():
        edit(md, [(MD_SI_OLD, MD_SI_NEW), (MD_DI_OLD, MD_DI_NEW),
                  (DECL_OLD, DECL_NEW), (LABELS_OLD, LABELS_NEW)])
    edit(cc, [(CC_OLD, CC_NEW), (SHIFT_OLD, SHIFT_NEW),
              (ORDER_OLD, ORDER_NEW), (COST_OLD, COST_NEW)])
    edit(pred, [(PRED_OLD, PRED_NEW)])
    print("patched pdp11.md, pdp11.cc and predicates.md")


if __name__ == "__main__":
    main()
