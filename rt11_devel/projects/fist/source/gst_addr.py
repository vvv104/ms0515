"""The game-state mirror: GST stands for Spectrum $9C00 (see LAYOUT.md)."""

GBASE = 0x9C00                       # GST stands for this Spectrum address


def g(addr, reg=None):
    """MACRO-11 operand for Spectrum address `addr` in GST - relative if `reg`
    is None, else indexed `GST+off(reg)` (used for the fighter selector C and
    the table-index registers)."""
    off = f"GST+{addr - GBASE}."
    return f"{off}({reg})" if reg else off
