"""
emu_driver — drive a stdio emulator subprocess from the host.

Generic over the emulator: works for ms0515-cli.exe, but also any other
process that takes commands on stdin and writes to stdout/stderr.

Usage
-----
    from emu_driver import EmulatorDriver

    emu = EmulatorDriver([
        "C:/path/to/ms0515-cli.exe",
        "--rom", "C:/.../ms0515-roma.rom",
        "--disk0", "C:/.../boot.dsk",
    ])
    emu.start()
    try:
        emu.send("\\r"); emu.wait_for(r"\\.\\s*$", "monitor prompt", timeout=30)
        emu.send("DIR\\r"); emu.wait_for(r"\\.\\s*$", "DIR done")
        print(emu.tail(2000))
    finally:
        emu.kill()
"""

from __future__ import annotations

import re
import subprocess
import sys
import threading
import time
from pathlib import Path


class EmulatorDriver:
    """A pty-less stdio bridge to a long-running subprocess.

    Captures everything the child writes to stdout (and stderr, merged) into a
    rolling byte buffer.  Callers send raw bytes to the child's stdin via
    ``send`` and wait for substrings or regex patterns via ``wait_for``.

    Output is treated as bytes; ``tail`` and ``wait_for`` decode with a
    user-chosen codec (default cp866 for Soviet-era OS output).
    """

    def __init__(
        self,
        cmd: list[str | Path],
        *,
        cwd: str | Path | None = None,
        env: dict[str, str] | None = None,
        encoding: str = "cp866",
    ) -> None:
        self.cmd = [str(c) for c in cmd]
        self.cwd = str(cwd) if cwd else None
        self.env = env
        self.encoding = encoding
        self._proc: subprocess.Popen | None = None
        self._buf = bytearray()
        self._buf_lock = threading.Lock()
        self._reader: threading.Thread | None = None

    # ── lifecycle ────────────────────────────────────────────────────────────

    def start(self) -> None:
        if self._proc is not None:
            raise RuntimeError("EmulatorDriver already started")
        self._proc = subprocess.Popen(
            self.cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
            cwd=self.cwd,
            env=self.env,
        )
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def kill(self) -> None:
        if self._proc and self._proc.poll() is None:
            self._proc.kill()
            try:
                self._proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                pass

    def __enter__(self) -> "EmulatorDriver":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.kill()

    # ── I/O primitives ───────────────────────────────────────────────────────

    def send(self, data: str | bytes) -> None:
        """Write data to the child's stdin.

        Strings are encoded with the driver's text codec.  Pass bytes to send
        raw escape sequences (F-keys, control chars).
        """
        if self._proc is None or self._proc.stdin is None:
            raise RuntimeError("emulator not running")
        if isinstance(data, str):
            data = data.encode(self.encoding, errors="replace")
        self._proc.stdin.write(data)
        self._proc.stdin.flush()

    def buffer_len(self) -> int:
        with self._buf_lock:
            return len(self._buf)

    def tail(self, n: int = 4096) -> str:
        """Return the last *n* bytes of captured output as decoded text."""
        with self._buf_lock:
            chunk = bytes(self._buf[-n:])
        return self._decode(chunk)

    def dump(self, path: str | Path) -> int:
        """Persist the entire captured output to *path*.  Returns size."""
        with self._buf_lock:
            data = bytes(self._buf)
        Path(path).write_bytes(data)
        return len(data)

    # ── pattern-driven waits ─────────────────────────────────────────────────

    def wait_for(
        self,
        pattern: str | re.Pattern,
        label: str,
        *,
        timeout: float = 60.0,
        window: int = 4096,
        idle: float = 0.6,
        progress_every: float = 10.0,
    ) -> float:
        """Block until *pattern* matches the tail of the output and the child
        has gone quiet for *idle* seconds.

        The two-condition wait (match + idle) catches the common "prompt is
        echoed before the next line of output arrives" race: a naïve match on
        the prompt fires too early when the child is mid-print.

        Returns the elapsed seconds.  Raises ``TimeoutError`` on failure and
        leaves the buffer untouched so the caller can inspect it.
        """
        rx = re.compile(pattern) if isinstance(pattern, str) else pattern
        start = time.monotonic()
        last_len = self.buffer_len()
        idle_since: float | None = None
        next_progress = start + progress_every

        while True:
            now = time.monotonic()
            if now - start > timeout:
                raise TimeoutError(f"timeout waiting for {label!r} after {timeout:.1f}s")

            cur_len = self.buffer_len()
            text = self.tail(window)
            matched = bool(rx.search(text))

            if cur_len != last_len:
                last_len = cur_len
                idle_since = None
            elif matched and idle_since is None:
                idle_since = now

            if matched and idle_since is not None and now - idle_since >= idle:
                return now - start

            if now >= next_progress:
                sys.stderr.write(f"  [{label}] waiting ({int(now-start)}s)\n")
                next_progress = now + progress_every

            time.sleep(0.1)

    def expect(self, pattern: str | re.Pattern, *, timeout: float = 5.0) -> str:
        """Quick alias for "wait_for" without the idle requirement, returning
        the matched substring."""
        rx = re.compile(pattern) if isinstance(pattern, str) else pattern
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            text = self.tail(4096)
            m = rx.search(text)
            if m:
                return m.group(0)
            time.sleep(0.05)
        raise TimeoutError(f"expect({pattern!r}) timed out")

    # ── internal ─────────────────────────────────────────────────────────────

    def _read_loop(self) -> None:
        assert self._proc and self._proc.stdout
        while True:
            try:
                chunk = self._proc.stdout.read(1)
            except Exception:
                return
            if not chunk:
                return
            with self._buf_lock:
                self._buf.append(chunk[0])

    def _decode(self, raw: bytes) -> str:
        text = raw.decode(self.encoding, errors="replace")
        text = re.sub(r"\x1b\[[0-9;?]*[a-zA-Z]", "", text)
        text = re.sub(r"\x1b\][^\x07]*\x07", "", text)
        return text


# ── escape-sequence helpers (terminal → emulator key) ────────────────────────
#
# When the emulator's CLI brings up a vt-100-ish terminal, F-keys are sent via
# CSI/SS3 sequences.  Centralise them here so callers don't need to remember.

ESC = b"\x1b"
FKEY_CSI = {n: ESC + b"[" + str(11 + (n if n <= 5 else n + 1)).encode() + b"~"
            for n in range(1, 13)}      # F1=ESC[11~, F6=ESC[17~ (vt220 gap)
# Re-do the F-key map by hand: vt220 uses 11,12,13,14,15,17,18,19,20,21,23,24
_FKEY_NUMS = [11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 23, 24]
FKEY_CSI = {i + 1: ESC + f"[{n}~".encode() for i, n in enumerate(_FKEY_NUMS)}
FKEY_SS3 = {  # xterm SS3 form, valid for F1..F4 only
    1: ESC + b"OP", 2: ESC + b"OQ", 3: ESC + b"OR", 4: ESC + b"OS",
}
ARROW_UP    = ESC + b"[A"
ARROW_DOWN  = ESC + b"[B"
ARROW_RIGHT = ESC + b"[C"
ARROW_LEFT  = ESC + b"[D"
