#!/usr/bin/env python3
"""app.help_exits — ASKING THE BINARY WHAT IT ACCEPTS NEVER STARTS IT (lane HELP-FLAG-1).

Two agents once ran `Jahshaka --help`, which the binary did not handle: it booted the whole
editor on the owner's display with his data root. The contract, on the REAL binary:

  * `--help` prints the usage and exits 0; `--version` prints one line and exits 0;
  * an unknown `--flag` prints ONE line on stderr and exits 2;
  * none of them opens a window: the row runs with DISPLAY unset and QT_QPA_PLATFORM=xcb, so a
    process that reached QApplication would abort on the missing display (exit != 0/2 with
    Qt's "could not connect to display" text) — the exit code IS the no-window proof;
  * none of them writes a file: the working directory, HOME, the XDG dirs and the data root
    are fresh empty directories that must stay empty, and the build tree's own
    `jahsettings.ini` (the settings file a Debug run with no data root uses) keeps its bytes.

Usage: test_help_exits.py <path to Jahshaka>
"""
import hashlib
import os
import subprocess
import sys
import tempfile

failures = 0


def check(cond, msg):
    global failures
    print(("ok:   " if cond else "FAIL: ") + msg)
    if not cond:
        failures += 1


def tree_files(root):
    out = []
    for d, _, files in os.walk(root):
        for f in files:
            out.append(os.path.relpath(os.path.join(d, f), root))
        if d != root:
            out.append(os.path.relpath(d, root) + "/")
    return sorted(out)


def digest(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def run(app, args, with_data_root):
    with tempfile.TemporaryDirectory(prefix="jah-help-") as tmp:
        dirs = {k: os.path.join(tmp, k) for k in ("cwd", "home", "cfg", "data", "cache", "root")}
        for d in dirs.values():
            os.makedirs(d)
        env = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY", "JAHSHAKA_DATA_ROOT")}
        env.update(HOME=dirs["home"], XDG_CONFIG_HOME=dirs["cfg"], XDG_DATA_HOME=dirs["data"],
                   XDG_CACHE_HOME=dirs["cache"], QT_QPA_PLATFORM="xcb")
        if with_data_root:
            env["JAHSHAKA_DATA_ROOT"] = dirs["root"]
        r = subprocess.run([app] + args, cwd=dirs["cwd"], env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, timeout=60)
        written = tree_files(tmp)
        written = [w for w in written if w.rstrip("/") not in dirs]
        return r.returncode, r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace"), written


def main():
    app = os.path.realpath(sys.argv[1])
    settings = os.path.join(os.path.dirname(app), "jahsettings.ini")
    before = digest(settings)
    for data_root in (True, False):
        tag = "with a data root" if data_root else "with NO data root"

        rc, out, err, written = run(app, ["--help"], data_root)
        check(rc == 0, f"--help exits 0 ({tag}; rc={rc}; stderr={err.strip()[:200]!r})")
        check("Usage: Jahshaka" in out and "--data-root" in out, f"--help prints the usage ({tag})")
        check(not written, f"--help writes no file ({tag}; wrote {written[:5]})")

        rc, out, err, written = run(app, ["--version"], data_root)
        check(rc == 0 and out.startswith("Jahshaka"), f"--version prints one line and exits 0 ({tag}; rc={rc}; {out.strip()!r})")
        check(not written, f"--version writes no file ({tag}; wrote {written[:5]})")

        rc, out, err, written = run(app, ["--no-such-flag"], data_root)
        lines = [l for l in err.splitlines() if l.strip()]
        check(rc == 2, f"an unknown --flag exits 2 ({tag}; rc={rc})")
        check(len(lines) == 1 and "--no-such-flag" in lines[0], f"the refusal is ONE stderr line naming it ({tag}; {lines[:3]})")
        check(not written, f"an unknown --flag writes no file ({tag}; wrote {written[:5]})")

        rc, out, err, written = run(app, ["--data-root", "/nonexistent-jah", "--help", "--no-such-flag"], data_root)
        check(rc == 0 and "Usage: Jahshaka" in out and not written,
              f"--help wins over every other argument ({tag}; rc={rc})")
    check(digest(settings) == before, "the build tree's jahsettings.ini keeps its bytes")
    print("FAILED: %d check(s)" % failures if failures else "ALL CHECKS PASSED")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
