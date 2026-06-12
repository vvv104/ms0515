"""OS-oracle for folder-backed HD volumes (.rtfs).

Builds a folder with host files + a descriptor, boots RT-11 with the
folder mounted as HD:, and drives the real driver both ways:
  DIR HD:                  -> mangled names visible
  COPY HD:... SY:          -> guest reads host bytes
  COPY SY:TT.SYS HD:       -> a host file materializes in the folder
  DELETE HD:...            -> the descriptor line turns `deleted`
  (external add)           -> a new host file appears in DIR HD:
"""
import shutil, sys, tempfile, time
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver
from rt11 import RT11Session

CLI = ROOT / "package/ms0515-cli.exe"
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"
SYSTEM_DIR = TOOLSET / "system"

if not (HERE / "HD.SYS").exists():
    raise SystemExit("HD.SYS not built — run the hd project build first")

tmp = Path(tempfile.gettempdir())
boot = tmp / "rtfs_oracle_boot"
shutil.rmtree(boot, ignore_errors=True)
shutil.copytree(SYSTEM_DIR, boot)
shutil.copy(HERE / "HD.SYS", boot / "HD.SYS")
(boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\n")

vol = tmp / "rtfs_oracle"
shutil.rmtree(vol, ignore_errors=True)
vol.mkdir(parents=True)
(vol / "hello.txt").write_bytes(b"HELLO FROM THE HOST FOLDER\r\n")
(vol / "data.bin").write_bytes(bytes(range(256)) * 4)
(vol / "device.rtfs").write_bytes(b"device: hd\nblocks: 2000\n")

emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                      "--disk0-side0", boot / "device.rtfs",
                      "--hd", str(vol / "device.rtfs")])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)

    out = rt.command("DIR HD:", timeout=30).replace(" ", "")
    print("1) DIR HD: shows folder files:",
          "HELLO.TXT" in out and "DATA.BIN" in out)

    out = rt.command("COPY HD:HELLO.TXT SY:", timeout=30).replace(" ", "")
    rt.command("DELETE/NOQUERY SY:HELLO.TXT", timeout=20, ignore_errors=True)
    print("2) guest reads host bytes:", "Filescopied" in out)

    rt.command("COPY SY:TT.SYS HD:", timeout=30)
    time.sleep(0.5)
    print("3) guest COPY materializes a host file:",
          (vol / "tt.sys").exists())

    rt.command("DELETE/NOQUERY HD:DATA.BIN", timeout=30, ignore_errors=True)
    time.sleep(0.5)
    desc = (vol / "device.rtfs").read_text()
    print("4) guest delete marks descriptor (host file kept):",
          "deleted" in desc and (vol / "data.bin").exists())

    (vol / "late.txt").write_bytes(b"added while running")
    out = rt.command("DIR HD:", timeout=30).replace(" ", "")
    print("5) external add visible inside:", "LATE.TXT" in out)
finally:
    emu.dump(tmp / "rtfs_oracle.log")
    emu.kill()
print(f"descriptor now:\n{(vol / 'device.rtfs').read_text()}")
