#!/usr/bin/env python3
"""
fingerprint_systems.py - Group every disk in collection/ss/ by the
operating-system fingerprint visible from its boot block, directory
listing, and driver/monitor file sizes.

This does NOT depend on the extracted file *contents* being correct -
filenames and sizes survive layout-detection errors, and the boot block
sits at a fixed byte offset in every cyl-0-last layout so we read it
directly from the raw image.

Outputs:
  collection/extracted/SYSTEMS.md - markdown report:
      * per-disk fingerprint table
      * cluster summary by boot-block hash
      * cluster summary by driver-set signature
"""

from __future__ import annotations

import hashlib
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
SS_DIR = REPO_ROOT / "collection" / "ss"
EXTRACTED = REPO_ROOT / "collection" / "extracted"
OUT_REPORT = EXTRACTED / "SYSTEMS.md"

BOOT_BYTE = 5120   # LBN 0 in cyl-0-last layouts
HOME_BYTE = 6144   # LBN 1 in cyl-0-last canonical (with INTERLEAVE[1]=2)

# Files we look for as system-identifying.
MONITORS = {"RT11SJ.SYS", "RT11FB.SYS", "RT11XM.SYS",
            "MON8SJ.SYS", "MONITR.SYS"}
DRIVERS = {"DZ.SYS", "DX.SYS", "DY.SYS", "DU.SYS", "DM.SYS",
           "SL.SYS", "TT.SYS", "VM.SYS", "LD.SYS", "LP.SYS",
           "SWAP.SYS"}
OS_TAGS = {"OSA.SYS", "OMEGA.SYS", "MIHIN.SYS", "MIRAGE.SYS",
           "DOS.SYS", "STARTS.COM"}

MANIFEST_ROW = re.compile(
    r"^\|\s*`(?P<safe>[^`]+)`\s*\|\s*`(?P<rt11>[^`]+)`\s*\|"
    r"\s*(?P<start>\d+)\s*\|\s*(?P<len>\d+)\s*\|\s*(?P<bytes>\d+)\s*\|")

LAYOUT_LINE = re.compile(r"^- Layout : `(?P<layout>[^`]+)`")


@dataclass
class FileInfo:
    name: str
    safe: str
    start: int
    blocks: int


@dataclass
class DiskFingerprint:
    name: str
    layout: str | None
    boot_hash: str
    home_hash: str
    boot_first16: str
    files: list[FileInfo]
    monitor: str
    drivers: tuple[str, ...]
    os_tags: tuple[str, ...]
    files_count: int

    @property
    def driver_sig(self) -> str:
        return ",".join(self.drivers) or "-"

    @property
    def system_label(self) -> str:
        tag = self.os_tags[0] if self.os_tags else (self.monitor or "-")
        return tag


def parse_manifest(path: Path) -> tuple[str | None, list[FileInfo]]:
    layout: str | None = None
    files: list[FileInfo] = []
    if not path.exists():
        return None, []
    for line in path.read_text(encoding="utf-8").splitlines():
        m = LAYOUT_LINE.match(line)
        if m:
            layout = m.group("layout")
            continue
        m = MANIFEST_ROW.match(line)
        if m:
            files.append(FileInfo(
                name=m.group("rt11"),
                safe=m.group("safe"),
                start=int(m.group("start")),
                blocks=int(m.group("len")),
            ))
    return layout, files


def fingerprint_disk(image: Path) -> DiskFingerprint:
    data = image.read_bytes()
    boot = data[BOOT_BYTE:BOOT_BYTE + 512] if len(data) >= BOOT_BYTE + 512 else b""
    home = data[HOME_BYTE:HOME_BYTE + 512] if len(data) >= HOME_BYTE + 512 else b""

    stem = image.stem
    manifest = EXTRACTED / stem / "MANIFEST.md"
    layout, files = parse_manifest(manifest)

    perm_names = {f.name.upper() for f in files}
    monitor = next((n for n in MONITORS if n in perm_names), "")
    drivers = tuple(sorted(n for n in DRIVERS if n in perm_names))
    os_tags = tuple(sorted(n for n in OS_TAGS if n in perm_names))

    return DiskFingerprint(
        name=image.name,
        layout=layout,
        boot_hash=hashlib.sha256(boot).hexdigest()[:12] if boot else "",
        home_hash=hashlib.sha256(home).hexdigest()[:12] if home else "",
        boot_first16=boot[:16].hex() if boot else "",
        files=files,
        monitor=monitor,
        drivers=drivers,
        os_tags=os_tags,
        files_count=len(files),
    )


