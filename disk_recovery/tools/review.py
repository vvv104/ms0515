#!/usr/bin/env python3
"""
review.py — a desktop GUI to review the recovered files and pick the canonical
version of each AMBIGUOUS one.

Tabs are the confidence bands (GUARANTEED / CHOSEN / HIGH / GOOD / MEDIUM /
UNVERIFIED / AMBIGUOUS / LOST).  Each tab lists its files; selecting one shows
its distinct versions and the disks each lives on.  Buttons:
  View        show one version's content (text decoded KOI-8R, else hex)
  Diff 2      unified text diff (or block/hex diff) of two selected versions
  Set canonical   mark the selected version as correct -> the file moves to
                  CHOSEN and is written to decisions.tsv (same format decide.py
                  uses), so the pipeline picks it up and it is not re-reviewed
  Clear           undo a choice

It reads decisions.tsv on start (already-decided files appear under CHOSEN, out
of AMBIGUOUS) and rewrites it on every choice.  Stdlib only (tkinter).

  python review.py            launch the GUI
  python review.py --selftest load the model and print band counts (no window)
"""

import json, sys, difflib
from pathlib import Path
from collections import defaultdict

sys.path.insert(0, str(Path(__file__).resolve().parent))
import verdict as V
from consensus import canonical_name

OUT = Path(__file__).resolve().parents[2] / "disk_recovery" / "work" / "corpus"
STORE = OUT / "files"

def _readable(b):
    # printable ASCII; CR/LF/TAB; BS/VT/FF; SO/SI (KOI-7 РУС/ЛАТ shifts); ESC.
    return ((0x20 <= b <= 0x7E) or b in (8, 9, 10, 11, 12, 13, 14, 15, 27)
            or (0xC0 <= b <= 0xFF))

def as_text(data):
    end = len(data)
    while end and data[end-1] == 0:
        end -= 1
    body = data[:end]
    if body and sum(1 for x in body if _readable(x)) / len(body) > 0.7:
        return body.decode("koi8-r", "replace")
    return None

