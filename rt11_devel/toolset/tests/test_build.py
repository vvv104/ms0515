"""Tests for the universal build driver: manifest parsing, recipe lookup,
{name} substitution, plan resolution."""

from __future__ import annotations

import sys
import textwrap
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent
sys.path.insert(0, str(TOOLSET))

from build import (BuildPlan, RECIPES, load_manifest,    # noqa: E402
                   DISK_TOOL, SYSTEM_DISK)


def write_manifest(tmp_path: Path, content: str) -> Path:
    p = tmp_path / "build.toml"
    p.write_text(textwrap.dedent(content), encoding="utf-8")
    return p


# ── recipe table sanity ─────────────────────────────────────────────────────

class TestRecipeTable:
    def test_known_languages(self):
        assert {"macro11", "pascal", "fortran", "basic"} <= set(RECIPES)

    @pytest.mark.parametrize("lang", ["macro11", "pascal", "fortran"])
    def test_every_buildable_recipe_has_macro_and_link(self, lang):
        compilers = RECIPES[lang]["compilers"]
        assert "MACRO.SAV" in compilers
        assert "LINK.SAV" in compilers

    def test_pascal_recipe_pulls_paslib(self):
        assert "PASLIB.OBJ" in RECIPES["pascal"]["libs"]
        assert any("PASLIB" in c for c in RECIPES["pascal"]["commands"])

    def test_fortran_recipe_pulls_forlib(self):
        assert "FORLIB.OBJ" in RECIPES["fortran"]["libs"]
        assert any("FORLIB" in c for c in RECIPES["fortran"]["commands"])


# ── manifest → plan ─────────────────────────────────────────────────────────

class TestBuildPlan:
    def test_minimal_macro11_manifest(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "FOO"
            language = "macro11"
        """)
        plan = load_manifest(m)
        assert plan.name == "FOO"
        assert plan.language == "macro11"
        assert plan.sources == ["FOO.MAC"]
        assert plan.outputs == ["FOO.SAV"]
        assert plan.commands == ["MACRO FOO", "LINK FOO"]

    def test_explicit_sources_and_outputs_override_defaults(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "FOO"
            language = "macro11"
            sources  = ["FOO.MAC", "EXTRA.MAC"]
            outputs  = ["FOO.SAV", "FOO.MAP"]
        """)
        plan = load_manifest(m)
        assert plan.sources == ["FOO.MAC", "EXTRA.MAC"]
        assert plan.outputs == ["FOO.SAV", "FOO.MAP"]

    def test_pascal_recipe_picks_paslib_and_three_phase_commands(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "BAR"
            language = "pascal"
        """)
        plan = load_manifest(m)
        assert plan.sources == ["BAR.PAS"]
        assert plan.commands == [
            "PAS1 BAR=BAR",
            "MACRO BAR",
            "LINK BAR,PASLIB,PAS1",
        ]
        assert "PASLIB.OBJ" in plan.recipe_libs

    def test_custom_commands_override_recipe(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "OVR"
            language = "macro11"
            [build]
            commands = ["RUN DZ2:MACRO {name}/LIST", "RUN DZ2:LINK {name},MYLIB"]
        """)
        plan = load_manifest(m)
        assert plan.commands == [
            "RUN DZ2:MACRO OVR/LIST",
            "RUN DZ2:LINK OVR,MYLIB",
        ]

    def test_extra_libs_appended_to_staged_files(self, tmp_path):
        # Need a source file on disk for staged_files() to point at something
        (tmp_path / "QUX.MAC").write_bytes(b"")
        m = write_manifest(tmp_path, """
            [project]
            name     = "QUX"
            language = "macro11"
            [build]
            libs = ["MYLIB.OBJ"]
        """)
        plan = load_manifest(m)
        sy = {Path(f).name for f in plan.sy_files()}
        dk = {Path(f).name for f in plan.dk_files()}
        assert "MYLIB.OBJ" in dk          # extra lib -> DK: (linked from there)
        assert "MACRO.SAV" in sy           # compiler  -> SY:
        assert "QUX.MAC" in dk             # source    -> DK:

    def test_hook_paths_stay_relative_to_manifest_dir(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name       = "HK"
            language   = "macro11"
            pre_build  = "source/gen.py"
            post_build = "tools/pack.py"
        """)
        plan = load_manifest(m)
        assert plan.pre_hook == "source/gen.py"
        assert plan.post_hook == "tools/pack.py"
        assert plan.manifest_dir == tmp_path


# ── error cases ─────────────────────────────────────────────────────────────

class TestManifestErrors:
    def test_missing_project_table(self, tmp_path):
        m = write_manifest(tmp_path, """
            [build]
            commands = ["RUN DZ2:WHATEVER"]
        """)
        with pytest.raises(ValueError, match="project"):
            load_manifest(m)

    def test_missing_name(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            language = "macro11"
        """)
        with pytest.raises(ValueError, match="name"):
            load_manifest(m)

    def test_missing_language(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name = "FOO"
        """)
        with pytest.raises(ValueError, match="language"):
            load_manifest(m)

    def test_unknown_language(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "FOO"
            language = "cobol"
        """)
        with pytest.raises(ValueError, match="cobol"):
            load_manifest(m)


