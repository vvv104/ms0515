import shutil, subprocess, sys, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent.parent / "toolset"
ROOT = TOOLSET.parent.parent
sys.path.insert(0, str(TOOLSET))
from emu_driver import EmulatorDriver
from rt11 import RT11Session

CLI = ROOT / "package/ms0515-cli.exe"
ROM = ROOT / "package/assets/rom/ms0515-roma.rom"
DISK = ROOT / "package/ms0515-disk.exe"
tmp = Path(tempfile.gettempdir())
work = tmp / "probe_run.dsk"
hd = tmp / "probe.hd"
shutil.copy(TOOLSET / "system.dsk", work)
subprocess.run([str(DISK), "put", str(work), "--side", "1", str(HERE / "PROBE.SAV")], check=True)
hd.write_bytes(b"\x00" * (2000 * 512))

emu = EmulatorDriver([CLI, "--rom", ROM, "--disk0", work, "--hd", str(hd)])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    out = rt.command("RUN DZ2:PROBE", timeout=30)
    print("=== PROBE output ===")
    print(repr(out))
finally:
    emu.kill()
