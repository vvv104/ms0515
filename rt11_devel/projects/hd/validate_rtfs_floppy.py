"""OS-oracle for folder-backed FLOPPIES (.rtfs, device: floppy).

Fills a folder with the curated system set, mounts it as drive 1 under a
bootable host, writes a bootloader onto it with COPY/BOOT (materializing
the hidden boot file), then boots RT-11 STANDALONE FROM THE HOST FOLDER
and exercises it.
"""
import shutil, subprocess, sys, tempfile, time
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
BOOT = ROOT / "package/assets/disks/rt11-hd.dsk"
DISK = ROOT / "package/ms0515-disk.exe"

tmp = Path(tempfile.gettempdir())
vol = tmp / "rtfs_floppy_oracle"
shutil.rmtree(vol, ignore_errors=True)
vol.mkdir(parents=True)

# 1. populate the folder with the curated system set
subprocess.run([str(DISK), "get",
                str(ROOT / "package/assets/disks/vvv-hd.dsk"),
                "--out", str(vol)], check=True, capture_output=True)
for f in sorted(vol.iterdir()):
    pass
(vol / "device.rtfs").write_bytes(b"device: floppy\nblocks: 800\n")
print("folder populated:", len(list(vol.iterdir())) - 1, "files")

# 2. host session: read the folder via the FDC + write a bootloader onto it
emu = EmulatorDriver([CLI, "--rom", ROM, "--disk0", BOOT,
                      "--disk1-side0", str(vol / "device.rtfs")])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    out = rt.command("DIR DZ1:RT11SJ.*", timeout=30).replace(" ", "")
    print("1) FDC reads the folder:", "RT11SJ.SYS" in out)
    out = rt.command("COPY/BOOT DZ1:RT11SJ.SYS DZ1:", timeout=40,
                     ignore_errors=True).replace(" ", "")
    print("2) COPY/BOOT onto the folder:", "?" not in out)
finally:
    emu.kill()
print("3) boot file materialized:", (vol / "boot.bin").exists())

# 3. boot standalone from the folder
emu = EmulatorDriver([CLI, "--rom", ROM,
                      "--disk0-side0", str(vol / "device.rtfs")])
emu.start()
try:
    rt = RT11Session(emu)
    rt.boot(timeout=60)
    print("4) RT-11 BOOTS FROM THE HOST FOLDER: True")
    out = rt.command("DIR SY:SWAP.*", timeout=30).replace(" ", "")
    print("5) system volume readable:", "SWAP.SYS" in out)
    rt.command("COPY SY:TT.SYS SY:COPY.TST", timeout=30)
    time.sleep(0.5)
    print("6) guest write materializes a host file:",
          (vol / "copy.tst").exists())
except Exception as ex:
    print("4) RT-11 BOOTS FROM THE HOST FOLDER: False |", str(ex)[:60])
finally:
    emu.dump(tmp / "rtfs_floppy_oracle.log")
    emu.kill()