def render_report(prints: list[DiskFingerprint]) -> str:
    lines: list[str] = []
    lines.append("# MS-0515 disk system fingerprints\n")
    lines.append("Boot-block hash is sha256 of bytes 5120..5631 (LBN 0 in any")
    lines.append("cyl-0-last layout).  Identical hash = identical boot loader =")
    lines.append("almost certainly the same operating system.  Files are listed")
    lines.append("from the RT-11 directory (which parses correctly even when the")
    lines.append("file-data mapping is wrong, so filename/size data is reliable).\n")

    # Per-disk table.
    lines.append("## Per-disk fingerprint\n")
    lines.append("| Disk | Layout | Boot sha (12) | Monitor | OS tag | Drivers | Files |")
    lines.append("|------|--------|---------------|---------|--------|---------|------:|")
    for p in sorted(prints, key=lambda x: x.name.lower()):
        lines.append(f"| `{p.name}` | {p.layout or '-'} | `{p.boot_hash}` | "
                     f"{p.monitor or '-'} | "
                     f"{','.join(p.os_tags) or '-'} | "
                     f"{p.driver_sig} | {p.files_count} |")
    lines.append("")

    # Cluster by boot-block hash.
    lines.append("## Clusters by boot-block hash\n")
    by_boot: dict[str, list[DiskFingerprint]] = defaultdict(list)
    for p in prints:
        by_boot[p.boot_hash].append(p)
    for sha, group in sorted(by_boot.items(), key=lambda x: -len(x[1])):
        labels = Counter(p.system_label for p in group).most_common(1)
        label = labels[0][0] if labels else "?"
        sample = group[0]
        lines.append(f"### `{sha}` ({len(group)} disk(s))  - dominant tag: `{label}`")
        lines.append("")
        lines.append(f"Sample boot first 16 bytes: `{sample.boot_first16}`")
        lines.append("")
        lines.append("Members:")
        for p in sorted(group, key=lambda x: x.name.lower()):
            lines.append(f"- `{p.name}` (monitor={p.monitor or '-'}, "
                         f"os_tags={','.join(p.os_tags) or '-'}, "
                         f"drivers={p.driver_sig}, files={p.files_count})")
        lines.append("")

    # Cluster by driver signature.
    lines.append("## Clusters by driver signature\n")
    by_drv: dict[str, list[DiskFingerprint]] = defaultdict(list)
    for p in prints:
        by_drv[p.driver_sig].append(p)
    for sig, group in sorted(by_drv.items(), key=lambda x: -len(x[1])):
        lines.append(f"### `{sig}` ({len(group)} disk(s))")
        for p in sorted(group, key=lambda x: x.name.lower()):
            lines.append(f"- `{p.name}` (monitor={p.monitor or '-'}, "
                         f"os_tags={','.join(p.os_tags) or '-'}, "
                         f"files={p.files_count}, layout={p.layout or '-'})")
        lines.append("")

    return "\n".join(lines)


def main() -> int:
    if not SS_DIR.exists():
        print(f"missing {SS_DIR}")
        return 1
    prints: list[DiskFingerprint] = []
    for image in sorted(SS_DIR.iterdir()):
        if not image.is_file() or image.suffix.lower() != ".dsk":
            continue
        prints.append(fingerprint_disk(image))
    OUT_REPORT.parent.mkdir(parents=True, exist_ok=True)
    OUT_REPORT.write_text(render_report(prints), encoding="utf-8")
    print(f"Wrote {OUT_REPORT} ({len(prints)} disks)")
    # Quick summary on stdout.
    by_boot: dict[str, list[DiskFingerprint]] = defaultdict(list)
    for p in prints:
        by_boot[p.boot_hash].append(p)
    print("Boot-hash clusters:")
    for sha, group in sorted(by_boot.items(), key=lambda x: -len(x[1])):
        labels = Counter(p.system_label for p in group).most_common(1)
        label = labels[0][0] if labels else "?"
        print(f"  {sha}  {len(group):>2} disk(s)  tag={label}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
