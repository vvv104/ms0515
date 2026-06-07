"""
rt11 — RT-11 monitor session helpers on top of EmulatorDriver.

Wraps the "send a command, wait for the dot prompt, return the captured
output" pattern.  Handles the Russian localisation's Date/Time/Startup
prompts at boot, the ASSIGN dance, and common build workflows
(``MACRO``, ``PAS1``, ``LINK``).

Use this for any project being built on RT-11 inside the emulator, not
just MACRO-11.  The session has no opinion on the language of the source.

Usage
-----
    from emu_driver import EmulatorDriver
    from rt11 import RT11Session

    emu = EmulatorDriver([cli, "--rom", rom, "--disk0", boot, "--disk1", work])
    emu.start()
    try:
        rt = RT11Session(emu)
        rt.boot()
        rt.assign("DZ1", "DK")
        rt.command("PAS1 MYPROG", timeout=300)
        rt.command("MACRO MYPROG", timeout=300)
        rt.command("LINK MYPROG,PASLIB,PAS1", timeout=300)
    finally:
        emu.kill()
"""

from __future__ import annotations

import re
import time
from typing import Iterable

from emu_driver import EmulatorDriver


# The dot prompt at the start of a line is the only universal sign that the
# monitor is ready for input.  We require it to sit alone with no following
# output for the configured idle window.
DOT_PROMPT = re.compile(r"\.\s*$")


class RT11Session:
    """A scripted RT-11 monitor session.

    All ``command`` calls send the line, wait for a fresh prompt, and return
    the captured output between the previous prompt and the new one (so
    callers can grep it for errors).
    """

    def __init__(self, emu: EmulatorDriver) -> None:
        self.emu = emu

    # ── boot ────────────────────────────────────────────────────────────────

    def boot(self, *, timeout: float = 60.0, send_returns: int = 3,
             return_delay: float = 0.4) -> None:
        """Wait for the monitor prompt.

        Many localised RT-11 builds (e.g. the Soviet V5.04 on the VVV disks)
        ask for Date, Time and a Startup command before showing the prompt.
        We send ``send_returns`` blank Returns spaced by ``return_delay`` to
        accept the defaults, then wait for the first dot prompt.
        """
        time.sleep(2.0)
        for _ in range(send_returns):
            self.emu.send("\r")
            time.sleep(return_delay)
        self.emu.wait_for(DOT_PROMPT, "monitor prompt", timeout=timeout)

    # ── command primitive ──────────────────────────────────────────────────

    def command(self, line: str, *, timeout: float = 60.0,
                ignore_errors: bool = False) -> str:
        """Send *line* followed by CR, wait for the prompt, return new output.

        On match the substring scanned is the buffer accumulated since this
        call started, so callers see only their own command's output.
        Raises ``RT11CommandError`` if RT-11 prints a ``?xxx-F-...`` fatal
        diagnostic, unless ``ignore_errors`` is true.
        """
        marker = self.emu.buffer_len()
        self.emu.send(line + "\r")
        self.emu.wait_for(DOT_PROMPT, f"command {line!r}", timeout=timeout)
        # Pull the new output relative to where we started.
        with self.emu._buf_lock:
            raw = bytes(self.emu._buf[marker:])
        text = self.emu._decode(raw)
        if not ignore_errors:
            err = re.search(r"\?[A-Z]{2,5}-F-[^\r\n.]+", text)
            if err:
                raise RT11CommandError(line, err.group(0), text)
        return text

    # ── high-level shortcuts ────────────────────────────────────────────────

    def assign(self, logical: str, physical: str) -> None:
        self.command(f"ASSIGN {logical} {physical}", timeout=15)

    def deassign(self, logical: str) -> None:
        self.command(f"DEASSIGN {logical}", timeout=15)

    def run(self, name: str, *, timeout: float = 120.0) -> str:
        return self.command(f"RUN {name}", timeout=timeout)

    def chain(self, commands: Iterable[str], *, timeout: float = 300.0) -> list[str]:
        return [self.command(c, timeout=timeout) for c in commands]


class RT11CommandError(RuntimeError):
    def __init__(self, command: str, error_line: str, full_output: str) -> None:
        super().__init__(f"{command!r}: {error_line}")
        self.command = command
        self.error_line = error_line
        self.full_output = full_output
