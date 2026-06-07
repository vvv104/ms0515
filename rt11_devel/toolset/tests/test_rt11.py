"""Tests for the ``rt11`` session helpers (parsing + command framing).

These tests use a fake child that pretends to be the RT-11 monitor so
we can verify the RT11Session control flow without needing the emulator.
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from emu_driver import EmulatorDriver       # noqa: E402
from rt11 import RT11Session, RT11CommandError, DOT_PROMPT  # noqa: E402


# A fake monitor: prints a dot prompt, then for every line of input echoes a
# fixed response (driven by an env-style script).  Lines are CR-terminated to
# match what RT11Session.boot/command actually sends — Python's readline()
# treats CR (\r) as a line ending only when followed by \n, so we accumulate
# bytes manually here and split on either delimiter.
def _fake_monitor(script: dict[str, str]) -> str:
    return f"""
import sys, time
sys.stdout.reconfigure(line_buffering=False, write_through=True)
script = {script!r}
# Initial dot prompt: the boot path sends a few CRs and then expects a
# prompt.  Real RT-11 also asks Date/Time/Startup first; we skip that
# here so the test child can be written portably across platforms.
sys.stdout.write('.\\n')
sys.stdout.flush()
buf = bytearray()
while True:
    b = sys.stdin.buffer.read(1)
    if not b: break
    if b in (b'\\r', b'\\n'):
        cmd = buf.decode('latin-1').strip().upper()
        buf.clear()
        if not cmd:
            continue
        response = script.get(cmd, '')
        if response:
            sys.stdout.write(response + '\\n')
        sys.stdout.write('.\\n')
        sys.stdout.flush()
    else:
        buf.extend(b)
"""


def _spawn_monitor(script: dict[str, str]) -> EmulatorDriver:
    return EmulatorDriver([sys.executable, "-c", _fake_monitor(script)],
                          encoding="latin-1")


class TestBoot:
    def test_accepts_default_date_time_startup_then_lands_at_prompt(self):
        with _spawn_monitor({}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, send_returns=3, return_delay=0.1)
            # Sanity: a subsequent command works.
            out = rt.command("NOOP", timeout=5)
            assert ".\n" in out or ".\r" in out


class TestCommand:
    def test_returns_only_new_output(self):
        with _spawn_monitor({"DIR": "FOO.SAV  10 blocks"}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, return_delay=0.1)
            out = rt.command("DIR", timeout=5)
            assert "FOO.SAV" in out

    def test_raises_on_fatal_error(self):
        with _spawn_monitor({"BAD": "?KMON-F-Command not understood"}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, return_delay=0.1)
            with pytest.raises(RT11CommandError) as exc:
                rt.command("BAD", timeout=5)
            assert "KMON-F" in str(exc.value)
            assert exc.value.command == "BAD"

    def test_ignore_errors_swallows_diagnostic(self):
        with _spawn_monitor({"BAD": "?KMON-F-Command not understood"}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, return_delay=0.1)
            out = rt.command("BAD", timeout=5, ignore_errors=True)
            assert "KMON-F" in out      # we still see it, just don't raise


class TestHighLevelShortcuts:
    def test_assign_just_sends_assign_command(self):
        # The fake monitor doesn't echo input — it just produces a fresh
        # prompt — so we verify success indirectly: assign() returns
        # without raising and the dot prompt is back.
        with _spawn_monitor({"ASSIGN DZ1 DK": "assigned"}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, return_delay=0.1)
            rt.assign("DZ1", "DK")
            assert "assigned" in emu.tail(256)

    def test_chain_runs_in_order(self):
        with _spawn_monitor({"A": "out-a", "B": "out-b", "C": "out-c"}) as emu:
            rt = RT11Session(emu)
            rt.boot(timeout=10, return_delay=0.1)
            outs = rt.chain(["A", "B", "C"], timeout=5)
            assert len(outs) == 3
            assert "out-a" in outs[0]
            assert "out-b" in outs[1]
            assert "out-c" in outs[2]


class TestPromptRegex:
    @pytest.mark.parametrize("text, should_match", [
        (".",              True),
        (".\n",            True),
        ("hello\n.",       True),
        (".\nsome stuff",  False),    # prompt not at the tail
        ("",               False),
        (".more",          False),
    ])
    def test_dot_prompt_matches_tail_only(self, text, should_match):
        assert bool(DOT_PROMPT.search(text)) is should_match
