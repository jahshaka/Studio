#!/usr/bin/env python3
"""tooling.width_pair - scripts/width_pair.py (TEST-STREAMLINE-1 4b; the run itself is card block C) on TOYS: a hand-written
ctest tree whose rows are shell sleeps claiming the card's 8 slots, a fake card lock (records each hold's label, runs the
command) and a fake nvidia-smi (the used MB = a base + 1000 per toy row running). No GPU, no display, no lock.

usage: width_pair_test.py <source root> <scratch base> plan|judge|refuse|bars|gain|reds|log|budget   (rows split by proof, each under the CPU row's 10 s)
Proves: the row set (a GPU row, priced under 10 s, no measure / cold / armed timing bars / named timing law; an unpriced or a
10-s row left out, each with its reason; a CPU row not asked); a width W run is `ctest -j W` with W x 8 slots (the toy rows
overlap W at once: the fake card's peak); the palindrome order and one hold per (width, part) with its label; the parts under the
cap; the verdicts per row incl. the fixture setup ctest adds; THE RULE: identical verdicts -> the widest width; a row red
only beside a neighbour -> that width refused by name; an out-of-memory line -> refused; a row past the row cap -> refused;
width 1 disagreeing with itself -> no width (exit 1); the card held -> exit 75 with the records kept; a bad display refused;
THE BARS (the lead's rulings on the 4-5 audit): a row past x1.5 its width-1 wall, or past its class budget, refuses the width;
a width whose total wall is not 15 % under the next narrower width's is refused; a VRAM peak past 90 % of memory.total, or an
unread total, refuses the width; a red row agrees only on equal FAIL lines; the declared open reds (the toy spec's THE OPEN REDS
table, its live table only) leave the set by name, an unreadable spec refused; the OOM line read from the run's own ctest.log
when the build's Testing/Temporary is removed under it; the tree's check mark held for the run and a live foreign mark refusing."""
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
  inflate) i=0; s=0; while [ $i -lt 8 ]; do [ "$(n)" -gt 1 ] && s=1; sleep 0.05; i=$((i+1)); done; [ $s = 1 ] && sleep 1.2; exit 0 ;;
  redline) i=0; s=0; while [ $i -lt 6 ]; do [ "$(n)" -gt 1 ] && s=1; sleep 0.05; i=$((i+1)); done
           [ $s = 1 ] && echo "FAIL: vkQueueSubmit: VK_ERROR_DEVICE_LOST" || echo "FAIL: (1) 1.44 q over the 1-q bar"; exit 1 ;;
  samered) echo "FAIL: (1) 1.44 q over the 1-q bar"; sleep 0.2; exit 1 ;;
  mark) read p _ < "$MARKFILE" && kill -0 "$p" && exit 0; echo "FAIL: no live check mark at $MARKFILE"; exit 1 ;;
  oomclobber) i=0; while [ $i -lt 12 ]; do [ "$(n)" -gt 2 ] && { echo "vkAllocateMemory: VK_ERROR_OUT_OF_DEVICE_MEMORY"; rm -rf "$CLOBBER"; exit 0; }; sleep 0.05; i=$((i+1)); done ;;
  tiny) s=0; [ "$(n)" -gt 1 ] && s=1; sleep 0.02; [ "$(n)" -gt 1 ] && s=1; [ $s = 1 ] && sleep 0.05; exit 0 ;;
  near) i=0; s=0; while [ $i -lt 4 ]; do [ "$(n)" -gt 1 ] && s=1; sleep 0.05; i=$((i+1)); done; sleep 0.5; [ $s = 1 ] && sleep 0.4; exit 0 ;;
  conflict) i=0; while [ $i -lt 12 ]; do [ "$(n)" -gt 1 ] && { echo "a neighbour on the card"; exit 1; }; sleep 0.05; i=$((i+1)); done ;;
  oom) i=0; while [ $i -lt 12 ]; do [ "$(n)" -gt 2 ] && { echo "vkAllocateMemory: VK_ERROR_OUT_OF_DEVICE_MEMORY"; exit 0; }; sleep 0.05; i=$((i+1)); done ;;
  over) s=0; [ "$(n)" -gt 1 ] && s=1; sleep 1.2; [ "$(n)" -gt 1 ] && s=1; [ $s = 1 ] && sleep 0.1; exit 0 ;;
  build) echo "1 2 3 toy.o 0" >> "$NINJALOG"; sleep 0.2 ;;
  oom1) echo "vkAllocateMemory: VK_ERROR_OUT_OF_DEVICE_MEMORY"; sleep 0.2 ;;
  slow) sleep 0.15; [ "$(n)" -gt 1 ] && sleep 1.0; exit 0 ;;
  flaky) c="$VRAMDIR/../flaky.count"; k=$(cat "$c" 2>/dev/null || echo 0); echo $((k+1)) > "$c"; [ "$k" = 2 ] && exit 1; sleep 0.1 ;;
