"""Run PROBE.SAV (the HD CSR probe) under RT-11 with a HD image mounted.

Boots a copy of the toolset system/ folder with PROBE.SAV staged on a
work folder.  Build PROBE.SAV first via the hdprobe project build.
Prints A (healthy t2 CSR), B (bit 7 clear) or Z (CSR reads zero).
"""
import shutil, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver
from rt11 import RT11Session

CLI = ROOT / "package/ms0515-cli.exe"
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"

if not (HERE / "PROBE.SAV").exists():
    raise SystemExit("PROBE.SAV not built — run the hdprobe project build first")

tmp = Path(tempfile.gettempdir())
boot = tmp / "probe_boot"
work = tmp / "probe_work"
hd = tmp / "probe.hd"
shutil.rmtree(boot, ignore_errors=True)
shutil.rmtree(work, ignore_errors=True)
shutil.copytree(TOOLSET / "system", boot)
(boot / "STARTS.COM").write_bytes(b"SET TT QUIET\r\n")
work.mkdir()
shutil.copy(HERE / "PROBE.SAV", work / "PROBE.SAV")
(work / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")
hd.write_bytes(b"\x00" * (2000 * 512))

emu = EmulatorDriver([CLI, "--no-config", "--rom", ROM,
                      "--disk0-side0", boot / "device.rtfs",
                      "--disk1-side0", work / "device.rtfs",
                      "--hd", str(hd)])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    out = rt.command("RUN DZ1:PROBE", timeout=30)
    print("=== PROBE output ===")
    print(repr(out))
finally:
    emu.kill()
