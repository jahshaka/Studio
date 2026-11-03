#!/usr/bin/env python3
"""bake.determinism.reference_run - THE SERIAL REFERENCE IS ONE CTEST CALL's (TEST-STREAMLINE-1 item 5; the 4-5 audit's
residue: the reference lived in ONE fixed directory of the build, so two ctest calls in one build raced on it - one call's
cleanup removing the other's reference, or a reader taking the other call's bytes). On a toy ctest tree whose rows ask
test_mesh_bake which directory is theirs (`--determinism-run-dir <root>`, through `sh` as the real rows run):
  1. the rows of ONE call agree (the setup and its readers share the reference);
  2. two calls running AT ONCE get different directories under <root>;
  3. `--determinism-clean <root>` (bake.determinism.reference.clean) removes its own call's directory and a dead call's,
     and keeps the directory of a call still running.
usage: bake_reference_run_test.py <test_mesh_bake> <scratch base>"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

EXE, BASE = sys.argv[1], sys.argv[2]
os.makedirs(BASE, exist_ok=True)
T = tempfile.mkdtemp(prefix="bake-ref-run-", dir=BASE)
ROOT = os.path.join(T, "serial-reference")
fails = []


def check(cond, what):
    print(("ok:   " if cond else "FAIL: ") + what, flush=True)
    if not cond:
        fails.append(what)


tree = os.path.join(T, "toy")
os.makedirs(tree)
with open(os.path.join(tree, "CTestTestfile.cmake"), "w") as fh:
    for r in ("setup", "reader"):
        # the rows sleep so the two calls overlap; each prints the directory its run owns
        fh.write(f'add_test({r} "/bin/sh" "-c" "sleep 0.3; \\"{EXE}\\" --determinism-run-dir \\"{ROOT}\\"")\n')
    fh.write(f'add_test(clean "{EXE}" "--determinism-clean" "{ROOT}")\n')
    fh.write('set_tests_properties(clean PROPERTIES DEPENDS "setup;reader")\n')


def call(*args):
    return subprocess.Popen(["ctest", "-V", "--test-dir", tree, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True)


one = call("-R", "^(setup|reader)$", "-j2")
two = call("-R", "^(setup|reader)$", "-j2")
o1, o2 = one.communicate()[0], two.communicate()[0]
d1 = re.findall(r"RUN-DIR (\S+)", o1)
d2 = re.findall(r"RUN-DIR (\S+)", o2)
check(len(d1) == 2 and len(set(d1)) == 1, f"one call: its setup and reader share one directory ({d1})")
check(len(d2) == 2 and len(set(d2)) == 1, f"the other call: likewise ({d2})")
check(d1 and d2 and d1[0] != d2[0] and all(d.startswith(ROOT + "/run-") for d in d1 + d2),
      f"two calls at once: two directories under the root ({d1[:1]} / {d2[:1]})")

# 3. the cleaner: its own call's directory and a dead call's go, a live call's stays
os.makedirs(os.path.join(ROOT, "run-1-0"))                       # pid 1 is never ctest: a dead call's
with open(os.path.join(tree, "CTestTestfile.cmake"), "a") as fh:
    fh.write('add_test(hold "/bin/sleep" "20")\n')
live = subprocess.Popen(["ctest", "--test-dir", tree, "-R", "^hold$"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
st = open(f"/proc/{live.pid}/stat").read()
livedir = os.path.join(ROOT, f"run-{live.pid}-{st[st.rindex(')') + 2:].split()[19]}")   # field 22: the start time
os.makedirs(livedir)
p = call("-R", "^clean$")
oc = p.communicate()[0]
mine = re.search(r"this run's: (run-\S+)\)", oc)
check(not os.path.exists(os.path.join(ROOT, "run-1-0")), "clean: a dead call's directory removed")
check(mine is not None and not os.path.exists(os.path.join(ROOT, mine.group(1))), "clean: its own call's directory removed")
check(os.path.isdir(livedir), "clean: a running call's directory kept")
live.kill()
live.wait()
shutil.rmtree(T, ignore_errors=True)
print("bake_reference_run_test: " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
sys.exit(1 if fails else 0)
