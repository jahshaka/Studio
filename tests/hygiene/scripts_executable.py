#!/usr/bin/env python3
# source.scripts_executable (TE-TOOLS-3, 2026-11-21) - A SCRIPT OUR TEXTS TELL A PERSON TO EXECUTE IS EXECUTABLE.
#
# WHY: scripts/gate_runlog.py, kernel_xid.py, ci_gate_check.py and coverage_map.py carried a shebang and usage lines in the
# executable form (`scripts/gate_runlog.py run ...`, the triage skill's `scripts/kernel_xid.py since ...`, GATE_AND_RIG.md's
# `scripts/coverage_map.py audit ...`) while git held them at mode 100644: the line as written fails "Permission denied"
# (exit 126). Every code caller runs them as `python3 <path>`, which works at either mode - the texts were the ones wrong.
# THE LAW: every tracked file directly under scripts/ whose first two bytes are "#!" (the file's own claim that it is run)
# has git mode 100755 in the index (the mode a checkout gives every tree - never the working copy's, which can drift).
# A NEGATIVE CONTROL runs first: a toy repo with one shebang script at 100644 is refused, at 100755 passes.
import os
import subprocess
import sys
import tempfile


def offenders(root):
    """[path] of tracked scripts/<file> with a shebang and an index mode other than 100755."""
    out = subprocess.run(["git", "-C", root, "ls-files", "-s", "--", "scripts"], capture_output=True, text=True, check=True).stdout
    bad = []
    for line in out.splitlines():
        meta, _, path = line.partition("\t")
        if path.count("/") != 1:
            continue
        try:
            with open(os.path.join(root, path), "rb") as f:
                shebang = f.read(2) == b"#!"
        except OSError:
            continue
        if shebang and meta.split()[0] != "100755":
            bad.append(path)
    return bad


def self_test():
    with tempfile.TemporaryDirectory() as t:
        g = ["git", "-C", t, "-c", "user.name=t", "-c", "user.email=t@t"]
        subprocess.run(["git", "init", "-q", t], check=True)
        os.makedirs(os.path.join(t, "scripts"))
        for n, body in (("tool.py", "#!/usr/bin/env python3\n"), ("lib.py", "x = 1\n")):
            with open(os.path.join(t, "scripts", n), "w") as f:
                f.write(body)
        subprocess.run(g + ["add", "scripts"], check=True)
        subprocess.run(g + ["update-index", "--chmod=-x", "scripts/tool.py"], check=True)
        assert offenders(t) == ["scripts/tool.py"], offenders(t)
        subprocess.run(g + ["update-index", "--chmod=+x", "scripts/tool.py"], check=True)
        assert offenders(t) == [], offenders(t)
    print("self-test: a shebang script at 100644 refused, at 100755 passed; a module without a shebang never asked", flush=True)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    self_test()
    bad = offenders(root)
    for p in bad:
        print(f"FAIL: {p} carries a shebang but is not executable in git (mode 100644): `git update-index --chmod=+x {p}`, "
              "or drop the shebang if no text tells anyone to run it", flush=True)
    print(f"source.scripts_executable: {'PASS' if not bad else 'FAIL'} ({len(bad)} of the shebang scripts under scripts/ not "
          "executable)", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