def hex_diff_window(parent, name, sha_a, data_a, sha_b, data_b,
                    model=None, record=None, on_refresh=None):
    """Side-by-side hex diff popup with synchronised scrolling and red-highlight
    on differing bytes (both hex and ASCII columns).  Next/Prev jump between
    differing rows."""
    import tkinter as tk
    from tkinter import ttk, messagebox, scrolledtext
    BPR = 16
    ROWS_PER_BLOCK = 512 // BPR                  # = 32: one RT-11 block per group
    n = max(len(data_a), len(data_b))
    rows = (n + BPR - 1) // BPR
    blocks = (rows + ROWS_PER_BLOCK - 1) // ROWS_PER_BLOCK
    diff_rows = []
    # Layout: before each block group of <=32 data lines we insert a header
    # line.  For block b, header is at line b*33+1 (Text lines are 1-indexed);
    # the data row r inside that block is sub = r%32 below it, so:
    #   line(r) = (r // ROWS_PER_BLOCK)*(ROWS_PER_BLOCK + 1) + (r % ROWS_PER_BLOCK) + 2
    #          = (r // ROWS_PER_BLOCK) + r + 2
    # (the previous +1 was off by one and shifted every diff highlight up by a row).
    def line_of(r):
        return (r // ROWS_PER_BLOCK) + r + 2
    total_lines = rows + blocks                  # data lines + block-header lines

    win = tk.Toplevel(parent)
    win.title(f"Compare: {name}  —  {sha_a[:8]} vs {sha_b[:8]}")
    win.geometry("1500x820")

    bar = ttk.Frame(win); bar.pack(fill="x", padx=4, pady=2)
    info = ttk.Label(bar, text="rendering...", font=("", 9))
    info.pack(side="left", padx=8)
    nxt_btn = ttk.Button(bar, text="Next diff ↓", width=14); nxt_btn.pack(side="right", padx=2)
    prv_btn = ttk.Button(bar, text="Prev diff ↑", width=14); prv_btn.pack(side="right", padx=2)
    disasm_btn = ttk.Button(bar, text="Disasm cmp", width=12); disasm_btn.pack(side="right", padx=8)
    if record is not None:
        zero_btn = ttk.Button(bar, text="Zero analysis", width=14)
        zero_btn.pack(side="right", padx=2)

        def do_zero():
            n = min(len(data_a), len(data_b)) // 512
            ZERO = b"\x00" * 512
            lines = []
            for blk in range(n):
                sa = data_a[blk*512:(blk+1)*512]; sb = data_b[blk*512:(blk+1)*512]
                za, zb = sa == ZERO, sb == ZERO
                if za and zb:
                    lines.append(f"  block {blk}: ZERO on both (natural padding)")
                elif za:
                    lines.append(f"  block {blk}: {sha_a[:8]} = ZERO, {sha_b[:8]} = data  ->  {sha_a[:8]} likely LOST")
                elif zb:
                    lines.append(f"  block {blk}: {sha_b[:8]} = ZERO, {sha_a[:8]} = data  ->  {sha_b[:8]} likely LOST")
            messagebox.showinfo(f"Zero analysis: {name}",
                                "\n".join(lines) if lines else "no zero blocks in either version")
        zero_btn.config(command=do_zero)

    hdr = ttk.Frame(win); hdr.pack(fill="x", padx=4)
    ttk.Label(hdr, text=f"{sha_a[:8]}  ({len(data_a)} B)", foreground="#444",
              font=("", 9, "bold")).pack(side="left", padx=8)
    ttk.Label(hdr, text=f"{sha_b[:8]}  ({len(data_b)} B)", foreground="#444",
              font=("", 9, "bold")).pack(side="right", padx=8)

    body = ttk.Frame(win); body.pack(fill="both", expand=True, padx=4, pady=2)
    sb = ttk.Scrollbar(body, orient="vertical"); sb.pack(side="right", fill="y")
    ta = tk.Text(body, wrap="none", font=("Consolas", 10), padx=4)
    tb = tk.Text(body, wrap="none", font=("Consolas", 10), padx=4)
    ta.pack(side="left", fill="both", expand=True)
    tb.pack(side="left", fill="both", expand=True)
    for t in (ta, tb):
        t.tag_configure("diff", foreground="red")
        t.tag_configure("addr", foreground="#888")
        t.tag_configure("blocksep", foreground="#666",
                        background="#eef0f8", font=("Consolas", 9, "bold"))
        t.tag_configure("cleaner", background="#d0f0d0")   # disasm: looks like code
        t.tag_configure("junkier", background="#f8d8d8")   # disasm: likely garbage

    # synced scrolling: any scroll on one side mirrors to the other
    syncing = [False]
    def make_yscroll(other):
        def cb(*args):
            sb.set(*args)
            if not syncing[0]:
                syncing[0] = True
                other.yview_moveto(float(args[0]))
                syncing[0] = False
        return cb
    ta.configure(yscrollcommand=make_yscroll(tb))
    tb.configure(yscrollcommand=make_yscroll(ta))
    sb.configure(command=lambda *a: (ta.yview(*a), tb.yview(*a)))
    def mwheel(e):
        d = -1 * (e.delta // 120)
        ta.yview_scroll(d, "units"); tb.yview_scroll(d, "units")
        return "break"
    for t in (ta, tb):
        t.bind("<MouseWheel>", mwheel)

    def build_line(off, seg, oth_len):
        n_ = len(seg)
        addr = f"{off:08x}  "
        hex_parts = []
        for i in range(BPR):
            hex_parts.append(f"{seg[i]:02x} " if i < n_ else "   ")
            if i == 7:
                hex_parts.append(" ")
        asc = "".join(chr(seg[i]) if i < n_ and 32 <= seg[i] <= 126 else
                      ("." if i < n_ else " ") for i in range(BPR))
        return addr + "".join(hex_parts) + "|" + asc + "|\n"

    def render(t, data, other):
        for blk in range(blocks):
            t.insert("end", f"────────── block {blk} ──────────\n", "blocksep")
            for sub in range(ROWS_PER_BLOCK):
                r = blk * ROWS_PER_BLOCK + sub
                if r >= rows:
                    break
                off = r * BPR
                seg = data[off:off+BPR]; oth = other[off:off+BPR]
                t.insert("end", build_line(off, seg, len(oth)))
                ln = line_of(r)
                t.tag_add("addr", f"{ln}.0", f"{ln}.10")
                for i in range(BPR):
                    a_has = i < len(seg); b_has = i < len(oth)
                    if a_has and b_has and seg[i] == oth[i]:
                        continue
                    if not a_has and not b_has:
                        continue
                    hpos = 10 + i*3 + (1 if i >= 8 else 0)
                    t.tag_add("diff", f"{ln}.{hpos}", f"{ln}.{hpos+2}")
                    apos = 60 + i           # 10 addr + 49 hex + 1 sep
                    t.tag_add("diff", f"{ln}.{apos}", f"{ln}.{apos+1}")
    render(ta, data_a, data_b)
    render(tb, data_b, data_a)
    for r in range(rows):
        off = r*BPR
        if data_a[off:off+BPR] != data_b[off:off+BPR]:
            diff_rows.append(r)
    for t in (ta, tb): t.configure(state="disabled")
    diff_blocks_list = sorted({r // ROWS_PER_BLOCK for r in diff_rows})
    info.config(text=f"{len(diff_blocks_list)} differing blocks (of {blocks} blocks; {len(diff_rows)} differing rows)")

    # Block-level navigation: jump moves to the next/prev BLOCK with any
    # difference (not to the next differing row), and tracks the current index
    # explicitly so sitting on the first diff doesn't break Next.
    idx = [-1]
    def jump(direction):
        if not diff_blocks_list: return
        idx[0] = (idx[0] + direction) % len(diff_blocks_list)
        blk = diff_blocks_list[idx[0]]
        header_line = blk * (ROWS_PER_BLOCK + 1) + 1
        frac = max(0, header_line - 1) / max(total_lines, 1)
        ta.yview_moveto(frac); tb.yview_moveto(frac)
        info.config(text=f"block {blk}  ({idx[0]+1}/{len(diff_blocks_list)} differing)")
    nxt_btn.config(command=lambda: jump(1))
    prv_btn.config(command=lambda: jump(-1))

    def do_disasm_compare():
        import sys as _sys
        _sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
        try:
            from pdp11_disasm import Disassembler
        except Exception as e:
            info.config(text=f"disasm unavailable: {e}"); return
        # clear any prior cleaner/junkier tags
        for t in (ta, tb):
            t.tag_remove("cleaner", "1.0", "end")
            t.tag_remove("junkier", "1.0", "end")
        cleaner_count = 0
        for blk in diff_blocks_list:
            sa = data_a[blk*512:(blk+1)*512]
            sb = data_b[blk*512:(blk+1)*512]
            try:
                ia = Disassembler(sa, 0).disassemble_all()
                ib = Disassembler(sb, 0).disassemble_all()
            except Exception:
                continue
            # HALT (0x0000) is technically a valid opcode but a block of all
            # HALTs is just zero-padding masquerading as code, so count it as
            # non-code together with .WORD (illegal/data words).
            wa = sum(1 for it in ia if str(it[-1]).startswith(".WORD") or str(it[-1]) == "HALT")
            wb = sum(1 for it in ib if str(it[-1]).startswith(".WORD") or str(it[-1]) == "HALT")
            ra = wa / max(len(ia), 1)
            rb = wb / max(len(ib), 1)
            if abs(ra - rb) < 0.20:
                continue                                  # too close to call
            cleaner, junkier = (ta, tb) if ra < rb else (tb, ta)
            first = blk * (ROWS_PER_BLOCK + 1) + 2        # first data line of block
            last = first + min(ROWS_PER_BLOCK, rows - blk*ROWS_PER_BLOCK) - 1
            cleaner.tag_add("cleaner", f"{first}.0", f"{last}.end")
            junkier.tag_add("junkier", f"{first}.0", f"{last}.end")
            cleaner_count += 1
        info.config(text=f"disasm: {cleaner_count} blocks classified  (green = cleaner code; red = junkier)")
    disasm_btn.config(command=do_disasm_compare)

    if diff_blocks_list:
        jump(1)                                 # auto-jump to first differing block

def hexdump(data, limit=4096):
    out = []
    for off in range(0, min(len(data), limit), 16):
        chunk = data[off:off+16]
        hexs = " ".join(f"{b:02x}" for b in chunk)
        asci = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)
        out.append(f"{off:06x}  {hexs:<47}  {asci}")
    if len(data) > limit:
        out.append(f"... ({len(data)} bytes total)")
    return "\n".join(out)

class Model:
    def __init__(self):
        self.corpus = json.load(open(OUT/"corpus.json", encoding="utf-8"))["records"]
        self.files = json.load(open(OUT/"consensus.json", encoding="utf-8"))["files"]
        cap_fp = json.load(open(OUT/"captures.json", encoding="utf-8")) \
            if (OUT/"captures.json").exists() else {}
        donor = json.load(open(OUT/"donor.json", encoding="utf-8")) \
            if (OUT/"donor.json").exists() else {"recovered": [], "corroborated": []}
        self.recovered = {(d["name"], d["blocks"]) for d in donor["recovered"]}
        self.corro = {(d["name"], d["blocks"]): d["source"] for d in donor["corroborated"]}
        # Key by the same canonical name consensus uses, so .EXE-first records
        # land in the same recs_by_key bucket as their .SAV-grouped consensus
        # record (otherwise zero-analysis / phys-disks would miss them).
        self.recs_by_key = defaultdict(list)
        for r in self.corpus:
            self.recs_by_key[(canonical_name(r["names"]), r["blocks"])].append(r)
        self.disk_of = V.physical_disks(self.corpus, cap_fp)
        self.cons_by_key = {(r["name"], r["blocks"]): r for r in self.files}
        self.chosen = V.load_decisions(V.DECISIONS, self.cons_by_key)
        self._reclassify()

    def _reclassify(self):
        for r in self.files:
            r["band"] = V.classify(r, self.recs_by_key, self.recovered,
                                   self.corro, self.disk_of, self.chosen)

    def by_band(self):
        d = defaultdict(list)
        for r in self.files:
            d[r["band"]].append(r)
        for b in d:
            d[b].sort(key=lambda r: r["name"])
        return d

    def versions(self, r):
        return V.version_disks(r)

    def content(self, sha):
        p = STORE / f"{sha}.bin"
        return p.read_bytes() if p.exists() else b""

    def set_choice(self, r, shas):
        """TOGGLE each sha in the file's canonical set: add if not present,
        remove if already canonical.  Returns (added, removed) sha lists."""
        key = (r["name"], r["blocks"])
        existing = self.chosen.get(key, [])
        if isinstance(existing, str): existing = [existing]
        combined = list(existing)
        added, removed = [], []
        for s in shas:
            if s in combined:
                combined.remove(s); removed.append(s)
            else:
                combined.append(s); added.append(s)
        if combined:
            self.chosen[key] = combined
        else:
            self.chosen.pop(key, None)
        self._save(); self._reclassify()
        return added, removed

    def clear_choice(self, r):
        self.chosen.pop((r["name"], r["blocks"]), None)
        self._save(); self._reclassify()

    def chosen_for(self, r):
        v = self.chosen.get((r["name"], r["blocks"]), [])
        return set(v) if isinstance(v, (list, set, tuple)) else {v}

    def _save(self):
        amb = [r for r in self.files if r["tier"] == "multi-version"]
        V.write_decisions(V.DECISIONS, amb, self.chosen)

def run_gui(model):
    import tkinter as tk
    from tkinter import ttk, scrolledtext

    root = tk.Tk()
    root.title("MS0515 recovery review")
    root.geometry("1200x760")
    paned = ttk.PanedWindow(root, orient="horizontal")
    paned.pack(fill="both", expand=True)

    nb = ttk.Notebook(paned)
    paned.add(nb, weight=3)
    trees = {}
    for band in V.BANDS:
        frame = ttk.Frame(nb)
        tv = ttk.Treeview(frame, columns=("blk", "vrfd", "vers", "chosen"), show="tree headings")
        tv.heading("#0", text="file"); tv.column("#0", width=170)
        tv.heading("blk", text="blk"); tv.column("blk", width=45, anchor="e")
        tv.heading("vrfd", text="vrfd"); tv.column("vrfd", width=60, anchor="e")
        tv.heading("vers", text="versions"); tv.column("vers", width=65, anchor="e")
        tv.heading("chosen", text="chosen"); tv.column("chosen", width=85)
        # files with >=1 canonical picked get the same green as chosen versions
        tv.tag_configure("has_chosen", background="#d0f0d0", foreground="#0a4f0a")
        sb = ttk.Scrollbar(frame, orient="vertical", command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.pack(side="left", fill="both", expand=True); sb.pack(side="right", fill="y")
        nb.add(frame, text=band)
        trees[band] = tv

    right = ttk.Frame(paned); paned.add(right, weight=2)
    info = ttk.Label(right, text="select a file", font=("", 10, "bold"))
    info.pack(anchor="w", padx=6, pady=4)
    ttk.Label(right, text="versions (select 1 to view / set, 2 to diff):").pack(anchor="w", padx=6)
    vbox = tk.Listbox(right, selectmode="extended", height=7, font=("Consolas", 9))
    vbox.pack(fill="x", padx=6)
    btns = ttk.Frame(right); btns.pack(fill="x", padx=6, pady=4)
    text = scrolledtext.ScrolledText(right, wrap="none", font=("Consolas", 9))
    text.pack(fill="both", expand=True, padx=6, pady=4)
    status = ttk.Label(right, text="", foreground="#066")
    status.pack(anchor="w", padx=6, pady=2)

    state = {"rec": None, "vers": []}

    def settext(s):
        text.delete("1.0", "end"); text.insert("1.0", s)

    def display_versions():
        """Redraw the right-side version list from state["rec"]/state["vers"].
        Keeps the original (unsorted) order so a version doesn't jump when its
        canonical state is toggled; chosen ones just get a green row + ✓ mark.
        Preserves the listbox selection across the redraw."""
        r = state["rec"]
        if not r:
            return
        chosen_set = model.chosen_for(r)
        cur_sel = list(vbox.curselection())
        vbox.delete(0, "end")
        for i, (sha, disks) in enumerate(state["vers"]):
            on = ", ".join(disks)
            if sha in chosen_set:
                vbox.insert("end", f"✓ {sha[:8]}  on: {on}")
                vbox.itemconfig(i, background="#d0f0d0", foreground="#0a4f0a")
            else:
                vbox.insert("end", f"{sha[:8]}  on: {on}")
        for i in cur_sel:
            if i < vbox.size():
                vbox.selection_set(i)
        canon_txt = ("  — canonical: " + "+".join(s[:8] for s in sorted(chosen_set))) if chosen_set else ""
        info.config(text=f"{r['name']}  {r['blocks']} blk  [{r['band']}]  "
                         f"{len(state['vers'])} version(s){canon_txt}")

    def on_select_file(event):
        tv = event.widget
        sel = tv.selection()
        if not sel:
            return
        idx = int(tv.item(sel[0], "tags")[0])
        r = model.files[idx]
        # When go_to() re-selects the same file in a different tab after a
        # toggle moved it between bands, do_set has already updated the right
        # panel — don't tear down state (it would lose synthetic byte-vote /
        # text-merge entries we just appended to vbox).
        if state.get("rec") is r:
            return
        state["rec"] = r
        state["vers"] = list(model.versions(r))   # original order, no reordering
        display_versions()
        settext("")
        status.config(text="")

    def go_to(r):
        """Switch to r's current-band tab and select its row, so a file that
        just moved (e.g. AMBIGUOUS -> CHOSEN after a toggle) stays on screen."""
        band = r.get("band")
        if band not in trees:
            return
        nb.select(V.BANDS.index(band))
        tv = trees[band]
        idx_str = str(model.files.index(r))
        for iid in tv.get_children():
            tags = tv.item(iid, "tags")
            if tags and tags[0] == idx_str:
                tv.selection_set(iid); tv.focus(iid); tv.see(iid)
                break

    for tv in trees.values():
        tv.bind("<<TreeviewSelect>>", on_select_file)

    def selected_versions():
        return [state["vers"][i] for i in vbox.curselection()]

    def do_view():
        v = selected_versions()
        if not v:
            status.config(text="select one version"); return
        data = model.content(v[0][0])
        t = as_text(data)
        settext(t if t is not None else hexdump(data))
        status.config(text=f"{v[0][0][:8]}  {len(data)} bytes")

    def do_diff():
        v = selected_versions()
        if len(v) != 2:
            status.config(text="select exactly two versions"); return
        a, b = model.content(v[0][0]), model.content(v[1][0])
        if a == b:
            t = as_text(a)
            settext(t if t is not None else hexdump(a))
            status.config(text=f"versions {v[0][0][:8]} and {v[1][0][:8]} are IDENTICAL "
                               f"({len(a)} bytes) — content shown above")
            return
        r = state["rec"]; nm = r["name"] if r else "?"
        hex_diff_window(root, nm, v[0][0], a, v[1][0], b,
                        model=model, record=r, on_refresh=refresh)
        total = sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i])
        status.config(text=f"hex diff popup opened ({total} byte(s) differ)")

    def do_bytevote():
        v = selected_versions()
        if len(v) < 2:
            status.config(text="select >=2 versions (you judge SAME build) to byte-vote"); return
        datas = [model.content(s) for s, _ in v]
        if len({len(d) for d in datas}) != 1:
            status.config(text="versions differ in length — cannot byte-vote"); return
        # weight each variant by how many distinct physical disks back it
        weights = [max(1, len(disks)) for _, disks in v]
        sha, vote_status = V.store_voted(datas, weights)
        input_shas = {s for s, _ in v}
        matches_existing = sha in input_shas
        state["vers"].append((sha, [f"byte-vote of {len(v)}"]))
        vbox.insert("end", f"{sha[:8]}  on: (byte-vote of {len(v)})")
        vbox.selection_clear(0, "end"); vbox.selection_set("end")
        voted = model.content(sha)
        t = as_text(voted)
        settext(t if t is not None else hexdump(voted))
        eq_note = " (= existing variant)" if matches_existing else " (synthetic)"
        status.config(text=f"byte-vote -> {sha[:8]}{eq_note}; {vote_status}")

    def do_zeroes():
        r = state["rec"]
        if not r:
            status.config(text="select a file first"); return
        key = (r["name"], r["blocks"])
        recs = model.recs_by_key.get(key, [])
        if not recs:
            status.config(text="no corpus records for this file"); return
        BLK, ZERO = 512, b"\x00"*512
        nblk = r["blocks"]
        # gather per-variant: data + per-capture (status, bad set)
        vars_ = []
        for cr in recs:
            d = model.content(cr["sha"])
            caps = [(p["capture"], p.get("status", False), set(p.get("bad", [])))
                    for p in cr["provenance"]]
            vars_.append({"sha": cr["sha"], "data": d, "caps": caps})
        lines = [f"Zero-block analysis: {r['name']}  ({nblk} blocks, {len(vars_)} raw variants)", ""]
        lost = nat = contr = 0
        for b in range(nblk):
            zeros = [v for v in vars_ if v["data"][b*BLK:(b+1)*BLK] == ZERO]
            datas = [v for v in vars_ if v not in zeros]
            if not zeros:
                continue
            if not datas:                                      # all-zero everywhere
                any_flagged = any(b in bad for v in vars_ for _, _, bad in v["caps"])
                if any_flagged:
                    lines.append(f"  block {b:4d}: all-zero on every variant, BUT some captures flagged it — could be wholly lost")
                    contr += 1
                else:
                    nat += 1
                continue
            # both data and zero exist for this block: classify each zero-variant
            lines.append(f"  block {b:4d}: {len(zeros)} zero / {len(datas)} data")
            for zv in zeros:
                fl = [c for c, _, bad in zv["caps"] if b in bad]
                cl = [c for c, st, bad in zv["caps"] if st and b not in bad]
                nr = [c for c, st, bad in zv["caps"] if not st]
                if fl:
                    verdict = f"FLAGGED on {fl[0]} -> LOST (CRC failed, zero-fill); others have data, take data"
                    lost += 1
                elif cl and not fl:
                    verdict = f"CRC-clean on {cl[0]} yet zero & others differ -> CONTRADICTION (edit? false-clean?)"
                    contr += 1
                else:
                    verdict = f"raw on {(nr or ['?'])[0]}, no status; others have data -> LIKELY LOST"
                    lost += 1
                lines.append(f"      {zv['sha'][:8]}: {verdict}")
        lines += ["",
                  f"summary: {lost} likely-LOST zeros, {nat} likely-NATURAL (all-zero everywhere clean), {contr} contradictions"]
        settext("\n".join(lines))
        status.config(text=f"zero analysis: {lost} LOST, {nat} NATURAL, {contr} contradiction")

    def do_disasm():
        v = selected_versions()
        if len(v) < 2:
            status.config(text="select >=2 versions to compare PDP-11 disassembly"); return
        import sys as _sys
        _sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
        try:
            from pdp11_disasm import Disassembler
        except Exception as e:
            status.config(text=f"pdp11_disasm not available: {e}"); return
        datas = [(s, model.content(s)) for s, _ in v]
        if len({len(d[1]) for d in datas}) != 1:
            status.config(text="versions differ in length"); return
        n = len(datas[0][1]) // 512
        lines = [f"PDP-11 disasm comparison: {n} blocks, {len(datas)} versions",
                 "( .WORD ratio = illegal/data words; lower = looks like code )", ""]
        shown = 0
        for b in range(n):
            segs = [d[1][b*512:(b+1)*512] for d in datas]
            if len(set(segs)) == 1:
                continue
            scores = []
            for sha, full in datas:
                seg = full[b*512:(b+1)*512]
                try:
                    items = Disassembler(seg, 0).disassemble_all()
                    wcnt = sum(1 for it in items if str(it[-1]).startswith(".WORD") or str(it[-1]) == "HALT")
                    scores.append((sha, wcnt, len(items)))
                except Exception:
                    scores.append((sha, None, 0))
            parts = "  ".join(f"{s[:8]}={(w/t):.0%}" if w is not None and t
                              else f"{s[:8]}=?" for s, w, t in scores)
            valid = [(s, w/t) for s, w, t in scores if w is not None and t]
            hint = ""
            if len(valid) >= 2:
                lo = min(valid, key=lambda x: x[1]); hi = max(valid, key=lambda x: x[1])
                if hi[1] - lo[1] > 0.20:
                    hint = f"  -> {lo[0][:8]} cleanest; {hi[0][:8]} likely garbage"
            lines.append(f"block {b:4d}: {parts}{hint}")
            shown += 1
            if shown >= 60:
                lines.append("(stopped at 60 differing blocks)"); break
        if shown == 0:
            lines.append("no differing blocks")
        settext("\n".join(lines))
        status.config(text=f"disasm compare: {shown} differing blocks analysed")

    def do_textmerge():
        v = selected_versions()
        r = state["rec"]
        if not r or len(v) < 2:
            status.config(text="select a file and >=2 versions to text-merge"); return
        if r.get("is_binary") or r.get("category") not in ("text", "other"):
            status.config(text=f"{r['name']} is {r.get('category','?')} — text-merge prefers readable bytes "
                               "and would corrupt a binary file; use Byte-vote sel instead"); return
        datas = [model.content(s) for s, _ in v]
        if len({len(d) for d in datas}) != 1:
            status.config(text="versions differ in length — cannot text-merge"); return
        merged, rescued = V.block_merge(datas)
        sha = V.store_content(merged)
        state["vers"].append((sha, [f"text-merge of {len(v)}"]))
        vbox.insert("end", f"{sha[:8]}  on: (text-merge of {len(v)}; {rescued} bytes rescued)")
        vbox.selection_clear(0, "end"); vbox.selection_set("end")
        t = as_text(merged)
        settext(t if t is not None else hexdump(merged))
        status.config(text=f"text-merge -> {sha[:8]}; {rescued} bytes taken from a readable copy; review, then Set canonical")

    def refresh():
        bands = model.by_band()
        for band, tv in trees.items():
            tv.delete(*tv.get_children())
            for r in bands.get(band, []):
                key = (r["name"], r["blocks"])
                cl = model.chosen.get(key, [])
                if isinstance(cl, str): cl = [cl]
                if not cl:        ch_disp = ""
                elif len(cl) == 1: ch_disp = cl[0][:8]
                else:              ch_disp = f"{cl[0][:6]}+{len(cl)-1}"
                idx = model.files.index(r)
                row_tags = [str(idx)]
                if cl:
                    row_tags.append("has_chosen")          # paint row green
                tv.insert("", "end", text=r["name"],
                          values=(r["blocks"],
                                  f"{r.get('verified_blocks', 0)}/{r['blocks']}",
                                  len(model.versions(r)), ch_disp),
                          tags=tuple(row_tags))
            ti = V.BANDS.index(band)
            nb.tab(ti, text=f"{band} ({len(bands.get(band, []))})")

    def do_set():
        v = selected_versions()
        r = state["rec"]
        if not r or not v:
            status.config(text="select a file and >=1 version to toggle canonical"); return
        added, removed = model.set_choice(r, [s for s, _ in v])
        refresh()                 # repaint left tree (band + chosen column + row colour)
        display_versions()        # repaint vbox marks/colours for this file
        go_to(r)                  # follow the file to whatever band it lives in now
        cur = list(model.chosen.get((r["name"], r["blocks"]), []))
        bits = []
        if added:   bits.append("+" + "+".join(s[:8] for s in added))
        if removed: bits.append("-" + "-".join(s[:8] for s in removed))
        status.config(text=f"{r['name']}: " + "  ".join(bits)
                           + f"   -> {len(cur)} canonical now"
                           + (": " + "+".join(s[:8] for s in cur) if cur else ""))

    def do_clear():
        r = state["rec"]
        if not r:
            return
        model.clear_choice(r)
        refresh()
        display_versions()
        go_to(r)
        status.config(text=f"cleared {r['name']}")

    ttk.Button(btns, text="Diff 2", command=do_diff).pack(side="left", padx=4)
    ttk.Button(btns, text="Byte-vote sel", command=do_bytevote).pack(side="left")
    ttk.Button(btns, text="Text-merge sel", command=do_textmerge).pack(side="left", padx=4)
    ttk.Button(btns, text="✓ Toggle canonical", command=do_set).pack(side="left", padx=4)
    ttk.Button(btns, text="Clear all", command=do_clear).pack(side="left")

    def on_version_select(event):
        sel = vbox.curselection()
        if len(sel) != 1:
            return                              # don't auto-view on multi-select
        sha = state["vers"][sel[0]][0]
        data = model.content(sha)
        t = as_text(data)
        settext(t if t is not None else hexdump(data))
        status.config(text=f"viewing {sha[:8]}  ({len(data)} bytes)")
    vbox.bind("<<ListboxSelect>>", on_version_select)

    refresh()
    try:
        amb_i = V.BANDS.index("AMBIGUOUS")
        nb.select(amb_i)
    except Exception:
        pass
    root.mainloop()

def main():
    model = Model()
    if "--selftest" in sys.argv:
        bands = model.by_band()
        for b in V.BANDS:
            print(f"  {b:11s} {len(bands.get(b, [])):4d}")
        amb = bands.get("AMBIGUOUS", [])
        if amb:
            r = amb[0]
            print("sample:", r["name"], r["blocks"], "->", model.versions(r))
        return
    try:
        run_gui(model)
    except Exception as e:
        sys.exit(f"GUI failed to start ({e}); tkinter available?")

if __name__ == "__main__":
    main()
