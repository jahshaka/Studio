#!/usr/bin/env python3
"""tooling.width_pair - scripts/width_pair.py (TEST-STREAMLINE-1 4b; the run itself is card block C) on TOYS: a hand-written
ctest tree whose rows are shell sleeps claiming the card's 8 slots, a fake card lock (records each hold's label, runs the
command) and a fake nvidia-smi (the used MB = a base + 1000 per toy row running). No GPU, no display, no lock.

usage: width_pair_test.py <source root> <scratch base> plan|judge|refuse   (three rows split by proof, each under the CPU row's 10 s)
Proves: the row set (a GPU row, priced under 10 s, no measure / cold / armed timing bars / named timing law; an unpriced or a
10-s row left out, each with its reason; a CPU row not asked); a width W run is `ctest -j W` with W x 8 slots (the toy rows
overlap W at once: the fake card's peak); the ABBA order and one hold per (width, part) with its label; the parts under the
cap; the verdicts per row incl. the fixture setup ctest adds; THE RULE: identical verdicts -> the widest width; a row red
only beside a neighbour -> that width refused by name; an out-of-memory line -> refused; a row past the row cap -> refused;
width 1 disagreeing with itself -> no width (exit 1); the card held -> exit 75 with the records kept; a bad display refused."""
import json
import os
import shutil
import subprocess
import sys
import tempfile

SRC, BASE, PART = sys.argv[1], sys.argv[2], sys.argv[3]
WP = os.path.join(SRC, "scripts", "width_pair.py")
os.makedirs(BASE, exist_ok=True)
T = tempfile.mkdtemp(prefix=f"width-pair-{PART}-", dir=BASE)
VR = os.path.join(T, "vram")
os.makedirs(VR)
fails = []


def check(cond, what):
    print(("ok:   " if cond else "FAIL: ") + what, flush=True)
    if not cond:
        fails.append(what)


ROW = """#!/bin/sh
# a toy GPU row: holds its 'VRAM' (a file) while it runs; $1 = its mode
f="$VRAMDIR/$(basename "$0").$$"; : > "$f"; trap 'rm -f "$f"' EXIT
n() { ls "$VRAMDIR" | wc -l; }
case "$1" in
  plain) sleep 0.12 ;;
  conflict) i=0; while [ $i -lt 12 ]; do [ "$(n)" -gt 1 ] && { echo "a neighbour on the card"; exit 1; }; sleep 0.05; i=$((i+1)); done ;;
  oom) i=0; while [ $i -lt 12 ]; do [ "$(n)" -gt 2 ] && { echo "vkAllocateMemory: VK_ERROR_OUT_OF_DEVICE_MEMORY"; exit 0; }; sleep 0.05; i=$((i+1)); done ;;
  slow) sleep 0.15; [ "$(n)" -gt 1 ] && sleep 1.0; exit 0 ;;
  flaky) c="$VRAMDIR/../flaky.count"; k=$(cat "$c" 2>/dev/null || echo 0); echo $((k+1)) > "$c"; [ "$k" = 2 ] && exit 1; sleep 0.1 ;;
esac
exit 0
"""
LOCK = """#!/bin/sh
# the fake card lock: gpu-exclusive.sh's argument shape; records the label; LOCK75=1 = the card stayed held
while [ $# -gt 0 ]; do case "$1" in --wait-max|--run-timeout) shift 2 ;; --label) echo "$2" >> "$LOCKLOG"; shift 2 ;; *) break ;; esac; done
[ "$LOCK75" = 1 ] && { echo "gpu-lock: the card stayed held"; exit 75; }
exec "$@"
"""
SMI = """#!/bin/sh
# the fake nvidia-smi: --query-gpu=memory.used ... -lms=<ms>: 500 + 1000 MB per toy row running, every 20 ms until killed
while :; do echo $((500 + 1000 * $(ls "$VRAMDIR" | wc -l))); sleep 0.02; done
"""


def write(path, text, mode=0o755):
    with open(path, "w") as fh:
        fh.write(text)
    os.chmod(path, mode)


write(os.path.join(T, "row.sh"), ROW)
write(os.path.join(T, "lock.sh"), LOCK)
write(os.path.join(T, "smi.sh"), SMI)


