"""Tests for ``emu_driver``: the stdio bridge against a fake child process.

We don't need the real emulator to exercise the EmulatorDriver state
machine.  A small Python child that echoes/sleeps on demand is enough
to cover wait_for, idle detection, encoding and timeout behaviour.
"""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from emu_driver import EmulatorDriver       # noqa: E402


def _echo_child_script(timing_script: str) -> str:
    """Return a small Python program that reads a one-line "command" and
    emits the configured timing_script (sequence of "delay,bytes" tokens).

    The driver tests can therefore set up exactly how the child responds
    to ``send``.  Stdout is unbuffered so the parent sees writes instantly.
    """
    return f"""
import sys, time
sys.stdout.reconfigure(line_buffering=False, write_through=True)
TIMING = {timing_script!r}
while True:
    line = sys.stdin.readline()
    if not line:
        break
    for chunk in TIMING.split('|'):
        if not chunk: continue
        delay_s, payload = chunk.split(',', 1)
        time.sleep(float(delay_s))
        sys.stdout.buffer.write(payload.encode('latin-1'))
        sys.stdout.buffer.flush()
"""


def _spawn_child(timing_script: str) -> EmulatorDriver:
    return EmulatorDriver(
        [sys.executable, "-c", _echo_child_script(timing_script)],
        encoding="latin-1",
    )


class TestBasicIO:
    def test_send_and_capture(self):
        with _spawn_child("0.0,HELLO\n") as emu:
            emu.send("go\n")
            emu.wait_for("HELLO", "got HELLO", timeout=5, idle=0.2)
            assert "HELLO" in emu.tail(64)

    def test_tail_returns_decoded_text(self):
        with _spawn_child("0.0,Voronkov 1995\n") as emu:
            emu.send("\n")
            emu.wait_for("Voronkov", "label", timeout=5, idle=0.2)
            assert "Voronkov" in emu.tail(64)

    def test_buffer_len_grows(self):
        with _spawn_child("0.0,ABCDEFGHIJ\n") as emu:
            emu.send("\n")
            time.sleep(0.5)
            assert emu.buffer_len() >= 10


class TestWaitForIdle:
    def test_holds_until_child_goes_silent(self):
        # Child emits PROMPT immediately, then 1.5s later spits trailing
        # output.  A naive search would fire on the first PROMPT, but
        # wait_for(idle=0.5) should keep waiting until the silence sticks.
        with _spawn_child("0.0,PROMPT|0.4,more\n|0.4,more again\n") as emu:
            emu.send("\n")
            emu.wait_for(r"PROMPT", "prompt", timeout=5, idle=0.5)
            tail = emu.tail(256)
            assert "more again" in tail

    def test_raises_on_timeout(self):
        with _spawn_child("2.0,never_arrives\n") as emu:
            emu.send("\n")
            with pytest.raises(TimeoutError) as exc_info:
                emu.wait_for("MATCH_ME", "miss", timeout=0.3, idle=0.1)
            assert "MATCH_ME" in str(exc_info.value) or "miss" in str(exc_info.value)


class TestPatternForms:
    def test_accepts_compiled_regex(self):
        with _spawn_child("0.0,line one\nline two\n") as emu:
            emu.send("\n")
            emu.wait_for(re.compile(r"line\s+two"), "regex object",
                         timeout=5, idle=0.2)

    def test_strips_ansi_in_decoded_view(self):
        with _spawn_child("0.0,\x1b[2J\x1b[1;1HBANNER\n") as emu:
            emu.send("\n")
            emu.wait_for("BANNER", "banner", timeout=5, idle=0.2)
            assert "BANNER" in emu.tail(64)
            # The ANSI cursor positioning escape MUST be stripped from
            # the *decoded* view.
            assert "\x1b" not in emu.tail(64)


class TestDump:
    def test_writes_full_buffer_to_disk(self, tmp_path):
        with _spawn_child("0.0,DUMPME\n") as emu:
            emu.send("\n")
            emu.wait_for("DUMPME", "wait", timeout=5, idle=0.2)
            out = tmp_path / "log.bin"
            n = emu.dump(out)
            assert n >= 6
            assert b"DUMPME" in out.read_bytes()


class TestLifecycle:
    def test_double_start_raises(self):
        emu = _spawn_child("0.0,X")
        emu.start()
        try:
            with pytest.raises(RuntimeError):
                emu.start()
        finally:
            emu.kill()

    def test_send_before_start_raises(self):
        emu = _spawn_child("0.0,X")
        with pytest.raises(RuntimeError):
            emu.send("anything")
