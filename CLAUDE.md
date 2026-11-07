# MS0515 Emulator Project

**Read this first after compacting!**

The emulator of the Elektronika MS 0515 and nothing else: its sources,
tests, tools and the documentation of the machine as it emulates it.  The
layers and what each is for: `docs/architecture.md` ("Layers in brief");
where every file lives: `docs/layout.md`; how a release is made:
`docs/release.md`.  Programs for the machine and their toolset are in
ms0515-devel, the software collection in ms0515-software; they use this
repository, never the other way round - nothing here reads from them at
build or test time (a copy of what ms0515-run needs is kept in
`src/tools/run/assets/`, refreshed by `tools/run_assets.py`).

## Rules
- All code, comments and documentation in **English only**.
- Original code from architecture knowledge; never copy from reference projects.
- **Never commit to `main` or push without the user's permission.**  Commits
  to a feature branch are allowed.  CI gates every merge and release:
  PR green (all platform jobs) → merge → `main` green → only then a tag;
  the rest of the release is `docs/release.md`, and it is finished, not started.
- **TDD**: interface, then tests, then code; run tests at each stage.
- **Revert failed attempts**: no dead code from approaches that did not work.
- The C/C++ standard is declared once, in `src/CMakeLists.txt`; never per target.
- **Zero warnings, never suppressed** (/W4 /WX, -Werror stay; rewrite the call:
  `fopen_s` over `fopen`, `_dupenv_s` over `getenv`, or drop it).
- **No vendored third-party sources**: dependencies through Conan.
- **No machine-specific paths in tracked files** (no `C:\Users\...`, `/home/...`):
  an environment variable with a repository-relative fallback; before every
  commit `git grep -i "users[.]voron"` must return nothing.
- **No session links in the history**: commit messages and PR bodies end with
  the `Co-Authored-By:` trailer and the "Generated with Claude Code" credit,
  nothing else.
