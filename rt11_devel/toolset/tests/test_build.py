"""Tests for the universal build driver: manifest parsing, recipe lookup,
{name} substitution, plan resolution, the toolchain found in the collection,
and a command run through ms0515-run (a stand-in for it)."""

from __future__ import annotations

import sys
import textwrap
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
TOOLSET = HERE.parent
sys.path.insert(0, str(TOOLSET))

import build                                             # noqa: E402
from build import BuildPlan, RECIPES, load_manifest      # noqa: E402
from rt11 import RT11CommandError                        # noqa: E402


def write_manifest(tmp_path: Path, content: str) -> Path:
    p = tmp_path / "build.toml"
    p.write_text(textwrap.dedent(content), encoding="utf-8")
    return p


# ── recipe table sanity ─────────────────────────────────────────────────────

class TestRecipeTable:
    def test_known_languages(self):
        assert {"macro11", "pascal", "fortran", "basic"} <= set(RECIPES)

    @pytest.mark.parametrize("lang", ["macro11", "pascal", "fortran"])
    def test_every_buildable_recipe_links(self, lang):
        assert "LINK.SAV" in RECIPES[lang]["compilers"]
        assert "SYSLIB.OBJ" in RECIPES[lang]["libs"]

    @pytest.mark.parametrize("lang", ["macro11", "pascal", "fortran"])
    def test_commands_are_in_the_programs_own_syntax(self, lang):
        # ms0515-run hands the line to the program, not to the monitor: a
        # line without `=` names inputs alone and writes nothing.
        for command in RECIPES[lang]["commands"]:
            program, _, line = command.partition(" ")
            assert program + ".SAV" in RECIPES[lang]["compilers"]
            assert "=" in line

    def test_fortran_compiles_to_an_object_itself(self):
        # FORTRA writes the .OBJ; there is no MACRO pass.
        assert [c.split()[0] for c in RECIPES["fortran"]["commands"]] == ["FORTRA", "LINK"]

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
        assert plan.commands == ["MACRO FOO=FOO", "LINK FOO=FOO"]

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
            "MACRO BAR=BAR",
            "LINK BAR=BAR,PASLIB,PAS1",
        ]
        assert "PASLIB.OBJ" in plan.recipe_libs

    def test_custom_commands_override_recipe(self, tmp_path):
        m = write_manifest(tmp_path, """
            [project]
            name     = "OVR"
            language = "macro11"
            [build]
            commands = ["MACRO {name},{name}={name}", "LINK {name}={name},MYLIB"]
        """)
        plan = load_manifest(m)
        assert plan.commands == [
            "MACRO OVR,OVR=OVR",
            "LINK OVR=OVR,MYLIB",
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
        dk = {Path(f).name for f in plan.dk_files()}
        assert "MYLIB.OBJ" in dk          # extra lib -> the work folder
        assert "QUX.MAC" in dk             # source    -> the work folder

    def test_extra_lib_of_the_project_beats_the_collections(self, tmp_path):
        (tmp_path / "QUX.MAC").write_bytes(b"")
        (tmp_path / "MYLIB.OBJ").write_bytes(b"")
        m = write_manifest(tmp_path, """
            [project]
            name     = "QUX"
            language = "macro11"
            [build]
            libs = ["MYLIB.OBJ"]
        """)
        plan = load_manifest(m)
        assert tmp_path / "MYLIB.OBJ" in plan.dk_files()

    def test_the_toolchain_comes_from_the_collection(self, tmp_path, monkeypatch):
        # DEC's tools and libraries lie in software/development; a kit's
        # toolchain has a folder of its own and, in fodos/, the system
        # libraries its programs link against - those win over DEC's.
        root = tmp_path / "collection"
        dev = root / "software" / "development"
        for folder, names in {
            dev: ["MACRO.SAV", "LINK.SAV", "SYSLIB.OBJ", "SYSMAC.SML"],
            dev / "pascal": ["PAS1.SAV", "PAS1.OBJ", "PASLIB.OBJ"],
            dev / "fodos": ["SYSLIB.OBJ", "SYSMAC.SML"],
        }.items():
            folder.mkdir(parents=True)
            for name in names:
                (folder / name).write_bytes(b"")
        (root / "disks.toml").write_bytes(b"")
        monkeypatch.setenv("MS0515_SOFTWARE", str(root))

        macro = load_manifest(write_manifest(tmp_path, """
            [project]
            name     = "M"
            language = "macro11"
        """)).tool_files()
        assert {f.name for f in macro} == {"MACRO.SAV", "LINK.SAV", "SYSLIB.OBJ", "SYSMAC.SML"}
        assert all(f.parent == dev for f in macro)

        pascal = {f.name: f for f in load_manifest(write_manifest(tmp_path, """
            [project]
            name     = "P"
            language = "pascal"
        """)).tool_files()}
        assert pascal["PAS1.SAV"].parent == dev / "pascal"
        assert pascal["SYSLIB.OBJ"].parent == dev / "fodos"
        assert pascal["MACRO.SAV"].parent == dev

    def test_a_tool_the_collection_lacks_is_named(self, tmp_path, monkeypatch):
        root = tmp_path / "collection"
        (root / "software" / "development").mkdir(parents=True)
        (root / "disks.toml").write_bytes(b"")
        monkeypatch.setenv("MS0515_SOFTWARE", str(root))
        plan = load_manifest(write_manifest(tmp_path, """
            [project]
            name     = "M"
            language = "macro11"
        """))
        with pytest.raises(SystemExit, match="MACRO.SAV"):
            plan.tool_files()

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


# ── a command through ms0515-run ────────────────────────────────────────────

class TestRunCommand:
    """run_command() with a stand-in for ms0515-run: a script that prints
    what it was given and ends as it is told."""

    def stand_in(self, tmp_path, monkeypatch, *, prints: str, status: int):
        script = tmp_path / "fake_run.py"
        script.write_text(textwrap.dedent(f"""
            import sys
            sys.stdout.write("ARGS " + " ".join(sys.argv[1:]) + "\\n")
            sys.stdout.write({prints!r})
            sys.exit({status})
        """), encoding="utf-8")
        monkeypatch.setattr(build, "RUNNER", [sys.executable, str(script)])

    def test_the_program_and_its_line_are_passed_apart(self, tmp_path, monkeypatch):
        self.stand_in(tmp_path, monkeypatch, prints="", status=0)
        out = build.run_command("LINK FOO,FOO=FOO,BAR", tmp_path)
        assert "ARGS LINK FOO,FOO=FOO,BAR" in out

    def test_a_failed_program_stops_the_build(self, tmp_path, monkeypatch):
        self.stand_in(tmp_path, monkeypatch, status=1,
                      prints="?MACRO-E-Errors detected:  3\n")
        with pytest.raises(RT11CommandError, match="Errors detected"):
            build.run_command("MACRO FOO=FOO", tmp_path)

    def test_an_error_printed_stops_it_whatever_the_status(self, tmp_path, monkeypatch):
        self.stand_in(tmp_path, monkeypatch, status=0,
                      prints="?LINK-F-File not found DK:FOO.OBJ\n")
        with pytest.raises(RT11CommandError, match="File not found"):
            build.run_command("LINK FOO=FOO", tmp_path)

    def test_a_warning_is_shown_and_does_not_stop_it(self, tmp_path, monkeypatch):
        self.stand_in(tmp_path, monkeypatch, status=0,
                      prints="?LINK-W-Undefined globals:\nMSGG\n")
        assert "Undefined globals" in build.run_command("LINK FOO=FOO", tmp_path)

    def test_a_failure_without_a_message_is_still_a_failure(self, tmp_path, monkeypatch):
        self.stand_in(tmp_path, monkeypatch, status=2, prints="")
        with pytest.raises(RT11CommandError, match="status 2"):
            build.run_command("MACRO FOO=FOO", tmp_path)


# ── error cases ─────────────────────────────────────────────────────────────

class TestManifestErrors:
    def test_missing_project_table(self, tmp_path):
        m = write_manifest(tmp_path, """
            [build]
            commands = ["WHATEVER X=X"]
        """)
        with pytest.raises(ValueError, match="project"):
            load_manifest(m)

    @pytest.mark.parametrize("key,value", [("commands", '["MACRO X"]'),
                                           ("libs", '["MYLIB.OBJ"]')])
    def test_build_keys_under_project_are_refused(self, tmp_path, key, value):
        # They used to be read from [build] alone and silently ignored
        # elsewhere, so the project built with the language's default recipe
        # and nobody was told - FIST lost three of its five commands that way.
        m = write_manifest(tmp_path, f"""
            [project]
            name     = "STRAY"
            language = "macro11"
            {key} = {value}
        """)
        with pytest.raises(ValueError, match=key):
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

class TestSystemFolderIsPristine:
    """system/ is the vvv104 ОМЕГА as a bootable folder: what the games'
    tests (fist, manicm) boot to run them - the base RT-11 set + the boot
    file + the descriptor, nothing else.  No build runs on it (build.py
    runs the tools with ms0515-run), and nothing may be staged into it."""

    EXPECTED = {"RT11SJ.SYS", "SWAP.SYS", "DZ.SYS", "TT.SYS",
                "PIP.SAV", "DUP.SAV", "DIR.SAV",
                "BOOT.BIN", "DEVICE.RTFS"}

    def test_folder_holds_only_the_base_system(self):
        system = TOOLSET / "system"
        if not system.is_dir():
            pytest.skip("toolset/system not present")
        names = {p.name.upper() for p in system.iterdir() if p.is_file()}
        assert names == self.EXPECTED, f"system/ template polluted: {names}"
