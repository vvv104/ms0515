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

def hex_diff_window(parent, name, sha_a, data_a, sha_b, data_b):
    """Side-by-side hex diff popup with synchronised scrolling and red-highlight
    on differing bytes (both hex and ASCII columns).  Next/Prev jump between
    differing rows."""
    import tkinter as tk
    from tkinter import ttk
    BPR = 16
    n = max(len(data_a), len(data_b))
    rows = (n + BPR - 1) // BPR
    diff_rows = []

    win = tk.Toplevel(parent)
    win.title(f"Compare: {name}  —  {sha_a[:8]} vs {sha_b[:8]}")
    win.geometry("1500x820")

    bar = ttk.Frame(win); bar.pack(fill="x", padx=4, pady=2)
    info = ttk.Label(bar, text="rendering...", font=("", 9))
    info.pack(side="left", padx=8)
    nxt_btn = ttk.Button(bar, text="Next diff ↓", width=14); nxt_btn.pack(side="right", padx=2)
    prv_btn = ttk.Button(bar, text="Prev diff ↑", width=14); prv_btn.pack(side="right", padx=2)

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
        for r in range(rows):
            off = r * BPR
            seg = data[off:off+BPR]; oth = other[off:off+BPR]
            t.insert("end", build_line(off, seg, len(oth)))
            ln = r + 1
            t.tag_add("addr", f"{ln}.0", f"{ln}.10")
            for i in range(BPR):
                a_has = i < len(seg); b_has = i < len(oth)
                if a_has and b_has and seg[i] == oth[i]:
                    continue
                if not a_has and not b_has:
                    continue
                hpos = 10 + i*3 + (1 if i >= 8 else 0)
                t.tag_add("diff", f"{ln}.{hpos}", f"{ln}.{hpos+2}")
                apos = 60 + i               # 10 addr + 49 hex + 1 sep
                t.tag_add("diff", f"{ln}.{apos}", f"{ln}.{apos+1}")
    render(ta, data_a, data_b)
    render(tb, data_b, data_a)
    for r in range(rows):
        off = r*BPR
        if data_a[off:off+BPR] != data_b[off:off+BPR]:
            diff_rows.append(r)
    for t in (ta, tb): t.configure(state="disabled")
    info.config(text=f"{len(diff_rows)} differing rows of {rows} (BPR={BPR})")

    def jump(direction):
        if not diff_rows or rows == 0: return
        cur = int(ta.yview()[0] * rows)
        if direction > 0:
            nxt = next((r for r in diff_rows if r > cur), diff_rows[0])
        else:
            nxt = next((r for r in reversed(diff_rows) if r < cur), diff_rows[-1])
        frac = nxt / rows if rows else 0
        ta.yview_moveto(frac); tb.yview_moveto(frac)
    nxt_btn.config(command=lambda: jump(1))
    prv_btn.config(command=lambda: jump(-1))
    if diff_rows:
        jump(1)                                 # auto-jump to first difference

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
        self.recs_by_key = defaultdict(list)
        for r in self.corpus:
            self.recs_by_key[(r["names"][0], r["blocks"])].append(r)
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

    def set_choice(self, r, sha):
        self.chosen[(r["name"], r["blocks"])] = sha
        self._save(); self._reclassify()

    def clear_choice(self, r):
        self.chosen.pop((r["name"], r["blocks"]), None)
        self._save(); self._reclassify()

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

    def on_select_file(event):
        tv = event.widget
        sel = tv.selection()
        if not sel:
            return
        rec = tv.item(sel[0], "values")  # we stash index in tags
        idx = int(tv.item(sel[0], "tags")[0])
        r = model.files[idx]
        state["rec"] = r
        state["vers"] = model.versions(r)
        vbox.delete(0, "end")
        for sha, disks in state["vers"]:
            mark = " <= chosen" if model.chosen.get((r["name"], r["blocks"])) == sha else ""
            vbox.insert("end", f"{sha[:8]}  on: {', '.join(disks)}{mark}")
        info.config(text=f"{r['name']}  {r['blocks']} blk  [{r['band']}]  "
                         f"{len(state['vers'])} version(s)")
        settext("")
        status.config(text="")

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
        ta, tb = as_text(a), as_text(b)
        if ta is not None and tb is not None:
            d = list(difflib.unified_diff(ta.splitlines(), tb.splitlines(),
                                          v[0][0][:8], v[1][0][:8], lineterm=""))
            settext("\n".join(d) if d else "(identical as text)")
            status.config(text=f"text diff {v[0][0][:8]} vs {v[1][0][:8]}")
        else:
            r = state["rec"]; nm = r["name"] if r else "?"
            hex_diff_window(root, nm, v[0][0], a, v[1][0], b)
            total = sum(1 for i in range(min(len(a), len(b))) if a[i] != b[i])
            status.config(text=f"hex diff popup opened ({total} byte(s) differ)")

    def do_bytevote():
        v = selected_versions()
        if len(v) < 2:
            status.config(text="select >=2 versions (you judge SAME build) to byte-vote"); return
        datas = [model.content(s) for s, _ in v]
        if len({len(d) for d in datas}) != 1:
            status.config(text="versions differ in length — cannot byte-vote"); return
        sha = V.store_voted(datas)
        state["vers"].append((sha, [f"byte-vote of {len(v)}"]))
        vbox.insert("end", f"{sha[:8]}  on: (byte-vote of {len(v)})")
        vbox.selection_clear(0, "end"); vbox.selection_set("end")
        voted = model.content(sha)
        t = as_text(voted)
        settext(t if t is not None else hexdump(voted))
        status.config(text=f"byte-vote -> {sha[:8]} of {len(v)} versions; review, then Set canonical")

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
                ch = model.chosen.get(key, "")
                idx = model.files.index(r)
                tv.insert("", "end", text=r["name"],
                          values=(r["blocks"],
                                  f"{r.get('verified_blocks', 0)}/{r['blocks']}",
                                  len(model.versions(r)), ch[:8]),
                          tags=(str(idx),))
            ti = V.BANDS.index(band)
            nb.tab(ti, text=f"{band} ({len(bands.get(band, []))})")

    def do_set():
        v = selected_versions()
        r = state["rec"]
        if not r or len(v) != 1:
            status.config(text="select one file and one version"); return
        model.set_choice(r, v[0][0])
        refresh()
        status.config(text=f"set {r['name']} -> {v[0][0][:8]} (written to decisions.tsv)")

    def do_clear():
        r = state["rec"]
        if not r:
            return
        model.clear_choice(r)
        refresh()
        status.config(text=f"cleared {r['name']}")

    ttk.Button(btns, text="View", command=do_view).pack(side="left")
    ttk.Button(btns, text="Diff 2", command=do_diff).pack(side="left", padx=4)
    ttk.Button(btns, text="Byte-vote sel", command=do_bytevote).pack(side="left")
    ttk.Button(btns, text="✓ Set canonical", command=do_set).pack(side="left", padx=4)
    ttk.Button(btns, text="Clear", command=do_clear).pack(side="left")

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
