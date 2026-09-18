"""mkpatches.py - export Omega's changes as patches of DEC's sources.

    python mkpatches.py WORKREPO

WORKREPO is a git repository whose first commit is DEC's files as they are
(LF line ends) and every later commit one architectural difference of
Omega's.  Each commit becomes patches/NN-<slug>.diff - its message as the
header, then the diff - and patches/series lists them in order.
"""
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def git(repo, *args):
    return subprocess.run(["git", "-C", str(repo), *args], check=True,
                          capture_output=True).stdout.decode("latin-1")


def main():
    repo = Path(sys.argv[1])
    commits = git(repo, "rev-list", "--reverse", "HEAD").split()
    out = HERE / "patches"
    out.mkdir(exist_ok=True)
    for old in out.glob("*.diff"):
        old.unlink()
    series = []
    for i, c in enumerate(commits[1:], 1):
        msg = git(repo, "log", "-1", "--format=%B", c).strip()
        slug = re.sub(r"[^a-z0-9]+", "-", msg.splitlines()[0].lower()).strip("-")[:40]
        name = f"{i:02d}-{slug}.diff"
        diff = git(repo, "diff", "--no-color", "--no-renames", f"{c}^", c)
        head = "".join(f"# {l}\n" if l else "#\n" for l in msg.splitlines())
        (out / name).write_bytes((head + "\n" + diff).encode("latin-1"))
        series.append(name)
        print(name)
    (out / "series").write_text("".join(s + "\n" for s in series))


if __name__ == "__main__":
    main()
