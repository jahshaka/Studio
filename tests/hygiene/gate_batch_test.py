#!/usr/bin/env python3
"""gate.batch — ONE GATE PER BATCH OF LANES (lane BATCH-GATE-1; docs/TESTING_GATE.md §3c; the owner's
question 3 of 2026-10-09, spikes/preflight-testing-1). Every case drives the REAL code — the lead's
`scripts/lead/merge-dbuild-lane.sh batch` / `batch-land` (JAH_MERGE_SCRIPT, else <workspace>/scripts/lead/),
its `fork-pin-check.sh`, this tree's `check-trailers.sh`, `gate-scope.py --attribute` and `gate_runlog.py`
— against a TOY REPO PAIR built here (a Studio repo with an `irisgl` gitlink, an irisgl repo with a
`thirdparty/ogre-next` gitlink, a toy fork with diverging lines, the d-build and lane worktrees laid out as
the lead's are), a PRIVATE run log, token directory and lead scratch; the judge is a stub that logs how it
was called (the real judge is gate.ci_check's and gate.fix_round's subject, and its diff is empty):

  1. THREE LANES -> ONE CANDIDATE: on branches batch-<tag> (d-build untouched), each lane a merge onto the
     running candidate; their pins r0 (d-build's) / f1 / f2 are ancestors of one pin -> accepted, the
     candidate pins P = f2 and the gate printed is the fork tier; SCRIPTING.md taken from the later lane;
     batch-land: ONE judge run, on d-build..candidate with the candidate's build; green -> d-build
     fast-forwarded to the candidate in BOTH repos (and the main tree's irisgl d-build ref);
  2. A CONFLICTING PAIR -> the whole batch refused, the lane and the file named, nothing moved;
  3. DIVERGING FORK PINS -> refused (the fork freeze), nothing moved;
  4. A RED, ATTRIBUTED: batch-land on a red judge moves nothing and prints the attribution command with
     every lane; run, it puts a row red 3/3 on the lane whose own tip makes it (0/3 on the other) and names
     a row red on NO lane's tip the COMBINATION; a worktree off its batch tip is refused;
  5. THE RECORDS' `lanes` IS A LIST (a batch's lanes; one lane = a one-element list; a legacy `lane`
     string still reads);
  6. `--joint` IS GONE: the old command refuses and names the batch command.

Run: gate_batch_test.py <source-dir> <build-dir>
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: FAILURES.append(what)


STUB_JUDGE = r'''#!/usr/bin/env python3
# the toy's judge: logs its arguments; green, or red on the rows named in TOY_JUDGE_MODE's file
import os, sys
open(os.environ["TOY_JUDGE_LOG"], "a").write(" ".join(sys.argv[1:]) + "\n")
try: mode = open(os.environ["TOY_JUDGE_MODE"]).read().strip()
except OSError: mode = "green"
if mode == "green":
    print("ci-gate-check: the scoped selection: 2 row(s) green — 2 at the tip"); sys.exit(0)
for r in mode.split(","):
    print(f"ci-gate-check: row {r} <- 0123456ab (the tip): red — FAIL and not in the contention class — needs a recorded verdict")
for r in mode.split(","):
    print(f"ci-gate-check: REFUSED — RED {r}: FAIL and not in the contention class — needs a recorded verdict")
sys.exit(1)
'''

TOY_CTEST = r'''
cmake_minimum_required(VERSION 3.20)
project(toylane NONE)
enable_testing()
add_test(NAME row.x COMMAND sh -c "if [ -e '@WT@/BREAKS_X' ]; then echo 'FAIL: BREAKS_X is in this tree'; exit 1; fi; echo ok")
add_test(NAME row.combo COMMAND sh -c "if [ -e '@WT@/BREAKS_X' ] && [ -e '@WT@/BREAKS_Y' ]; then echo 'FAIL: X and Y together'; exit 1; fi; echo ok")
'''


def main(source, build):
    scripts = os.path.join(source, "scripts")
    sys.path.insert(0, scripts)
    import gate_runlog as rl
    for k in ("JAH_GATE_SLOT_HELD", "JAH_VRAM_HELD", "JAH_VRAM_ALL", "DISPLAY", "JAH_JUDGE_READ", "OGRE_PREFIX"):
        os.environ.pop(k, None)
    merge = os.environ.get("JAH_MERGE_SCRIPT") or os.path.join(rl.workspace_root(), "scripts", "lead",
                                                               "merge-dbuild-lane.sh")
    have = os.path.isfile(merge) and "batch-land" in open(merge).read()
    check(have, f"the lead's merge script carries `batch` / `batch-land` ({merge}; the lane's patch: "
                f"spikes/batch-gate-1/lead-scripts.patch)")
    if not have: return 1
    scratch = tempfile.mkdtemp(prefix="gate-batch-test-")
    try:
        return run(source, scripts, merge, scratch, rl)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run(source, scripts, merge, scratch, rl):
    gcfg = os.path.join(scratch, "gitconfig")
    open(gcfg, "w").write("[user]\n\tname = jahshaka\n\temail = jahshaka@gmail.com\n[init]\n\tdefaultBranch = main\n"
                          "[protocol \"file\"]\n\tallow = always\n[uploadpack]\n\tallowAnySHA1InWant = true\n"
                          "[advice]\n\tdetachedHead = false\n")
    genv = dict(os.environ, GIT_CONFIG_GLOBAL=gcfg, GIT_CONFIG_NOSYSTEM="1", GIT_AUTHOR_NAME="jahshaka",
                GIT_AUTHOR_EMAIL="jahshaka@gmail.com", GIT_COMMITTER_NAME="jahshaka",
                GIT_COMMITTER_EMAIL="jahshaka@gmail.com")

    def git(cwd, *args):
        r = subprocess.run(["git", "-C", cwd] + list(args), capture_output=True, text=True, env=genv)
        if r.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)} in {cwd}: {r.stderr.strip()}")
        return r.stdout.strip()

    def write(path, text):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w").write(text)

    def commit(cwd, files, msg, links=None):
        for f, t in (files or {}).items():
            write(os.path.join(cwd, f), t)
            git(cwd, "add", f)
        for path, sha in (links or {}).items():
            git(cwd, "update-index", "--add", "--cacheinfo", f"160000,{sha},{path}")
        git(cwd, "commit", "-q", "-m", msg)
        return git(cwd, "rev-parse", "HEAD")

    # ---- the toy fork: r0 (the stack tag) -> f1 -> f2 on `jahshaka`; fx from r0 on `side` -------------------
    fork = os.path.join(scratch, "fork")
    os.makedirs(fork); git(fork, "init", "-q", "-b", "jahshaka")
    r0 = commit(fork, {"OgreMain.txt": "r0\n"}, "r0"); git(fork, "tag", "jahshaka-stack-v1")
    f1 = commit(fork, {"OgreMain.txt": "f1\n"}, "f1")
    f2 = commit(fork, {"OgreMain.txt": "f2\n"}, "f2")
    git(fork, "checkout", "-q", "-b", "side", r0)
    fx = commit(fork, {"OgreMain.txt": "fx\n"}, "fx")
    git(fork, "checkout", "-q", "jahshaka")
    changes = os.path.join(scratch, "OGRE_NEXT_CHANGES.md")
    open(changes, "w").write("".join(f"- {c[:9]} toy\n" for c in (f1, f2, fx)))

    # ---- the main tree: Studio (branch ogre) + its irisgl (o3de) + the fork clone -----------------------------
    M = os.path.join(scratch, "jahshaka")
    ig = os.path.join(M, "irisgl")
    os.makedirs(ig); git(ig, "init", "-q", "-b", "o3de")
    ibase = commit(ig, {"engine.txt": "one\ntwo\nthree\n"}, "irisgl base", {"thirdparty/ogre-next": r0})
    subprocess.run(["git", "clone", "-q", fork, os.path.join(ig, "thirdparty", "ogre-next")], env=genv, check=True)
    git(os.path.join(ig, "thirdparty", "ogre-next"), "checkout", "-q", r0)
    git(M, "init", "-q", "-b", "ogre")
    write(os.path.join(M, ".gitignore"), "/.claude/\nbuild-linux/\n")
    os.makedirs(os.path.join(M, "scripts"), exist_ok=True)
    shutil.copy(os.path.join(scripts, "check-trailers.sh"), os.path.join(M, "scripts", "check-trailers.sh"))
    write(os.path.join(M, "scripts", "ci_gate_check.py"), STUB_JUDGE)
    git(M, "add", ".gitignore", "scripts")
    sbase = commit(M, {"src/a.txt": "a1\na2\na3\n", "src/b.txt": "b\n", "docs/SCRIPTING.md": "verbs v0\n"},
                   "studio base", {"irisgl": ibase})
    # d-build, as the lead's: a worktree on d-build, its irisgl a clone on d-build
    D = os.path.join(M, ".claude", "worktrees", "d-build")
    git(M, "worktree", "add", "-q", "-b", "d-build", D, sbase)

    def clone_irisgl(wt, branch, at):
        p = os.path.join(wt, "irisgl")
        if os.path.isdir(p): shutil.rmtree(p)
        subprocess.run(["git", "clone", "-q", ig, p], env=genv, check=True)
        git(p, "fetch", "-q", ig, "+refs/heads/*:refs/remotes/main/*")
        git(p, "checkout", "-q", "-b", branch, at)
        return p
    clone_irisgl(D, "d-build", ibase)

    def lane(name, studio=None, irisgl=None, pin=None, wt_files=None):
        """A lane worktree off d-build's tip with its own irisgl clone; returns the batch spec."""
        base = git(D, "rev-parse", "HEAD"); ib = git(os.path.join(D, "irisgl"), "rev-parse", "HEAD")
        W = os.path.join(M, ".claude", "worktrees", name)
        git(M, "worktree", "add", "-q", "-b", name, W, base)
        wi = clone_irisgl(W, f"{name}-irisgl", ib)
        it = ib
        if irisgl or pin:
            it = commit(wi, irisgl, f"{name}: irisgl", {"thirdparty/ogre-next": pin} if pin else None)
        st = commit(W, dict(studio or {}, **(wt_files or {})), f"{name}: studio", {"irisgl": it})
        return W, st, it, f"{name}:{st}:{it}"

    lead = os.path.join(scratch, "lead")
    judge_log, judge_mode = os.path.join(scratch, "judge.log"), os.path.join(scratch, "judge.mode")
    menv = dict(genv, JAH_FPC_MAIN=M, JAH_LEAD_SCRATCH=lead, JAH_FPC_FORK_CLONE=os.path.join(ig, "thirdparty", "ogre-next"),
                JAH_FPC_FORK_REMOTE=fork, JAH_FPC_CHANGES=changes, TOY_JUDGE_LOG=judge_log, TOY_JUDGE_MODE=judge_mode)
    rcb = os.path.join(scratch, "rc-build"); os.makedirs(rcb)

    def mscript(*args):
        p = subprocess.run(["bash", merge] + list(args), capture_output=True, text=True, env=menv)
        return p.returncode, p.stdout + p.stderr

    def refs(name):
        return (subprocess.run(["git", "-C", M, "rev-parse", "-q", "--verify", f"refs/heads/{name}"],
                               capture_output=True, text=True, env=genv).stdout.strip(),
                subprocess.run(["git", "-C", os.path.join(D, "irisgl"), "rev-parse", "-q", "--verify",
                                f"refs/heads/{name}"], capture_output=True, text=True, env=genv).stdout.strip())

    def dbuild():
        return git(D, "rev-parse", "HEAD"), git(os.path.join(D, "irisgl"), "rev-parse", "HEAD")

    # ---- 1. three lanes -> one candidate; one judge; both repos fast-forwarded ------------------------------
    print("1. three lanes -> one candidate (pins r0 / f1 / f2: ancestors of P = f2)")
    Wa, sta, ita, spa = lane("lane-a", {"src/a.txt": "a1\nA2\na3\n"}, {"engine.txt": "one\nTWO\nthree\n"})
    Wb, stb, itb, spb = lane("lane-b", {"src/b.txt": "B\n", "docs/SCRIPTING.md": "verbs vb\n"}, None, pin=f1)
    Wc, stc, itc, spc = lane("lane-c", {"src/c.txt": "c\n", "docs/SCRIPTING.md": "verbs vc\n"},
                             {"render.txt": "r\n"}, pin=f2)
    s0, i0 = dbuild()
    rc, out = mscript("batch", "t1", spa, spb, spc)
    print("\n".join("     | " + l for l in out.splitlines()[-12:]))
    sc, ic = refs("batch-t1")
    check(rc == 0 and sc and ic, "the batch is accepted and batch-t1 exists in both repos (exit %d)" % rc)
    check(dbuild() == (s0, i0), "...and d-build did NOT move (the candidate is on its own branches)")
    for l in ("lane-a", "lane-b", "lane-c"):
        check(f"fork pins: {l} " in out, f"...every lane's fork pin is printed ({l})")
    check(f"fork pin P = {f2[:9]}" in out and "MOVED" in out, "...P = f2 (the descendant of every pin), said as a move")
    if sc and ic:
        check(git(os.path.join(D, "irisgl"), "rev-parse", f"{ic}:thirdparty/ogre-next") == f2,
              "...the irisgl candidate pins P")
        check(git(D, "rev-parse", f"{sc}:irisgl") == ic, "...the Studio candidate pins the irisgl candidate")
        chain = git(D, "rev-list", "--first-parent", f"{sbase}..{sc}").split()
        seconds = [git(D, "rev-parse", f"{c}^2") for c in chain]
        check(len(chain) == 3 and seconds == [stc, stb, sta],
              "...one merge per lane on the first-parent line, in the lead's order, each lane's tip its second parent")
        check(git(D, "show", f"{sc}:docs/SCRIPTING.md") == "verbs vc", "...SCRIPTING.md is the later lane's")
        check(git(D, "show", f"{sc}:src/a.txt") == "a1\nA2\na3" and git(D, "show", f"{sc}:src/c.txt") == "c",
              "...the candidate holds every lane's change")
        check(git(os.path.join(D, "irisgl"), "show", f"{ic}:engine.txt") == "one\nTWO\nthree",
              "...and every lane's irisgl change")
        check("JAH_GATE_TIER=fork" in out and f"JAH_GATE_RANGE={sbase}..{sc}" in out
              and "JAH_GATE_LANES=lane-a,lane-b,lane-c" in out and f"rc-gate.sh batch-t1 {sc}" in out,
              "...the ONE gate command: rc-gate.sh on the candidate, the range d-build..candidate, the fork tier, "
              "the lanes")
    open(judge_mode, "w").write("green")
    rc, out = mscript("batch-land", "t1", "--build", rcb)
    print("\n".join("     | " + l for l in out.splitlines()[-6:]))
    calls = open(judge_log).read().splitlines() if os.path.exists(judge_log) else []
    check(calls == [f"{sbase}..{sc} --build {rcb}"], "batch-land: ONE judge run, on d-build..candidate, with the "
          "candidate's build (%s)" % calls)
    check(rc == 0 and dbuild() == (sc, ic), "...green -> d-build fast-forwarded to the candidate in both repos (exit %d)" % rc)
    check(git(ig, "rev-parse", "refs/heads/d-build") == ic, "...and the main tree's irisgl d-build ref follows")
    check("HASHES" in out and "READY TO PUSH" in out, "...the HASHES line and the push line are printed")

    # ---- 2. a conflicting pair -> refused whole, the lane and the file named, nothing moved --------------
    print("2. a conflicting pair")
    s1, i1 = dbuild()
    _, _, _, spd = lane("lane-d", {"src/a.txt": "a1\nD2\na3\n"})
    _, _, _, spe = lane("lane-e", {"src/a.txt": "a1\nE2\na3\n", "src/e.txt": "e\n"})
    rc, out = mscript("batch", "t2", spd, spe)
    check(rc != 0 and "lane-e CONFLICTS" in out and "src/a.txt" in out,
          "refused, naming the lane and the file (exit %d: %s)" % (rc, [l for l in out.splitlines() if "REFUSED" in l]))
    check(refs("batch-t2") == ("", "") and dbuild() == (s1, i1), "...nothing moved (no batch-t2, d-build unchanged)")
    tmp = git(os.path.join(D, "irisgl"), "for-each-ref", "refs/batch-tmp/")
    check(tmp == "", "...and its temporary refs are gone (%s)" % tmp[:80])

    # ---- 3. diverging fork pins -> refused (the fork freeze) ------------------------------------------------
    print("3. diverging fork pins")
    _, _, _, spf = lane("lane-f", {"src/f.txt": "f\n"}, None, pin=fx)
    _, _, _, spg = lane("lane-g", {"src/g.txt": "g\n"})
    rc, out = mscript("batch", "t3", spf, spg)
    check(rc != 0 and "the pins diverge" in out and fx[:9] in out,
          "two lanes on diverging fork lines (fx vs d-build's f2) are refused, the pins named (exit %d)" % rc)
    check(refs("batch-t3") == ("", "") and dbuild() == (s1, i1), "...nothing moved")

    # ---- 4. a red, attributed -----------------------------------------------------------------------------
    print("4. a red row, attributed to the lane whose own tip makes it")
    pin_now = git(os.path.join(D, "irisgl"), "rev-parse", "HEAD:thirdparty/ogre-next")
    install = os.path.join(scratch, "install"); os.makedirs(install)
    open(os.path.join(install, "BUILT_FROM"), "w").write(pin_now + "\n")
    lanes4 = {}
    for name, flag in (("lane-h", "BREAKS_X"), ("lane-i", "BREAKS_Y")):
        W, st, it, sp = lane(name, {flag: "1\n", f"src/{name}.txt": "x\n"})
        subprocess.run(["git", "clone", "-q", fork, os.path.join(W, "irisgl", "thirdparty", "ogre-next")],
                       env=genv, check=True)
        git(os.path.join(W, "irisgl", "thirdparty", "ogre-next"), "checkout", "-q", pin_now)
        tsrc, tb = os.path.join(scratch, f"toy-{name}"), os.path.join(W, "build-linux")
        write(os.path.join(tsrc, "CMakeLists.txt"), TOY_CTEST.replace("@WT@", W))
        r = subprocess.run(["cmake", "-S", tsrc, "-B", tb], capture_output=True, text=True)
        check(r.returncode == 0, f"the toy ctest project of {name} configures")
        lanes4[name] = (W, st, sp)
    rc, out = mscript("batch", "t4", lanes4["lane-h"][2], lanes4["lane-i"][2])
    sc4, ic4 = refs("batch-t4")
    check(rc == 0 and sc4 and "JAH_GATE_TIER=scoped" in out and f"fork pin P = {pin_now[:9]}" in out,
          "equal pins -> accepted, P = d-build's pin, the scoped gate (exit %d)" % rc)
    open(judge_mode, "w").write("row.x,row.combo")
    s4 = dbuild()
    rc, out = mscript("batch-land", "t4", "--build", rcb)
    check(rc == 6 and dbuild() == s4 and refs("batch-t4") == (sc4, ic4),
          "batch-land on a red judge: refused, nothing moved (exit %d)" % rc)
    acmd = [l for l in out.splitlines() if "--attribute" in l]
    check(len(acmd) == 1 and "row.combo,row.x" in acmd[0] and "--batch t4" in acmd[0]
          and all(f"{n}:{lanes4[n][0]}:{lanes4[n][1]}" in acmd[0] for n in lanes4),
          "...it prints THE attribution command: the red rows, the tag, every lane's worktree and tip (no DISPLAY)")
    runs = os.path.join(scratch, "runs")
    aenv = dict(os.environ, JAH_RUN_LOG_DIR=runs, JAH_VRAM_DIR=os.path.join(scratch, "vram"), JAH_VRAM_TOKENS="3",
                JAH_VRAM_WAIT="5", OGRE_PREFIX=install)
    args = acmd[0].split("gate-scope.py", 1)[1].split() if acmd else []
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + args, capture_output=True,
                       text=True, env=aenv)
    aout = p.stdout + p.stderr
    print("\n".join("     | " + l for l in aout.splitlines() if l.startswith(("row", "=>", "==="))))
    check(p.returncode == 0, "the attribution runs (exit %d)" % p.returncode)
    check("row.x | lane-h | 3/3 red | FAIL: BREAKS_X is in this tree" in aout and "row.x | lane-i | 0/3 red | -" in aout,
          "row.x: 3/3 red on lane-h's own tip, 0/3 on lane-i's, the first failing check named")
    check("=> row.x: lane-h (3/3 red on its own tip)" in aout, "...attributed to lane-h")
    check("=> row.combo: COMBINATION" in aout and "row.combo | lane-h | 0/3 red" in aout
          and "row.combo | lane-i | 0/3 red" in aout, "row.combo: red on no lane's own tip -> the COMBINATION")
    recs = []
    for f in sorted(os.listdir(runs)) if os.path.isdir(runs) else []:
        recs += [json.loads(l) for l in open(os.path.join(runs, f))]
    check(len(recs) == 12 and all(r.get("reason") == "attribute:t4" and r.get("retry") is True for r in recs),
          "12 records (2 rows x 2 lanes x 3), each `reason: attribute:t4`, a retry (%d)" % len(recs))
    check(all(r["lanes"] == [n] and r["tip"]["studio"] == lanes4[n][1] for n in lanes4 for r in recs
              if r["tip"]["studio"] == lanes4[n][1]) and {r["tip"]["studio"] for r in recs} == {lanes4[n][1] for n in lanes4},
          "...each at ITS lane's own tip, `lanes` = [that lane]")
    check("lane" not in (recs[0] if recs else {"lane": 1}), "...and no single-string `lane` field is written")
    git(lanes4["lane-i"][0], "commit", "-q", "--allow-empty", "-m", "lane-i moved on")
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + args, capture_output=True,
                       text=True, env=aenv)
    check(p.returncode == 4 and "not the batch's tip" in p.stderr,
          "a lane worktree moved past its batch tip is refused (the exact-tip rule; exit %d)" % p.returncode)

    # ---- 5. the records' `lanes` is a list ------------------------------------------------------------------
    print("5. the run log's `lanes`")
    check(rl.lane_list("p1c-light-list,vr-reorder-1") == ["p1c-light-list", "vr-reorder-1"]
          and rl.lane_list(["a", "b,c", "a"]) == ["a", "b", "c"] and rl.lane_list("solo") == ["solo"],
          "lane_list: a comma list, repeats and one name all give the list")
    check(rl.record_lanes({"lanes": ["x", "y"]}) == ["x", "y"] and rl.record_lanes({"lane": "old"}) == ["old"],
          "record_lanes reads the list, and the one string the older records carry")
    os.environ["JAH_RUN_LOG_DIR"] = os.path.join(scratch, "runs5")
    import io, contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rl.run_ctest("ctest -j1 --timeout 60 --no-tests=error -R '^row[.]combo$'", os.path.join(lanes4["lane-h"][0],
                     "build-linux"), "scoped", "p1c-light-list,vr-reorder-1", 1, root=lanes4["lane-h"][0])
    r5 = []
    for f in sorted(os.listdir(os.environ["JAH_RUN_LOG_DIR"])):
        r5 += [json.loads(l) for l in open(os.path.join(os.environ["JAH_RUN_LOG_DIR"], f))]
    check(len(r5) == 1 and r5[0].get("lanes") == ["p1c-light-list", "vr-reorder-1"]
          and r5[0]["tip"]["studio"] == lanes4["lane-h"][1],
          "a batch gate's record: `lanes` = the batch's list, the tip = the tree it ran in (%s)"
          % (str((r5[0].get("lanes"), r5[0]["tip"]["studio"][:9])) if r5 else "no record"))
    check("joint" not in rl.TIERS, "the run log's tiers no longer list `joint`")

    # ---- 6. --joint is gone ---------------------------------------------------------------------------------
    print("6. --joint")
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py"), "--joint", "a..b", "c..d"],
                       capture_output=True, text=True)
    check(p.returncode == 2 and "retired" in p.stderr and "merge-dbuild-lane.sh batch" in p.stderr,
          "`gate-scope.sh --joint` refuses and names the batch command (exit %d)" % p.returncode)
    src = open(os.path.join(scripts, "gate-scope.py")).read()
    check("def joint(" not in src and '"--joint", nargs' not in src, "...its code is deleted")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit("usage: gate_batch_test.py <source-dir> <build-dir>")
    try:
        main(sys.argv[1], sys.argv[2])
    except Exception as e:                       # a broken fixture is a red, said plainly
        import traceback; traceback.print_exc()
        FAILURES.append(f"{type(e).__name__}: {e}")
    print(f"\ngate.batch: {'PASS' if not FAILURES else 'FAIL (%d)' % len(FAILURES)}")
    sys.exit(1 if FAILURES else 0)
