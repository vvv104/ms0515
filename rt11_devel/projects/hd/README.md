# HD.SYS — paravirtual hard-disk driver for the MS-0515

Builds the RT-11 `HD.SYS` device handler that drives the emulator's
paravirtual `HD:` block device (see `docs/hardware/hd.md`).  The driver is
the **t2** variant; `HD.SYS` v2.0 serves t2 (and t3).

## Source provenance

`HD.MAC` is the v2.0 driver from the public "HD t1..t5" driver
distribution kit (`HD Sources/v2.0/HD.MAC`,
`http://emulator.pdp-11.org.ru/misc/HD_v1_v2_v3_v4_v5.zip`).  The kit's
build recipe (`MACRO HD` + `LINK/NOBIT/EXECUTE:HD.SYS HD`) is captured in
`build.toml`.

### MS-0515 adaptations

Two localized changes from the upstream source, both for `$$SILENT`:

1. **`$$SILENT = 1` enabled** (it ships commented out).  The driver
   otherwise prints its install/boot banner straight to `@#TPS`/`@#TPB`
   -- the DL11 console at 177564/177566.  The MS-0515 has no DL11 there
   (its console is the keyboard + video), so the address never reports
   "ready" and the message loop would hang the install.  This is exactly
   the "disable console output on the 0515" note from the zx-pk.ru thread.

2. **A `BR 20$` on the install-probe good path under `$$SILENT`.**  The
   v2.0 install routine reaches its success exit (`20$: ClC`) *through*
   the message-print loop's branches; `$$SILENT` removes that block, so
   the good path would fall into `30$`, which patches `20$` to `SEC` and
   makes `INSTALL` reject a perfectly healthy device.  The added branch
   restores the missing good-path exit.

## Build

```
python rt11_devel/toolset/build.py rt11_devel/projects/hd/build.toml
```

Produces `HD.SYS` here, assembled and linked by the real RT-11 SJ V5.04
MACRO/LINK inside the emulator (recipe: `MACRO HD` +
`LINK/NOBIT/EXECUTE:HD.SYS HD`).  `build.py` stages `MACRO.SAV`, `LINK.SAV`
and `SYSMAC.SML` on SY: (MACRO auto-searches SY: for the macro library, and
the CCL `LINK` form needs the tools there) — the pristine `system/`
template carries none of them.  See `../../toolset/GOTCHAS.md`.

## Validate (OS oracle)

```
python rt11_devel/projects/hd/validate.py
```

Boots RT-11 from a temp copy of the toolset's `system/` folder template
(with the freshly built `HD.SYS` dropped in) and a blank 20000-block image
on `--hd`, then drives the real driver: `INSTALL HD` / `LOAD HD` /
`INIT HD:` / `COPY SY:STARTS.COM HD:` / `DIR HD:`.  A passing run shows
the copied file back in `DIR HD:` with plenty of free blocks -- proof the
t2 protocol + DMA work against the genuine driver.

Two more oracles cover folder-backed devices end to end:
`validate_rtfs.py` (a host folder as HD: — reads, guest-created host
files, guest delete -> descriptor, live external adds) and
`validate_rtfs_floppy.py` (a host folder as a bootable floppy: COPY/BOOT
materializes the hidden boot file, then RT-11 boots standalone from the
folder).

## Using it by hand

Put `HD.SYS` on the bootable system disk and mount a blank HD image.
RT-11 registers handlers found on the boot volume automatically, and the
SJ monitor fetches them on demand — so after booting it is simply:

```
INIT HD:             ! format the volume (answer Y)
DIR HD:              ! empty directory
COPY SY:TT.SYS HD:   ! use it
```

(`INSTALL HD` / `LOAD HD` are only needed when the handler was added
after boot or must stay resident.)