# ── real manifest in this repo ──────────────────────────────────────────────

class TestRepoManifests:
    """Pick up the manifests actually shipped under
    rt11_devel/projects/<name>/build.toml and make sure they parse cleanly."""

    def test_saper_manifest_parses(self):
        manifest = TOOLSET.parent / "projects" / "saper" / "build.toml"
        if not manifest.exists():
            pytest.skip("projects/saper/build.toml not present in this checkout")
        plan = load_manifest(manifest)
        assert plan.name == "SAPER"
        assert plan.language == "macro11"
        assert "SAPER.SAV" in plan.outputs


# ── pristine template invariant ─────────────────────────────────────────────

class TestSystemDiskIsPristine:
    """system.dsk is a read-only TEMPLATE: build.py copies it and modifies only
    the copy, staging the whole toolchain onto that copy.  Nothing build-time
    (compilers, SYSMAC.SML, STARTS.COM) may be baked into the template, or a
    build would secretly depend on a polluted image instead of on staging."""

    BASE = {"RT11SJ.SYS", "SWAP.SYS", "DZ.SYS", "TT.SYS",
            "PIP.SAV", "DUP.SAV", "DIR.SAV"}

    def _side0_files(self) -> set[str]:
        import re
        import subprocess
        out = subprocess.run(
            [str(DISK_TOOL), "dir", str(SYSTEM_DISK), "--side", "0"],
            capture_output=True, text=True)
        return set(re.findall(r"([A-Z0-9]+\.[A-Z0-9]+)\s+blk=", out.stdout))

    def test_template_holds_only_the_base_rt11_system(self):
        if not DISK_TOOL.exists() or not SYSTEM_DISK.exists():
            pytest.skip("needs package/ms0515-disk and system.dsk")
        files = self._side0_files()
        extra = files - self.BASE
        assert not extra, f"system.dsk template is polluted with {extra}"
        assert files == self.BASE


class TestToolchainIsStaged:
    """The toolchain must be STAGED onto the copy, not assumed present on the
    template: compilers + SYSMAC.SML go on SY: (side 0), sources + object libs
    on DK: (side 1)."""

    def _plan(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name = "FOO"
            language = "macro11"
        """)
        return load_manifest(m)

    def test_compilers_and_macro_library_staged_on_sy(self, tmp_path):
        sy = {p.name for p in self._plan(tmp_path).sy_files()}
        assert {"MACRO.SAV", "LINK.SAV", "SYSMAC.SML"} <= sy

    def test_sources_and_object_libs_staged_on_dk(self, tmp_path):
        dk = {p.name for p in self._plan(tmp_path).dk_files()}
        assert "FOO.MAC" in dk
        assert "SYSLIB.OBJ" in dk
        # the SY:-only files must NOT also be on DK
        assert "SYSMAC.SML" not in dk
        assert "MACRO.SAV" not in dk
