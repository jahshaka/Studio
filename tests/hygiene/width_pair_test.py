#!/usr/bin/env python3
"""tooling.width_pair - scripts/width_pair.py (TEST-STREAMLINE-1 4b; the run itself is card block C) on TOYS: a hand-written
ctest tree whose rows are shell sleeps claiming the card's 8 slots, a fake card lock (records each hold's label, runs the
command) and a fake nvidia-smi (the used MB = a base + 1000 per toy row running). No GPU, no display, no lock.

usage: width_pair_test.py <source root> <scratch base> plan|judge|refuse|bars|gain   (rows split by proof, each under the CPU row's 10 s)
Proves: the row set (a GPU row, priced under 10 s, no measure / cold / armed timing bars / named timing law; an unpriced or a
10-s row left out, each with its reason; a CPU row not asked); a width W run is `ctest -j W` with W x 8 slots (the toy rows
overlap W at once: the fake card's peak); the palindrome order and one hold per (width, part) with its label; the parts under the
cap; the verdicts per row incl. the fixture setup ctest adds; THE RULE: identical verdicts -> the widest width; a row red
only beside a neighbour -> that width refused by name; an out-of-memory line -> refused; a row past the row cap -> refused;
width 1 disagreeing with itself -> no width (exit 1); the card held -> exit 75 with the records kept; a bad display refused;
THE BARS (the lead's rulings on the 4-5 audit): a row past x1.5 its width-1 wall, or past its class budget, refuses the width;
a width whose total wall is not 15 % under the next narrower width's is refused; a VRAM peak past 90 % of memory.total, or an
unread total, refuses the width."""
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
  plain) sleep 0.2 ;;
  inflate) i=0; s=0; while [ $i -lt 4 ]; do [ "$(n)" -gt 1 ] && s=1; sleep 0.05; i=$((i+1)); done; [ $s = 1 ] && sleep 0.3; exit 0 ;;
  near) i=0; s=0; while [ $i -lt 16 ]; do [ "$(n)" -gt 1 ] && s=1; sleep 0.05; i=$((i+1)); done; [ $s = 1 ] && sleep 0.3; exit 0 ;;
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
# the fake nvidia-smi: --query-gpu=memory.total: $SMITOTAL MB (16376 by default; SMITOTAL=none: no reading);
# --query-gpu=memory.used ... -lms=<ms>: 500 + 1000 MB per toy row running, every 20 ms until killed
case "$1" in *memory.total*) [ "${SMITOTAL:-}" = none ] && exit 9; echo "${SMITOTAL:-16376}"; exit 0 ;; esac
while :; do echo $((500 + 1000 * $(ls "$VRAMDIR" | wc -l))); sleep 0.02; done
"""


def write(path, text, mode=0o755):
    with open(path, "w") as fh:
        fh.write(text)
    os.chmod(path, mode)


write(os.path.join(T, "row.sh"), ROW)
write(os.path.join(T, "lock.sh"), LOCK)
write(os.path.join(T, "smi.sh"), SMI)


def toy(name, extra, plain=4):
    """A toy build: its CTestTestfile.cmake, its prices, its cold-rows file. `extra` = [(row, mode, props)]; `plain` plain
    rows toy.a, toy.b, ... (0.2 s each)."""
    b = os.path.join(T, name)
    os.makedirs(os.path.join(b, "tests"))
    rows = [(f"toy.{chr(97 + i)}", "plain", "") for i in range(plain)] + [
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


b1 = toy("clean", [], plain=6)
if PART == "plan":
    # 1. THE PLAN: the row set and every exclusion with its reason; nothing run, no hold
    rc, out, labels, recs = run(b1, "--plan")
    check(rc == 0 and "6 rows with no timing law priced under 10 s" in out and not labels and not recs,
          f"--plan: the 6 small GPU rows, nothing run, no hold (rc {rc})")
    for why, row in (("measure", "toy.measure"), ("cold", "toy.cold"), ("cold", "toy.coldlist"),
                     ("timing bars armed", "toy.timing"), ("a timing law", "gi.walk_court"),
                     ("priced 10 s or more", "toy.big"), ("unpriced", "toy.unpriced")):
        check(any(ln.startswith(f"  left out, {why}:") and row in ln for ln in out.splitlines()),
              f"--plan: {row} left out, '{why}'")
    check("toy.cpu" not in out and "toy.home" not in out, "--plan: a CPU row and a fixture setup are not asked (no card claim)")
    rc, out, labels, recs = run(b1, "--plan", "--part-cap-s", "0.7")
    check(rc == 0 and "3 part(s)" in out and "part 3: 2 rows" in out,
          f"--plan --part-cap-s 0.7: the 6 rows (0.33 s each at width 1) packed into 3 parts under the cap")

    # 2. A BAD DISPLAY IS REFUSED before any hold
    for d in (None, ":0", ":12"):
        rc, out, labels, recs = run(b1, *(["--display", d] if d else []))
        check(rc == 2 and "a rig display" in out and not labels, f"--display {d}: refused (exit 2), no hold")

    # 8. THE CARD STAYED HELD: exit 75 at once, the record kept, no further hold
    rc, out, labels, recs = run(b1, "--display", ":71", env={"LOCK75": "1"})
    check(rc == 75 and len(labels) == 1 and len(recs) == 1 and recs[0]["held"] is False,
          f"held: the card stayed held - exit 75 after the first hold's wait, its record kept (rc {rc}, {len(labels)} hold(s))")


elif PART == "judge":
    # 3. A CLEAN SET: the default order 1,2,3,2,1, one hold per run, W rows at once at width W, every verdict equal -> the widest width
    rc, out, labels, recs = run(b1, "--display", ":71")
    check(rc == 0 and "ADOPT WIDTH 3" in out, f"clean: ADOPT WIDTH 3 (rc {rc}) {out.splitlines()[-1:]}")
    check(labels == ["width-w1-p1", "width-w2-p1", "width-w3-p1", "width-w2-p1", "width-w1-p1"],
          f"clean: five holds in the default order 1,2,3,2,1, each labelled ({labels})")
    peaks = {r["width"]: r["vram_peak_mb"] for r in recs}
    check(peaks.get(1) == 1500 and peaks.get(3, 0) >= 3500 and peaks.get(2, 0) >= 2500,
          f"clean: the VRAM peak read per width - 1 row at once at width 1, 3 at width 3 ({peaks})")
    check(all(r["rows"].get("toy.home", [None])[0] == "PASS" for r in recs),
          "clean: the fixture setup ctest adds is run and judged with its rows")
    check(all(set(r["rows"]) == {"toy.a", "toy.b", "toy.c", "toy.d", "toy.e", "toy.f", "toy.home"} for r in recs),
          "clean: only the asked rows (and their setup) ran - no excluded row")
    tsv = open(os.path.join(T, "clean-out", "summary.tsv")).read().splitlines()
    check(len(tsv) == 4 and tsv[1].startswith("1\t2\t"), "clean: summary.tsv - one line per width, its runs, walls, peak, the slowest row")
    check(all(r.get("ctest_wall_s") is not None and r.get("vram_total_mb") == 16376 for r in recs),
          "clean: each run records ctest's own total time and the card's memory.total")

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

elif PART == "bars":
    # 9. THE INFLATION BAR: a row at x2.5 its width-1 wall beside a neighbour, still under its class budget and the row cap
    #    (10 s) - width 2 refused by name -> width 1 (the base adopted width 2)
    bi = toy("inflate", [("toy.inflate", "inflate", "")])
    rc, out, labels, recs = run(bi, "--display", ":71", "--order", "1,2")
    ref = [ln for ln in out.splitlines() if "REFUSED" in ln]
    check(rc == 0 and "ADOPT WIDTH 1" in out and len(ref) == 1 and "toy.inflate ran" in ref[0] and "its width-1 wall" in ref[0]
          and "class budget" not in ref[0], f"inflate: a row inflated past x1.5 its width-1 wall refuses width 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 10. THE CLASS BUDGET (x0.1 for the toys: 1 s): a row 0.8 s alone, 1.1 s beside a neighbour (x1.4: under the inflation bar) past its 1-s budget
    bn = toy("near", [("toy.near", "near", "")])
    rc, out, labels, recs = run(bn, "--display", ":71", "--order", "1,2", "--budget-scale", "0.1")
    ref = [ln for ln in out.splitlines() if "REFUSED" in ln]
    check(rc == 0 and "ADOPT WIDTH 1" in out and len(ref) == 1 and "toy.near ran" in ref[0]
          and "past its class budget 1 s" in ref[0] and "its width-1 wall" not in ref[0],
          f"near: a row past its class budget at width 2 (under x1.5) refuses width 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 4. A ROW RED ONLY BESIDE A NEIGHBOUR: width 2 refused by name -> width 1
    b2 = toy("conflict", [("toy.conflict", "conflict", "")])
    rc, out, labels, recs = run(b2, "--display", ":71", "--order", "1,2")
    check(rc == 0 and "ADOPT WIDTH 1" in out and "WIDTH 2 REFUSED: toy.conflict PASS at width 1, FAIL at width 2" in out,
          f"conflict: width 2 refused by the row that fails beside a neighbour, ADOPT WIDTH 1 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

elif PART == "gain":
    # 11. THE WALL GAIN: 4 rows at width 3 take two rounds, as at width 2 - width 3 refused for no gain -> width 2
    bg = toy("nogain", [])
    rc, out, labels, recs = run(bg, "--display", ":71", "--order", "1,2,3")
    check(rc == 0 and "ADOPT WIDTH 2" in out and "WIDTH 3 REFUSED: total wall" in out and "the bar: 15 % under" in out,
          f"nogain: width 3 no faster than width 2 is refused, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 12. THE VRAM HEADROOM: a 3500-MB card - width 3's peak (500 + 3 x 1000) past 90 % of it, width 2's (2500) under
    bv = toy("headroom", [], plain=6)      # (never "vram": the fake card's directory)
    rc, out, labels, recs = run(bv, "--display", ":71", "--order", "1,2,3", env={"SMITOTAL": "3500"})
    check(rc == 0 and "ADOPT WIDTH 2" in out and "WIDTH 3 REFUSED: VRAM peak 3500 MB, past 90 % of the card's 3500 MB" in out,
          f"vram: width 3's peak past 90 % of memory.total refuses it, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 13. AN UNREAD TOTAL: nvidia-smi gives no memory.total - no headroom measured, width 2 refused -> width 1
    rc, out, labels, recs = run(bg, "--display", ":71", "--order", "1,2", env={"SMITOTAL": "none"})
    check(rc == 0 and "ADOPT WIDTH 1" in out and "the VRAM total unread" in out,
          f"vram unread: no memory.total refuses width 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

else:
    check(False, f"unknown part {PART!r} (plan|judge|refuse|bars|gain)")
shutil.rmtree(T, ignore_errors=True)
print("width_pair_test: " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
sys.exit(1 if fails else 0)
