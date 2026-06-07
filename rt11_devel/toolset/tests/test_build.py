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

from build import BuildPlan, RECIPES, load_manifest      # noqa: E402


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
        assert plan.commands == ["RUN DZ2:MACRO FOO", "RUN DZ2:LINK FOO"]

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
            "RUN DZ2:PAS1 BAR=BAR",
            "RUN DZ2:MACRO BAR",
            "RUN DZ2:LINK BAR,PASLIB,PAS1",
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
        staged_names = [Path(f).name for f in plan.staged_files()]
        assert "MYLIB.OBJ" in staged_names
        assert "MACRO.SAV" in staged_names
        assert "QUX.MAC" in staged_names

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