def toy(name, extra):
    """A toy build: its CTestTestfile.cmake, its prices, its cold-rows file. `extra` = [(row, mode, props)]."""
    b = os.path.join(T, name)
    os.makedirs(os.path.join(b, "tests"))
    rows = [("toy.a", "plain", ""), ("toy.b", "plain", ""), ("toy.c", "plain", ""), ("toy.d", "plain", ""),
            ("toy.measure", "plain", 'LABELS "engine;measure"'), ("toy.cold", "plain", "JAHSHAKA_COLD_BY_SUBJECT TRUE"),
            ("toy.coldlist", "plain", ""), ("toy.timing", "plain", 'ENVIRONMENT "JAHSHAKA_TIMING_BARS=1"'),
            ("gi.walk_court", "plain", ""), ("toy.big", "plain", ""), ("toy.unpriced", "plain", "")] + extra
    lines = ['add_test(toy.home "/bin/true")', 'set_tests_properties(toy.home PROPERTIES FIXTURES_SETUP toyHome)',
             'add_test(toy.cpu "/bin/true")']
    for n, mode, props in rows:
        lines.append(f'add_test({n} "{T}/row.sh" "{mode}")')
        lines.append(f'set_tests_properties({n} PROPERTIES RESOURCE_GROUPS "vram:8" {props})')
    lines.append("set_tests_properties(toy.a PROPERTIES FIXTURES_REQUIRED toyHome)")
    for n, _, props in extra:
        if "COST" not in props:
            lines.append(f"set_tests_properties({n} PROPERTIES COST 100)")
    with open(os.path.join(b, "CTestTestfile.cmake"), "w") as fh:
        fh.write("\n".join(lines) + "\n")
    pr = {n: 0.3 for n, _, _ in rows}
    pr.pop("toy.unpriced")
    pr["toy.big"] = 12.0
    with open(os.path.join(b, "gate-times.txt"), "w") as fh:
        fh.write("# toy prices\n" + "".join(f"{n} {s}  toy\n" for n, s in pr.items()))
    with open(os.path.join(b, "tests", "test-cold-rows.txt"), "w") as fh:
        fh.write(f"toy.coldlist\t{b}\t\t/bin/true\n")
    return b


def run(b, *args, env=None):
    log = os.path.join(T, "lock.log")
    if os.path.exists(log):
        os.remove(log)
    e = dict(os.environ, VRAMDIR=VR, LOCKLOG=log, **(env or {}))
    out = os.path.join(T, os.path.basename(b) + "-out")
    shutil.rmtree(out, ignore_errors=True)
    p = subprocess.run([sys.executable, "-I", WP, b, "--prices", os.path.join(b, "gate-times.txt"),
                        "--lock", os.path.join(T, "lock.sh"), "--nvidia-smi", os.path.join(T, "smi.sh"), "--out", out,
                        *args], env=e, capture_output=True, text=True)
    labels = open(log).read().split() if os.path.exists(log) else []
    recs = [json.loads(ln) for ln in open(os.path.join(out, "runs.jsonl"))] if os.path.exists(
        os.path.join(out, "runs.jsonl")) else []
    return p.returncode, p.stdout + p.stderr, labels, recs


b1 = toy("clean", [])
if PART == "plan":
    # 1. THE PLAN: the row set and every exclusion with its reason; nothing run, no hold
    rc, out, labels, recs = run(b1, "--plan")
    check(rc == 0 and "4 rows with no timing law priced under 10 s" in out and not labels and not recs,
          f"--plan: the 4 small GPU rows, nothing run, no hold (rc {rc})")
    for why, row in (("measure", "toy.measure"), ("cold", "toy.cold"), ("cold", "toy.coldlist"),
                     ("timing bars armed", "toy.timing"), ("a timing law", "gi.walk_court"),
                     ("priced 10 s or more", "toy.big"), ("unpriced", "toy.unpriced")):
        check(any(ln.startswith(f"  left out, {why}:") and row in ln for ln in out.splitlines()),
              f"--plan: {row} left out, '{why}'")
    check("toy.cpu" not in out and "toy.home" not in out, "--plan: a CPU row and a fixture setup are not asked (no card claim)")
    rc, out, labels, recs = run(b1, "--plan", "--part-cap-s", "0.7")
    check(rc == 0 and "2 part(s)" in out and "part 2: 2 rows" in out,
          f"--plan --part-cap-s 0.7: the 4 rows (0.33 s each at width 1) packed into 2 parts under the cap")

    # 2. A BAD DISPLAY IS REFUSED before any hold
    for d in (None, ":0", ":12"):
        rc, out, labels, recs = run(b1, *(["--display", d] if d else []))
        check(rc == 2 and "a rig display" in out and not labels, f"--display {d}: refused (exit 2), no hold")

    # 8. THE CARD STAYED HELD: exit 75 at once, the record kept, no further hold
    rc, out, labels, recs = run(b1, "--display", ":71", env={"LOCK75": "1"})
    check(rc == 75 and len(labels) == 1 and len(recs) == 1 and recs[0]["held"] is False,
          f"held: the card stayed held - exit 75 after the first hold's wait, its record kept (rc {rc}, {len(labels)} hold(s))")