esac
exit 0
"""
LOCK = """#!/bin/sh
# the fake card lock: gpu-exclusive.sh's argument shape; records the label; LOCK75=1 = the card stayed held; LOCKWAIT=1 = the
# card is held by ANOTHER holder for 0.4 s first (3 rows' 'VRAM' in use while this call waits for the lock)
while [ $# -gt 0 ]; do case "$1" in --wait-max|--run-timeout) shift 2 ;; --label) echo "$2" >> "$LOCKLOG"; shift 2 ;; *) break ;; esac; done
[ "$LOCK75" = 1 ] && { echo "gpu-lock: the card stayed held"; exit 75; }
[ "$LOCKWAIT" = 1 ] && { for i in 1 2 3; do : > "$VRAMDIR/other.$i"; done; sleep 0.4; rm -f "$VRAMDIR"/other.*; }
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
TREE = os.path.join(T, "tree")                    # the toy tree whose check mark a run holds (URGENT #153)
subprocess.run(["git", "init", "-q", TREE], check=True)
MARK = os.path.join(TREE, ".git", "jah-check.pid")
REDS = os.path.join(T, "reds.md")                 # the toy spec's THE OPEN REDS table
with open(REDS, "w") as fh:
    fh.write("## Stage 1\n| row | x |\n|---|---|\n| toy.a | a stage table only names it |\n\n"
             "## THE OPEN REDS — the live list\n| row | cause | fixer | before |\n|---|---|---|---|\n"
             "| toy.declared | (1) 1.44 q | ITS-SPEC S0 | S1 |\n")
write(os.path.join(T, "lock.sh"), LOCK)
write(os.path.join(T, "smi.sh"), SMI)


def toy(name, extra, plain=4, more=()):
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
    lines += list(more)
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
    e = dict(os.environ, VRAMDIR=VR, LOCKLOG=log, MARKFILE=MARK, NINJALOG=os.path.join(b, ".ninja_log"),
             CLOBBER=os.path.join(b, "Testing", "Temporary"), **(env or {}))
    out = os.path.join(T, os.path.basename(b) + "-out")
    shutil.rmtree(out, ignore_errors=True)
    p = subprocess.run([sys.executable, "-I", WP, b, "--prices", os.path.join(b, "gate-times.txt"),
                        *([] if "--lock" in args else ["--lock", os.path.join(T, "lock.sh")]), "--nvidia-smi", os.path.join(T, "smi.sh"), "--out", out,
                        "--tree", TREE, *([] if "--open-reds" in args else ["--open-reds", REDS]), *args], env=e,
                       capture_output=True, text=True)
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

    # 1b. THE FIXTURE CLOSURE (the final CPU audit's finding 2): a row whose required fixture's setup is excluded (cold) is left
    #     out with it, and so is a row two fixtures away (its setup needs the cold one); a row needing a setup that takes no
    #     card (toy.a: toy.home) stays. The base selected all three: ctest would have run the cold setup in their hold.
    bx = toy("closure", [("toy.coldsetup", "plain", "JAHSHAKA_COLD_BY_SUBJECT TRUE FIXTURES_SETUP coldFix"),
                         ("toy.needscold", "plain", "FIXTURES_REQUIRED coldFix"),
                         ("toy.mid", "plain", "FIXTURES_SETUP nnFix FIXTURES_REQUIRED coldFix"),
                         ("toy.needsneeds", "plain", "FIXTURES_REQUIRED nnFix")])
    rc, out, labels, recs = run(bx, "--plan")
    ln = [x for x in out.splitlines() if x.startswith("  left out, its fixtures need an excluded row")]
    check(rc == 0 and "4 rows with no timing law" in out and len(ln) == 1 and ": 3: " in ln[0]
          and "toy.needscold (needs toy.coldsetup: cold)" in ln[0] and "toy.needsneeds (needs toy.coldsetup: cold)" in ln[0]
          and "toy.mid (needs toy.coldsetup: cold)" in ln[0] and "toy.a" not in ln[0],
          f"closure --plan: the rows whose fixtures need the cold setup left out by name, toy.a kept (rc {rc}) {ln}")

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
    # 9. THE INFLATION BAR: a row at x4 its width-1 wall beside a neighbour (0.4 -> 1.6 s: 1.2 s over it, 0.7 s clear of the 0.5-s
    #    floor - a width-1 run slowed by the toy's own spawns under --code -j12 stays inflated), under its class budget and the
    #    row cap (10 s) - width 2 refused by name -> width 1 (the base adopted width 2)
    bi = toy("inflate", [("toy.inflate", "inflate", "")])
    rc, out, labels, recs = run(bi, "--display", ":71", "--order", "1,2")
    ref = [ln for ln in out.splitlines() if "REFUSED" in ln]
    check(rc == 0 and "ADOPT WIDTH 1" in out and len(ref) == 1 and "toy.inflate ran" in ref[0] and "its width-1 wall" in ref[0]
          and "class budget" not in ref[0], f"inflate: a row inflated past x1.5 its width-1 wall refuses width 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 9b. THE WALLS' RESOLUTION: a 0.02-s row read at x3 beside a neighbour (0.07 s) is under the 0.5-s floor - not an
    #     inflation (the set's fixture setups run 0.00-0.02 s): width 2 adopted
    bt = toy("tiny", [("toy.tiny", "tiny", "")])
    rc, out, labels, recs = run(bt, "--display", ":71", "--order", "1,2")
    check(rc == 0 and "ADOPT WIDTH 2" in out and "REFUSED" not in out,
          f"tiny: a 0.02-s row's x3 under the 0.5-s floor refuses nothing, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 10. THE CLASS BUDGET (x0.1 for the toys: 1 s): a row ~0.76 s alone, ~1.16 s beside a neighbour (+0.4 s: under the
    #     inflation bar, max(x1.5, +0.5 s) = ~1.26 s) past its 1-s budget. THE ALONE WALL IS A SLEEP, NOT POLLS (TE8, 2026-11-04):
    #     it was 16 forking polls, 0.86 s solo, 0.14 s under the budget, and the code gate's -j12 load stretched the forks past
    #     it - read "over its class budget alone", left out, width 2 adopted: a false red. Now 4 polls (the neighbour's 0.2 s),
    #     then a sleep load cannot stretch: ~0.24 s under the budget alone, ~0.16 s over it at width 2, ~0.1 s under the bar
    bn = toy("near", [("toy.near", "near", "")])
    rc, out, labels, recs = run(bn, "--display", ":71", "--order", "1,2", "--budget-scale", "0.1")
    ref = [ln for ln in out.splitlines() if "REFUSED" in ln]
    check(rc == 0 and "ADOPT WIDTH 1" in out and len(ref) == 1 and "toy.near ran" in ref[0]
          and "past its class budget 1 s" in ref[0] and "its width-1 wall" not in ref[0],
          f"near: a row past its class budget at width 2 (under x1.5) refuses width 2 (rc {rc}) "
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

    # 12b. THE SAMPLER COUNTS FROM THE HOLD's START (the final CPU audit's LOW: the gate had no toy): another holder keeps 3 rows'
    #      'VRAM' while the call waits for the lock - width 1's peak is its own one row (1500 MB), never the other holder's 3500
    rc, out, labels, recs = run(bg, "--display", ":71", "--order", "1", env={"LOCKWAIT": "1"})
    check(rc == 0 and len(recs) == 1 and recs[0]["vram_peak_mb"] == 1500,
          f"held: the VRAM read while the lock is waited for is not this run's - width 1's peak "
          f"{recs[0]['vram_peak_mb'] if recs else None} MB (1500 = one toy row; 3500 = the other holder's) (rc {rc})")

    # 13. AN UNREAD TOTAL: nvidia-smi gives no memory.total - no headroom measured, width 2 refused -> width 1
    rc, out, labels, recs = run(bg, "--display", ":71", "--order", "1,2", env={"SMITOTAL": "none"})
    check(rc == 0 and "ADOPT WIDTH 1" in out and "the VRAM total unread" in out,
          f"vram unread: no memory.total refuses width 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

elif PART == "reds":
    # 4. A ROW RED ONLY BESIDE A NEIGHBOUR: width 2 refused by name -> width 1
    b2 = toy("conflict", [("toy.conflict", "conflict", "")])
    rc, out, labels, recs = run(b2, "--display", ":71", "--order", "1,2")
    check(rc == 0 and "ADOPT WIDTH 1" in out and "WIDTH 2 REFUSED: toy.conflict PASS at width 1, FAIL at width 2" in out,
          f"conflict: width 2 refused by the row that fails beside a neighbour, ADOPT WIDTH 1 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 14. A RED AGREES ONLY ON ITS FAIL LINES: red at width 1 on its law, red at width 2 on a device loss - width 2 refused
    #     with both lines (the base judged FAIL = FAIL and adopted width 2)
    br = toy("redline", [("toy.redline", "redline", "")])
    rc, out, labels, recs = run(br, "--display", ":71", "--order", "1,2")
    ref = [ln for ln in out.splitlines() if "REFUSED" in ln]
    check(rc == 0 and "ADOPT WIDTH 1" in out and len(ref) == 1
          and "toy.redline FAIL[FAIL: (1) 1.44 q over the 1-q bar] at width 1, FAIL[FAIL: vkQueueSubmit: VK_ERROR_DEVICE_LOST] "
              "at width 2"
          in ref[0], f"redline: a red whose FAIL line changes beside a neighbour refuses width 2 (rc {rc}) {ref}")

    # 15. ... AND THE SAME FAIL LINES AGREE: red at both widths on one law line -> width 2 adopted; the run held the tree's
    #     check mark (a row reads it live) and removed it after
    bs = toy("samered", [("toy.samered", "samered", ""), ("toy.mark", "mark", "")])
    rc, out, labels, recs = run(bs, "--display", ":71", "--order", "1,2")
    check(rc == 0 and "ADOPT WIDTH 2" in out and "REFUSED" not in out,
          f"samered: a red with equal FAIL lines at both widths agrees, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")
    check(all(r["rows"].get("toy.mark", [None])[0] == "PASS" for r in recs) and not os.path.exists(MARK),
          f"samered: every run held the tree's check mark (toy.mark read it live: "
          f"{[r['rows'].get('toy.mark', [None])[0] for r in recs]}), removed after")

elif PART == "log":
    # 16. THE DECLARED OPEN REDS LEAVE THE SET, each named with its table line - only the live table's rows (a stage table
    #     naming toy.a declares nothing)
    bd = toy("declared", [("toy.declared", "plain", "")])
    rc, out, labels, recs = run(bd, "--plan")
    ln = [x for x in out.splitlines() if x.startswith("  left out, a declared open red")]
    check(rc == 0 and len(ln) == 1 and "toy.declared (:9)" in ln[0] and "toy.a" not in ln[0]
          and "4 rows with no timing law" in out, f"declared: the open red left out with its line, toy.a kept (rc {rc}) {ln}")
    # 16b. WHAT AN ADOPTED WIDTH COVERS (the final CPU audit's finding 3): only the rows it measured - the declared red keeps the
    #      whole card, alone (alone-rows.txt with its reason), never the width (the base wrote no list: a width applied to every row)
    rc, out, labels, recs = run(bd, "--display", ":71", "--order", "1,2")
    od = os.path.join(T, "declared-out")
    cov = open(os.path.join(od, "adopted-rows.txt")).read().split() if os.path.exists(os.path.join(od, "adopted-rows.txt")) else None
    alone = dict(ln.split("\t", 1) for ln in open(os.path.join(od, "alone-rows.txt")).read().splitlines()) \
        if os.path.exists(os.path.join(od, "alone-rows.txt")) else {}
    check(rc == 0 and "ADOPT WIDTH 2" in out and cov == ["toy.a", "toy.b", "toy.c", "toy.d"]
          and alone.get("toy.declared") == "a declared open red" and alone.get("toy.cold") == "cold"
          and "keep the whole card, 8 x W slots, alone" in out,
          f"declared: width 2 covers only the rows it measured, the declared red stays alone with its reason (rc {rc}) "
          f"covered {cov}, alone {sorted(alone)}")
    rc, out, labels, recs = run(bd, "--plan", "--open-reds", os.path.join(T, "no-such-spec.md"))
    check(rc == 2 and "unreadable" in out, f"declared: an unreadable open-reds spec refused (rc {rc}), not read as empty")

    # 17. THE RUN's OWN LOG: the out-of-memory line is read from <run>/ctest.log even when another ctest in the build
    #     removes Testing/Temporary under the run (the base read the shared LastTest.log, lost it and adopted width 3)
    bc = toy("clobber", [("toy.oomclobber", "oomclobber", "")])
    rc, out, labels, recs = run(bc, "--display", ":71", "--order", "1,2,3")
    check(rc == 0 and "ADOPT WIDTH 2" in out and "WIDTH 3 REFUSED:" in out and "out-of-memory" in out
          and all(r.get("log", "").endswith("ctest.log") and os.path.exists(r["log"]) for r in recs),
          f"clobber: the OOM line read from the run's own ctest.log, width 3 refused, ADOPT WIDTH 2 (rc {rc}) "
          f"{[ln for ln in out.splitlines() if 'REFUSED' in ln or 'ADOPT' in ln]}")

    # 17b. A BUILD DURING THE RUN (the final CPU audit's finding 4): a row "builds" the toy tree (appends to its .ninja_log) in
    #      the width-1 hold - the run is VOID before the width-2 hold, exit 1, nothing judged (the base ran on and adopted)
    bb = toy("rebuilt", [("toy.build", "build", "")])
    rc, out, labels, recs = run(bb, "--display", ":71", "--order", "1,2")
    check(rc == 1 and labels == ["width-w1-p1"] and "VOID before width 2 part 1" in out and ".ninja_log moved" in out
          and "ADOPT" not in out, f"rebuilt: a build mid-run voids it before the next hold, exit 1 (rc {rc}, holds {labels})")

    # 18. ANOTHER CHECK's LIVE MARK in the tree refuses the run before any hold (this test's own pid: alive)
    with open(MARK, "w") as fh:
        fh.write(f"{os.getpid()} 2026-11-03T00:00:00\n")
    rc, out, labels, recs = run(b1, "--display", ":71", "--order", "1,2")
    check(rc == 2 and "another check is running in this tree" in out and not labels and not recs,
          f"mark: a live mark of another check refuses the run, no hold (rc {rc})")
    os.remove(MARK)

elif PART == "budget":
    # 19. A ROW ALREADY PAST ITS CLASS BUDGET AT WIDTH 1 (the final CPU audit's finding 1; x0.1 for the toys: 1 s): toy.over runs
    #     1.2 s alone, 1.3 s beside a neighbour (x1.08: no inflation) - a stale price or a budget red, named at width 1 and left
    #     out of the judgement, so width 2 is judged on the other rows and adopted (the base judged the budget at width 2+ only
    #     and refused width 2 with the wrong cause: "past its class budget 1 s at width 2")
    bo = toy("over", [("toy.over", "over", "")])
    with open(os.path.join(bo, "gate-times.txt"), "a") as fh:
        fh.write("toy.over 0.8  toy (over 1 s / 1.5)\n")
    rc, out, labels, recs = run(bo, "--plan", "--budget-scale", "0.1")
    ln = [x for x in out.splitlines() if "priced over their class budget / 1.5" in x]
    check(rc == 0 and len(ln) == 1 and "toy.over 0.80/1" in ln[0] and "toy.a" not in ln[0],
          f"over --plan: the row priced over its budget / 1.5 named (the budget binds first), the 0.3-s rows not (rc {rc}) {ln}")
    rc, out, labels, recs = run(bo, "--display", ":71", "--order", "1,2", "--budget-scale", "0.1")
    o1 = [x for x in out.splitlines() if "OVER ITS CLASS BUDGET AT WIDTH 1" in x]
    check(rc == 0 and "ADOPT WIDTH 2" in out and "REFUSED" not in out and len(o1) == 1 and "toy.over ran 1.2" in o1[0]
          and "past its 1 s" in o1[0], f"over: the row past its budget alone named at width 1, not charged to width 2, ADOPT "
          f"WIDTH 2 (rc {rc}) {[x for x in out.splitlines() if 'REFUSED' in x or 'ADOPT' in x or 'OVER ITS' in x]}")
    od = os.path.join(T, "over-out")
    cov = open(os.path.join(od, "adopted-rows.txt")).read().split() if os.path.exists(os.path.join(od, "adopted-rows.txt")) else []
    alone = open(os.path.join(od, "alone-rows.txt")).read() if os.path.exists(os.path.join(od, "alone-rows.txt")) else ""
    check("toy.a" in cov and "toy.over" not in cov and "toy.over\tover its class budget at width 1" in alone,
          f"over: the adopted width does not cover the row over its budget alone (covered {cov})")

    # 20. THE SCALE IS A TOY's KNOB: above 1 (it loosens every budget) refused; anything but 1 with the card's own lock refused
    for sc, extra, what in (("2", [], "loosens every class budget"),
                            ("0.5", ["--lock", os.path.join(SRC, "scripts", "gpu-exclusive.sh")], "on the card")):
        rc, out, labels, recs = run(bo, "--plan", "--budget-scale", sc, *extra)
        check(rc == 2 and what in out and not labels, f"scale {sc}{' on the card' if extra else ''}: refused (rc {rc})")

    # 21. A ROW's OWN OUT-OF-MEMORY LINE ALONE AT WIDTH 1: no width adopted, exit 1, the fault named (the base judged OOM at
    #     width 2+ only: width 2 refused and width 1 adopted with the fault unnamed)
    bm = toy("oom1", [("toy.oom1", "oom1", "")])
    rc, out, labels, recs = run(bm, "--display", ":71", "--order", "1,2")
    check(rc == 1 and "WIDTH 1 HAS 1 OUT-OF-MEMORY / Xid LINE(S) WITH ONE ROW ON THE CARD" in out and "ADOPT" not in out.replace(
          "NO WIDTH ADOPTED", ""), f"oom1: an OOM line alone at width 1 adopts no width (rc {rc}) "
          f"{[x for x in out.splitlines() if 'WIDTH' in x][:3]}")

elif PART == "perrow":
    # THE PER-ROW WIDTH (block C's ruling, 2026-11-03: no width for the whole set - 7 rows past a bar, lab.monitor unstable
    # alone): each row adopts width 2 only on its OWN bars in every width-2 run, the run's VRAM under 90 %; the rest alone,
    # each with its reason. Synthetic records through write_per_row (RED AT THE BASE: the base has no per-row judgement).
    import importlib.util
    spm = importlib.util.spec_from_file_location("width_pair_under_test", WP)
    wp = importlib.util.module_from_spec(spm)
    spm.loader.exec_module(wp)

    def rec(w, k, rows, peak=5000.0, total=16000.0, oom=(), xid=()):
        return {"tag": f"w{w}-p{k}", "width": w, "part": k, "held": True, "rows": rows, "asked": sorted(rows),
                "vram_peak_mb": peak, "vram_total_mb": total, "oom": list(oom), "xid": list(xid)}
    P = "PASS"
    w1a = {"ok": (P, 2.0), "slow": (P, 2.0), "budget": (P, 8.0), "red": ("FAIL[FAIL: (1) a law]", 3.0), "flip": (P, 2.0),
           "alone_over": (P, 12.0), "unstable": (P, 2.0), "vram": (P, 2.0), "excluded": (P, 2.0)}
    w1b = dict(w1a, unstable=("FAIL[FAIL: (4) a gap]", 2.1))
    w2 = {"ok": (P, 2.9), "slow": (P, 3.4), "budget": (P, 10.4), "red": ("FAIL[FAIL: (1) a law]", 3.1),
          "flip": ("FAIL[FAIL: a neighbour]", 2.0), "alone_over": (P, 13.0), "unstable": (P, 2.0), "excluded": (P, 2.0)}
    recs = [rec(1, 1, w1a), rec(2, 1, w2), rec(2, 2, {"vram": (P, 2.0)}, peak=14500.0), rec(2, 1, dict(w2, slow=(P, 2.2))),
            rec(1, 1, w1b)]
    budget = {n: 10.0 for n in w1a}
    out = os.path.join(T, "pr"); os.makedirs(out)
    try:
        ad, al = wp.write_per_row(recs, budget, {"a timing law": ["excluded"]}, out, 2)
        got = open(os.path.join(out, "adopted-rows.txt")).read().split()
        why = dict(ln.split("\t", 1) for ln in open(os.path.join(out, "alone-rows.txt")).read().splitlines())
    except AttributeError as e:
        got, why = None, {}
        check(False, f"perrow: the tool has no per-row judgement ({e})")
    if got is not None:
        check(got == ["ok", "red"], f"perrow: the rows that held every bar in every width-2 run adopt it - a green row and a "
              f"red row on equal FAIL lines: {got}")
        check("x1.70" in why.get("slow", ""), f"perrow: a row past x1.5 in ONE of two width-2 runs keeps the card: {why.get('slow')}")
        check("past its class budget" in why.get("budget", ""), f"perrow: a row past its class budget at width 2: {why.get('budget')}")
        check("its verdict at width 2" in why.get("flip", ""), f"perrow: a verdict that changes at width 2: {why.get('flip')}")
        check("over its class budget alone" in why.get("alone_over", ""), f"perrow: a row over budget alone: {why.get('alone_over')}")
        check("disagrees with itself" in why.get("unstable", ""), f"perrow: width 1 disagreeing with itself: {why.get('unstable')}")
        check("VRAM peak 14500" in why.get("vram", ""), f"perrow: a width-2 run past 90 % of the card: {why.get('vram')}")
        check(why.get("excluded") == "a timing law", f"perrow: a row the selection leaves out never adopts: {why.get('excluded')}")

else:
    check(False, f"unknown part {PART!r} (plan|judge|refuse|bars|gain|reds|log|budget|perrow)")
shutil.rmtree(T, ignore_errors=True)
print("width_pair_test: " + ("PASSED" if not fails else f"FAILED ({len(fails)})"))
sys.exit(1 if fails else 0)
