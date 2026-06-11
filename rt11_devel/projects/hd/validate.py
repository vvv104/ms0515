"""OS-oracle: drive the real HD.SYS against the emulator's HD: device.

Boots RT-11 with HD.SYS on SY: and a blank HD image on --hd, then:
  LOAD HD ; INIT HD: ; COPY a file to HD: ; DIR HD:
If the directory comes back with our file, the t2 device + driver work.
"""
import shutil, subprocess, sys, tempfile, time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver
from rt11 import RT11Session, DOT_PROMPT

SYSTEM = TOOLSET / "system.dsk"
CLI = ROOT / "package/ms0515-cli.exe"
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"
DISK = ROOT / "package/ms0515-disk.exe"

HD_BLOCKS = 20000                      # ~10 MB, well under the 65535 cap

tmp = Path(tempfile.gettempdir())
sysdsk = tmp / "hd_oracle_sys.dsk"
hdimg = tmp / "hd_oracle.hd"

# 1. system disk with HD.SYS + a startup file on SY: (side 0).  system.dsk
# carries no STARTS.COM (the toolset stages one per build), so a direct boot
# like this one stages the toolset default to start cleanly.
shutil.copy(SYSTEM, sysdsk)
subprocess.run([str(DISK), "put", str(sysdsk), "--side", "0",
                str(HERE / "HD.SYS"), str(TOOLSET / "STARTS.COM")], check=True)

# 2. blank HD image
hdimg.write_bytes(b"\x00" * (HD_BLOCKS * 512))
print(f"blank HD image: {hdimg} ({HD_BLOCKS} blocks)")

emu = EmulatorDriver([CLI, "--rom", ROM, "--disk0", sysdsk, "--hd", str(hdimg)])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    print("=== INSTALL HD (register the device with the monitor) ===")
    print(rt.command("INSTALL HD", timeout=30))
    print("=== LOAD HD ===")
    print(rt.command("LOAD HD", timeout=30))

    print("=== INIT HD: (auto-confirm) ===")
    emu.send("INIT HD:\r")
    emu.expect(r"\?", timeout=20)       # "...are you sure?" (localized)
    emu.send("Y\r")
    emu.wait_for(DOT_PROMPT, "INIT done", timeout=60)
    print(emu.tail(600))

    print("=== COPY SY:STARTS.COM HD: ===")
    print(rt.command("COPY SY:STARTS.COM HD:", timeout=30))

    print("=== DIR HD: ===")
    print(rt.command("DIR HD:", timeout=30))
finally:
    emu.dump(tmp / "hd_oracle_session.log")
    emu.kill()
print(f"\nfull session log: {tmp / 'hd_oracle_session.log'}")
