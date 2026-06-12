"""OS-oracle: drive the real HD.SYS against the emulator's HD: device.

Boots RT-11 from a copy of the toolset's system/ folder template (with
HD.SYS dropped in) and a blank HD image on --hd, then:
  INSTALL HD ; LOAD HD ; INIT HD: ; COPY a file to HD: ; DIR HD:
If the directory comes back with our file, the t2 device + driver work.

Build HD.SYS first:  python rt11_devel/toolset/build.py \
                            rt11_devel/projects/hd/build.toml
"""
import shutil, sys, tempfile
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver
from rt11 import RT11Session, DOT_PROMPT

CLI = ROOT / "package/ms0515-cli.exe"
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"
SYSTEM_DIR = TOOLSET / "system"

HD_BLOCKS = 20000                      # ~10 MB, well under the 65535 cap

if not (HERE / "HD.SYS").exists():
    raise SystemExit("HD.SYS not built — run the hd project build first")

tmp = Path(tempfile.gettempdir())
boot = tmp / "hd_oracle_boot"
hdimg = tmp / "hd_oracle.hd"

# 1. boot folder: the system template + HD.SYS + a quiet startup file
shutil.rmtree(boot, ignore_errors=True)
shutil.copytree(SYSTEM_DIR, boot)
shutil.copy(HERE / "HD.SYS", boot / "HD.SYS")
(boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\n")

# 2. blank HD image
hdimg.write_bytes(b"\x00" * (HD_BLOCKS * 512))
print(f"blank HD image: {hdimg} ({HD_BLOCKS} blocks)")

emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                      "--disk0-side0", boot / "device.rtfs",
                      "--hd", str(hdimg)])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    print("=== INSTALL HD (register the device with the monitor) ===")
    print(rt.command("INSTALL HD", timeout=30, ignore_errors=True))
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
