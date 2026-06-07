"""Combine the shipping data files for SAPER.

Inputs (in this directory):
    K.HLP     encrypted help text (original Pascal SAPER)
    NSEQ.BIN  FORLIB RAN() noise that decrypts K.HLP
    K.DAT     sprite atlas

Outputs (one level up, alongside SAPER.SAV):
    SAPER.HLP   = K.HLP[:4096] + NSEQ.BIN[:3680] padded to 8192 (16 blocks)
    SAPER.DAT   = K.DAT verbatim
"""
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT  = HERE.parent

khlp = (HERE / "K.HLP").read_bytes()
nseq = (HERE / "NSEQ.BIN").read_bytes()
combined = khlp[:4096] + nseq[:3680]
combined = combined + b"\x00" * (8192 - len(combined))
(OUT / "SAPER.HLP").write_bytes(combined)
(OUT / "SAPER.DAT").write_bytes((HERE / "K.DAT").read_bytes())
print(f"SAPER.HLP: {len(combined)} bytes (16 blocks); SAPER.DAT copied from K.DAT")
