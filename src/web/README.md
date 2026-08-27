# The MS-0515 in the browser

The core and the lib compiled with Emscripten behind a flat C API
(`src/ms0515_web.cpp`), a page that drives the module (`www/`), and the
assets it ships with - a static site.  Nothing runs on a server: the
browser downloads the module, a ROM and a disk image, and the machine runs
in the tab; an image the guest writes to lives on in the browser's
IndexedDB (the original stays on the host, one click brings it back).

## Build

```
cd src
conan build . -pr:h profiles/emscripten -pr:b default --build=missing
```

The Emscripten SDK comes from Conan Center as a tool requirement (about
600 MB once).  `emcc` must run a current Node: point `EM_NODE_JS` at one
(the SDK package's own Node is too old for its version check):

```
export EM_NODE_JS="$(which node)"          # bash; Windows: the node.exe path
```

The site lands in `build/emscripten-release/web/dist/`:

```
ms0515.js, ms0515.wasm   the module (MODULARIZE, createMs0515())
index.html, app.js, keys.js
rom/                     ms0515-roma.rom, ms0515-romb.rom
disks/                   the demo disks from assets/disks
```

Serve the folder as is (`python -m http.server` in it, or GitHub Pages);
`file://` does not work (a module fetches its `.wasm`).  The published
site, <https://vvv104.github.io/ms0515/>, is deployed by
`.github/workflows/pages.yml` on every release tag: the repository's Pages
source is "GitHub Actions", and the `github-pages` environment allows the
`v*` tags to deploy (its branch policy: `main` and the tag rule `v*`).

## Check

`node src/web/smoke.mjs build/emscripten-release/web/dist` boots the OSA
disk for three seconds and expects RT-11's screen (the same oracle the
native tests use).  `node src/web/browser_check.mjs http://localhost:8515/`
does the same through the page in a headless Chromium-family browser
started with `--remote-debugging-port=9222` (the page exposes
`window.__ms()` - the frame count and the picture's colours - for it).
CI runs both in the `web / emscripten` job.

## The C API

| call | |
|---|---|
| `ms_create()` / `ms_destroy(h)` / `ms_reset(h)` | one machine |
| `ms_load_rom(h, path)` | the ROM from the module's file system |
| `ms_mount(h, unit, path)` / `ms_unmount(h, unit)` | FDC unit = side × 2 + drive (FD0 / FD1 the drives' side 0, FD2 / FD3 their side 1); a double-sided image goes on both units of its drive |
| `ms_disk_active(h, unit)` / `ms_hd_active(h)` | the activity lamps |
| `ms_mount_hd(h, path)` / `ms_unmount_hd(h)` | the paravirtual HD: image (any multiple of 512 bytes); mounting presents the controller |
| `ms_frame(h)` | one 50 Hz frame; returns the CPU cycles (0 = halted) |
| `ms_render(h)` | the picture: 640 × 400 RGBA, line-doubled like the SDL front-end |
| `ms_audio(h, out, max, rate)` | the last frame's speaker as 16-bit PCM |
| `ms_key(h, key, down)` / `ms_key_tick(h, ms)` | `ms0515::Key` values (`www/keys.js` mirrors the enum, `ms_key_max()` guards the drift); the tick drives auto-repeat |
| `ms_save_state(h, path)` / `ms_load_state(h, path)` | snapshots in the module's file system |
| `ms_ruslat(h)` / `ms_caps(h)` / `ms_key_held(h, key)` | the keyboard's lamps and held keys, for the host-key mapping |
| `ms_key_release_all(h)` | every key up (the canvas lost the focus) |
| `ms_disk_dir(path, side, linear)` / `ms_disk_get(path, side, linear, name)` + `ms_disk_data()` / `ms_disk_put(path, side, linear, name, data, len, y, m, d, prot)` / `ms_disk_rm(...)` / `ms_disk_rename(...)` / `ms_disk_protect(..., on)` / `ms_disk_init(...)` / `ms_disk_squeeze(...)` / `ms_disk_error()` | the RT-11 directory of an image in the module's file system (the `src/disk` library; `linear` for the HD): the page's commander |
| `ms_disk_grow(path, blocks)` | a linear image enlarged: the commander grows a logical disk a file does not fit into |
| `ms_ld_create(blocks, segments, volumeId)` / `ms_ld_put(name, data, len, y, m, d, prot)` / `ms_ld_data()` + `ms_ld_size()` | a logical disk built in memory - the linear file the system's LD handler mounts as a volume (`MOUNT LD0: DZn:NAME.DSK`) |
| `ms_joystick(h, bits)` | the joystick on the MS7007 port: bits 0-4 right, left, down, up, fire (`joystick.js`: the arrows and Space, or a touch overlay) |

## The page

The drives mount like the desktop front-ends': A and B have a side 0 and
a side 1 each (a 400 KB image is one side, an 800 KB one takes both sides
of its drive), the HD takes an image of any size - the shipped images, the
user's own (opened from a file, or a new blank image - a floppy of one or
two sides, or a HD of a chosen size), all mounted and
unmounted on the running machine from the drive's panel in the toolbar,
which also carries the drive's activity lamp.  The mounts and the list of
own images persist in localStorage, the image bytes in IndexedDB.

`?disk=osa.dsk&disk1=...&hd=...&rom=a` picks the images for drive A,
drive B, the HD and the ROM (over the remembered mounts); `autostart=0`
waits for the Boot button; `type=R%20FIST` (with `delay=` ms, 3000) types
a command for the monitor after the boot (`window.__ms.type()` does the
same).

The keyboard is the SDL front-end's (`Keymap.cpp` / `PhysicalKeyboard.cpp`)
with `KeyboardEvent.code` for the scancode: a host key maps by character to
an MS7004 key plus the Shift it needs there (a US-layout keyboard makes the
characters on its caps; in РУС mode - the machine's lamp, read through
`ms_ruslat` - the letters are positional ЙЦУКЕН), a synthetic Shift makes
up the difference and is undone at release, CAPS + Shift inverts a letter,
the numpad / * + and a few РУС-mode symbols are special cases.  The host's
auto-repeat is ignored: the MS7004 repeats itself (`ms_key_tick`).

Full screen (the button, F11): the picture alone on the display, through
the Fullscreen API on `<main>` (not on an iPhone, which has none: there
"Add to Home Screen" is the way).  On a touch device the keyboard button
focuses a hidden text field so the OS raises its keyboard (`softkeys.js`):
its characters arrive as input events and go through the typing queue,
Cyrillic letters as the ЙЦУКЕН positions with the machine switched to РУС
on the way (and back for Latin); Enter and Backspace by key; the page
shrinks to the visual viewport while the keyboard is up.

"Files" replaces the screen with a two-pane commander (`fm.js`) over the
mounted disks' RT-11 directories, read through the offline disk library
compiled into the module: a floppy side or the HD image in each pane, the
unused areas listed with the files, and the ten keys always drawn below as
in Midnight Commander - F1 Upload a file of the user's (its name made a
6.3 RT-11 name, no date: the OS cannot hold today's), F2 Download to the
computer, F3 View, F4 Edit, F5 Copy to the other pane (with the date and
the protection), F6 Rename, F7 Init the pane's volume, F8 Delete, F9
Squeeze, F10 Quit (Esc too); Tab, the arrows, Enter as in the commander.
Insert or Shift with the arrows marks files, gray + / - / * mark by the
OS's patterns (`*`, `%`, an omitted part `*`, several with commas) or
invert; a protected file is deleted or moved only after one more Yes / No.
With Alt held the bar shows the other meanings: Alt+F1 / F2 open a pane's
disk list, Alt+F5 gathers the marked files into a logical disk (the
PROGS.DSK format: linear, one directory segment per 72 files, the name's
stem as the volume id) written to the other pane's disk for `MOUNT LD0:`,
Alt+F6 protects the marked files (or unprotects them when every one is),
Alt+F7 finds files by pattern on every mounted disk and goes to the pick;
Alt+F8 on an unused area undeletes the file it was (the OS's DELETE only
flips the entry's status - the name, shown after `< UNUSED >`, the length
and the date stay, and so does the data until a put covers the area;
`ms_disk_undelete` by the entry's ordinal, the `i` of `ms_disk_dir`, under
the old name or another one the dialog asks for - a free NAME1 offered
when the old name is taken now; on an unused area that was no file the key
reads Recover: the area made a file of a name given, whatever lies in it).
F3 on an unused area views its bytes (`ms_disk_area` by the ordinal).
Enter on a logical disk enters it as if a directory (".." or Backspace
back out): its file is taken into the module's file system, read there
linearly and put back into its disk after every change; a file that does
not fit makes the volume grow by what it needs (`ms_disk_grow`: blocks
appended, the last empty entry lengthened or one added); in the dialogs
such a place is `DZn:NAME.DSK/FILE`.
The viewer's keys, as mc's: F1 text / octal / hex in turn (a binary
starts at octal), F2 wrap /
unwrap at the machine's 80 columns, F3 and F10 back, F4 the encoding
(ASCII, KOI-8R, KOI-7, KOI-7 with the terminal's РУС / ЛАТ shifts ^N / ^O
- 0x40..0x5F lowercase and 0x60..0x7F uppercase Cyrillic while shifted -
CP866 in turn; guessed from the bytes), F5 go to a line (an offset in the
dump), F7 search - a string in the encoding, or a byte sequence in the
digits shown - the hit marked and scrolled to.  The editor's (`edit.js`):
F1 the representation, F2 save (after a word; the editing goes on - and
leaving with changes asks Yes / No / Cancel), F4 the encoding (a text in the encoding it was read in, saved
the same way with CR LF; a binary as its bytes in octal - the machine's
notation - or hex, the digits or the characters typed over), F5 go to, F7
search, F8 replace / insert (Delete and Backspace remove bytes), F10 back.
A write goes around the FDC: the image is unmounted, changed in the
module's file system, mounted again, so the guest sees a changed disk at
its next directory read; a file that grows moves to a free area, leaving
an unused one where it was.

Sound: each frame's PCM goes to an AudioWorklet (`audio-worklet.js`) that
plays the chunks back to back and drops the oldest past ~100 ms of lag; it
starts on a click (browsers require a gesture).  An image the guest writes
to is copied to IndexedDB (checked every 64 frames by its mtime in the
module's file system); "Download" saves the live image, "Revert" drops
what was written to a shipped image and mounts the original again,
"Delete" removes one of the user's own.