elif PART == "judge":
    # 3. A CLEAN SET: the ABBA order, one hold per run, W rows at once at width W, every verdict equal -> the widest width
    rc, out, labels, recs = run(b1, "--display", ":71")
    check(rc == 0 and "ADOPT WIDTH 3" in out, f"clean: ADOPT WIDTH 3 (rc {rc}) {out.splitlines()[-1:]}")
    check(labels == ["width-w1-p1", "width-w2-p1", "width-w3-p1", "width-w3-p1", "width-w2-p1", "width-w1-p1"],
          f"clean: six holds in ABBA order 1,2,3,3,2,1, each labelled ({labels})")
    peaks = {r["width"]: r["vram_peak_mb"] for r in recs}
    check(peaks.get(1) == 1500 and peaks.get(3, 0) >= 3500 and peaks.get(2, 0) >= 2500,
          f"clean: the VRAM peak read per width - 1 row at once at width 1, 3 at width 3 ({peaks})")
    check(all(r["rows"].get("toy.home", [None])[0] == "PASS" for r in recs),
          "clean: the fixture setup ctest adds is run and judged with its rows")
    check(all(set(r["rows"]) == {"toy.a", "toy.b", "toy.c", "toy.d", "toy.home"} for r in recs),
          "clean: only the asked rows (and their setup) ran - no excluded row")
    tsv = open(os.path.join(T, "clean-out", "summary.tsv")).read().splitlines()
    check(len(tsv) == 4 and tsv[1].startswith("1\t2\t"), "clean: summary.tsv - one line per width, its runs, walls, peak, the slowest row")

    # 4. A ROW RED ONLY BESIDE A NEIGHBOUR: width 2 refused by name -> width 1
    b2 = toy("conflict", [("toy.conflict", "conflict", "")])
    rc, out, labels, recs = run(b2, "--display", ":71", "--order", "1,2,2,1")
    check(rc == 0 and "ADOPT WIDTH 1" in out and "WIDTH 2 REFUSED: toy.conflict PASS at width 1, FAIL/FAIL at width 2" in out,
          f"conflict: width 2 refused by the row that fails beside a neighbour, ADOPT WIDTH 1 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

elif PART == "refuse":
    # 5. AN OUT-OF-MEMORY LINE at width 3 (the row still passes): width 3 refused -> width 2
    b3 = toy("oom", [("toy.oom", "oom", "")])
    rc, out, labels, recs = run(b3, "--display", ":71", "--order", "1,2,3")
    check(rc == 0 and "ADOPT WIDTH 2" in out and "WIDTH 3 REFUSED:" in out and "out-of-memory" in out,
          f"oom: width 3 refused on its out-of-memory line though every verdict held, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 6. A ROW PAST THE ROW CAP beside a neighbour: refused -> width 1
    b4 = toy("slow", [("toy.slow", "slow", "")])
    rc, out, labels, recs = run(b4, "--display", ":71", "--order", "1,2", "--row-cap-s", "0.9")
    check(rc == 0 and "ADOPT WIDTH 1" in out and "toy.slow ran" in out and "past 1 s" in out,
          f"slow: a row inflated past the row cap at width 2 refuses it (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 7. WIDTH 1 DISAGREES WITH ITSELF: no width adopted (exit 1)
    b5 = toy("flaky", [("toy.flaky", "flaky", "")])
    rc, out, labels, recs = run(b5, "--display", ":71", "--order", "1,2,1")
    check(rc == 1 and "WIDTH 1 DISAGREES WITH ITSELF: toy.flaky PASS / FAIL" in out and "NO WIDTH ADOPTED" in out,
          f"flaky: a row whose width-1 runs differ - no width adopted, exit 1 (rc {rc})")

else:
    check(False, f"unknown part {PART!r} (plan|judge|refuse)")
shutil.rmtree(T, ignore_errors=True)
print("width_pair_test: " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
sys.exit(1 if fails else 0)
