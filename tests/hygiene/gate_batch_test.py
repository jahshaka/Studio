#!/usr/bin/env python3
"""gate.batch — ONE GATE PER BATCH OF LANES (lane BATCH-GATE-1; docs/TESTING_GATE.md §3c; the owner's
question 3 of 2026-10-09, spikes/preflight-testing-1). Every case drives the REAL code — the lead's
`scripts/lead/merge-dbuild-lane.sh batch` / `batch-scripting` / `batch-land` (JAH_MERGE_SCRIPT, else
<workspace>/scripts/lead/), its `fork-pin-check.sh`, this tree's `check-trailers.sh`, `gate-scope.py
--attribute` and `gate_runlog.py` — against a TOY REPO PAIR built here (a Studio repo with an `irisgl`
gitlink, an irisgl repo with a `thirdparty/ogre-next` gitlink, a toy fork with diverging lines, the d-build,
lane and rc worktrees laid out as the lead's are), a PRIVATE run log, token directory and lead scratch. The
judge is a stub that logs how it was called (the real judge is gate.ci_check's and gate.fix_round's subject;
its diff is empty):

  1. THE FORK IS FROZEN PER BATCH: lanes on r0 (d-build's) / f1 / f2 are refused, each lane behind named
     ("re-pin <lane> to <P>"); THREE LANES on the one pin f2 -> ONE CANDIDATE on batch-<tag> (d-build
     untouched), P = f2, the fork tier printed; two lanes touched docs/SCRIPTING.md -> the state says regenerate, `batch-scripting` (what
     rc-gate.sh runs on the built candidate) commits the REGENERATED file onto batch-<tag>, and batch-land runs
     ONE judge on d-build..<that sha> and fast-forwards BOTH repos to it;
  2. CONFLICTS refuse the whole batch, naming the lane and the file, nothing moved, no stray ref: a Studio
     pair and an irisgl pair; a lane that changed the judge (until JAH_JUDGE_READ names its tip) and a lane
     with an attribution trailer are refused the same way;
  3. THE FORK FREEZE: diverging pins (fx against d-build's f2; g1 and g2, both descending from d-build's pin)
     are refused, exit 5;
  4. A RED, ATTRIBUTED: batch-land on a red judge moves nothing; it passes --verdict through; it runs nothing
     on an inherited DISPLAY (--display :NN is required, :60-:99 with an X lock) and prints the command; run,
     the attribution names a lane by ANY red solo on its own tip, says ABSENT where a lane's build lacks the
     row (never blames it), leaves a NOADMIT cell INCOMPLETE, calls a row red only at the candidate a
     COMBINATION DEFECT (exit 3), one red at d-build's tip (the --control: the base build rc-base, which must be AT d-build's tip) a
     D-BUILD DEFECT naming nobody,
     and one green everywhere NOT REPRODUCED; the whole card is held once and
     the gate slot is never taken (a held slot does not stop it); records: `lanes`, tier, reason, retry;
     a lane worktree off its tip is refused; a stale candidate (d-build moved) is refused at batch-land;
  5. THE RECORDS' `lanes` IS A LIST; 6. `--joint` IS GONE.

On a box without the lead's tooling (no <workspace>/scripts/lead/merge-dbuild-lane.sh) the row says
`SKIP: lead tooling not present` and exits 77 (ctest: Skipped) — an absence, never a pass.

Run: gate_batch_test.py <source-dir> <build-dir>
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

FAILURES = []
SKIP = 77


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

# each tree's toy rows read its own files: BREAKS_X / BREAKS_Y (lane h / lane i; the candidate has both),
# HAS_Z (lane i registers row.z; lane h does not), BASE_BROKEN (d-build's tree only: the control's row.base)
TOY_CTEST = r'''
cmake_minimum_required(VERSION 3.20)
project(toylane NONE)
enable_testing()
add_test(NAME row.x COMMAND sh -c "if [ -e '@WT@/BREAKS_X' ]; then echo 'FAIL: BREAKS_X is in this tree'; exit 1; fi; echo ok")
add_test(NAME row.combo COMMAND sh -c "if [ -e '@WT@/BREAKS_X' ] && [ -e '@WT@/BREAKS_Y' ]; then echo 'FAIL: X and Y together'; exit 1; fi; echo ok")
add_test(NAME row.flake COMMAND sh -c "echo ok")
add_test(NAME row.noadmit COMMAND sh -c "if [ -e '@WT@/BREAKS_X' ]; then echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - row.noadmit'; exit 75; fi; echo ok")
add_test(NAME row.base COMMAND sh -c "if [ -e '@WT@/BASE_BROKEN' ]; then echo 'FAIL: broken on d-build already'; exit 1; fi; echo ok")
add_test(NAME row.ctl COMMAND sh -c "if [ -e '@WT@/BASE_BROKEN' ]; then echo 'NOADMIT vram: no admission for 2 tokens within 1 s (0 of 3 free at the last look) - row.ctl'; exit 75; elif [ -e '@WT@/BREAKS_X' ]; then echo 'FAIL: ctl'; exit 1; fi; echo ok")
if(EXISTS "@WT@/HAS_Z")
  add_test(NAME row.z COMMAND sh -c "echo 'FAIL: z is broken'; exit 1")
endif()
'''


def main(source, build):
    scripts = os.path.join(source, "scripts")
    sys.path.insert(0, scripts)
    import gate_runlog as rl
    for k in ("JAH_GATE_SLOT_HELD", "JAH_VRAM_HELD", "JAH_VRAM_ALL", "DISPLAY", "JAH_JUDGE_READ", "OGRE_PREFIX",
              "JAH_DUMP_API_DOCS"):
        os.environ.pop(k, None)
    merge = os.environ.get("JAH_MERGE_SCRIPT") or os.path.join(rl.workspace_root(), "scripts", "lead",
                                                               "merge-dbuild-lane.sh")
    if not os.path.isfile(merge):
        # an honest absence (the Mac's clone without the lead repo, a CI box): neither a FAIL nor a pass
        print(f"SKIP: lead tooling not present ({merge})")
        return SKIP
    have = "batch-land" in open(merge).read() and "batch-scripting" in open(merge).read()
    check(have, f"the lead's merge script carries `batch` / `batch-scripting` / `batch-land` ({merge}; the lane's "
                f"patch: spikes/batch-gate-1/lead-scripts.patch)")
    if not have: return 1
    scratch = tempfile.mkdtemp(prefix="gate-batch-test-")
    try:
        return run(source, scripts, merge, scratch, rl, build)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def run(source, scripts, merge, scratch, rl, build_dir):
    gcfg = os.path.join(scratch, "gitconfig")
    open(gcfg, "w").write("[user]\n\tname = jahshaka\n\temail = jahshaka@gmail.com\n[init]\n\tdefaultBranch = main\n"
                          "[protocol \"file\"]\n\tallow = always\n[uploadpack]\n\tallowAnySHA1InWant = true\n"
                          "[advice]\n\tdetachedHead = false\n")
    # the toy's own commits take jahshaka from the toy's git config; NO GIT_AUTHOR_* is exported anywhere, and the
    # merge script runs under a config naming someone else — what it commits must carry jahshaka by its own hand
    for k in ("GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_NAME", "GIT_COMMITTER_EMAIL"):
        os.environ.pop(k, None)
    genv = dict(os.environ, GIT_CONFIG_GLOBAL=gcfg, GIT_CONFIG_NOSYSTEM="1")
    gcfg2 = os.path.join(scratch, "gitconfig-other")
    open(gcfg2, "w").write(open(gcfg).read().replace("name = jahshaka", "name = not-jahshaka")
                           .replace("email = jahshaka@gmail.com", "email = other@example.com"))

    def git(cwd, *args):
        r = subprocess.run(["git", "-C", cwd] + list(args), capture_output=True, text=True, env=genv)
        if r.returncode != 0:
            raise RuntimeError(f"git {' '.join(args)} in {cwd}: {r.stderr.strip()}")
        return r.stdout.strip()

    def git_try(cwd, *args):
        return subprocess.run(["git", "-C", cwd] + list(args), capture_output=True, text=True, env=genv).stdout.strip()

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

    # ---- the toy fork: r0 (the stack tag) -> f1 -> f2 on `jahshaka`; fx from r0; g1, g2 from f2 -------------
    fork = os.path.join(scratch, "fork")
    os.makedirs(fork); git(fork, "init", "-q", "-b", "jahshaka")
    r0 = commit(fork, {"OgreMain.txt": "r0\n"}, "r0"); git(fork, "tag", "jahshaka-stack-v1")
    f1 = commit(fork, {"OgreMain.txt": "f1\n"}, "f1")
    f2 = commit(fork, {"OgreMain.txt": "f2\n"}, "f2")
    side = {}
    for br, at, txt in (("side", r0, "fx"), ("g1", f2, "g1"), ("g2", f2, "g2")):
        git(fork, "checkout", "-q", "-b", br, at)
        side[txt] = commit(fork, {"OgreMain.txt": txt + "\n"}, txt)
    fx, g1, g2 = side["fx"], side["g1"], side["g2"]
    git(fork, "checkout", "-q", "jahshaka")
    changes = os.path.join(scratch, "OGRE_NEXT_CHANGES.md")
    open(changes, "w").write("".join(f"- {c[:9]} toy\n" for c in (f1, f2, fx, g1, g2)))

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
    D = os.path.join(M, ".claude", "worktrees", "d-build")
    git(M, "worktree", "add", "-q", "-b", "d-build", D, sbase)

    def clone_irisgl(wt, branch, at, fork_at=None):
        p = os.path.join(wt, "irisgl")
        if os.path.isdir(p): shutil.rmtree(p)
        subprocess.run(["git", "clone", "-q", ig, p], env=genv, check=True)
        git(p, "fetch", "-q", ig, "+refs/heads/*:refs/remotes/main/*")
        if branch: git(p, "checkout", "-q", "-b", branch, at)
        else: git(p, "checkout", "-q", at)
        if fork_at:
            subprocess.run(["git", "clone", "-q", fork, os.path.join(p, "thirdparty", "ogre-next")], env=genv, check=True)
            git(os.path.join(p, "thirdparty", "ogre-next"), "checkout", "-q", fork_at)
        return p
    clone_irisgl(D, "d-build", ibase)

    def lane(name, studio=None, irisgl=None, pin=None, msg=None):
        """A lane worktree off d-build's tip with its own irisgl clone; returns (worktree, tip, irisgl tip, spec)."""
        base = git(D, "rev-parse", "HEAD"); ib = git(os.path.join(D, "irisgl"), "rev-parse", "HEAD")
        W = os.path.join(M, ".claude", "worktrees", name)
        git(M, "worktree", "add", "-q", "-b", name, W, base)
        wi = clone_irisgl(W, f"{name}-irisgl", ib)
        it = ib
        if irisgl or pin:
            it = commit(wi, irisgl, f"{name}: irisgl", {"thirdparty/ogre-next": pin} if pin else None)
        st = commit(W, studio or {}, msg or f"{name}: studio", {"irisgl": it})
        return W, st, it, f"{name}:{st}:{it}"

    def toy_build(tree):
        tsrc, tb = os.path.join(scratch, "toy-" + os.path.basename(tree)), os.path.join(tree, "build-linux")
        write(os.path.join(tsrc, "CMakeLists.txt"), TOY_CTEST.replace("@WT@", tree))
        r = subprocess.run(["cmake", "-S", tsrc, "-B", tb], capture_output=True, text=True)
        check(r.returncode == 0, f"the toy ctest project of {os.path.basename(tree)} configures")

    def rc_tree(tag, sc, ic, fork_at=None):
        """The candidate's rc tree, as rc-gate.sh makes it: a worktree at the candidate, irisgl at its pin."""
        T = os.path.join(M, ".claude", "worktrees", f"rc-batch-{tag}")
        git(M, "worktree", "add", "-q", "-b", f"rc-batch-{tag}", T, sc)
        clone_irisgl(T, None, ic, fork_at)
        return T

    lead = os.path.join(scratch, "lead")
    judge_log, judge_mode = os.path.join(scratch, "judge.log"), os.path.join(scratch, "judge.mode")
    x11 = os.path.join(scratch, "x11"); os.makedirs(x11)
    menv = dict(genv, GIT_CONFIG_GLOBAL=gcfg2, JAH_FPC_MAIN=M, JAH_LEAD_SCRATCH=lead, JAH_FPC_FORK_CLONE=os.path.join(ig, "thirdparty", "ogre-next"),
                JAH_FPC_FORK_REMOTE=fork, JAH_FPC_CHANGES=changes, TOY_JUDGE_LOG=judge_log, TOY_JUDGE_MODE=judge_mode,
                JAH_X11_ROOT=x11)

    def mscript(*args, **env):
        p = subprocess.run(["bash", merge] + list(args), capture_output=True, text=True, env=dict(menv, **env))
        return p.returncode, p.stdout + p.stderr

    def refs(name):
        return (git_try(M, "rev-parse", "-q", "--verify", f"refs/heads/{name}"),
                git_try(os.path.join(D, "irisgl"), "rev-parse", "-q", "--verify", f"refs/heads/{name}"))

    def stray(tag, lanes_):
        """Every ref a refused batch could have left: batch-<tag> in the three repos, batch-tip-<tag> in the lanes'
        irisgl, refs/batch-tmp/ in d-build's irisgl."""
        out = [r for r in refs(f"batch-{tag}") if r]
        out += [git_try(ig, "rev-parse", "-q", "--verify", f"refs/heads/batch-{tag}")]
        out += [git_try(os.path.join(M, ".claude", "worktrees", l, "irisgl"), "rev-parse", "-q", "--verify",
                        f"refs/heads/batch-tip-{tag}") for l in lanes_]
        out += [git_try(os.path.join(D, "irisgl"), "for-each-ref", "refs/batch-tmp/")]
        return [r for r in out if r]

    def dbuild():
        return git(D, "rev-parse", "HEAD"), git(os.path.join(D, "irisgl"), "rev-parse", "HEAD")

    # ---- 1. three lanes -> one candidate; SCRIPTING.md regenerated; one judge; both repos fast-forwarded ----
    print("1. the fork is FROZEN per batch: pins r0 (d-build's) / f1 / f2 -> refused, the lanes behind named")
    _, _, _, spp1 = lane("lane-p1", {"src/p1.txt": "p\n"})
    _, _, _, spp2 = lane("lane-p2", {"src/p2.txt": "p\n"}, None, pin=f1)
    _, _, _, spp3 = lane("lane-p3", {"src/p3.txt": "p\n"}, None, pin=f2)
    rc, out = mscript("batch", "t1x", spp1, spp2, spp3)
    check(rc == 5 and f"re-pin lane-p1 to {f2[:9]}" in out and f"re-pin lane-p2 to {f2[:9]}" in out
          and "re-pin lane-p3" not in out, "ANCESTOR pins are refused: each lane behind the batch's one pin is named "
          "(\"re-pin <lane> to <P>\"; exit %d)" % rc)
    check(stray("t1x", ["lane-p1", "lane-p2", "lane-p3"]) == [], "...nothing moved, no stray ref")
    print("1b. three lanes on the ONE pin f2 (the batch's bump) -> one candidate")
    Wa, sta, ita, spa = lane("lane-a", {"src/a.txt": "a1\nA2\na3\n"}, {"engine.txt": "one\nTWO\nthree\n"}, pin=f2)
    Wb, stb, itb, spb = lane("lane-b", {"src/b.txt": "B\n", "docs/SCRIPTING.md": "verbs vb\n"}, None, pin=f2)
    Wc, stc, itc, spc = lane("lane-c", {"src/c.txt": "c\n", "docs/SCRIPTING.md": "verbs vc\n"},
                             {"render.txt": "r\n"}, pin=f2)
    s0, i0 = dbuild()
    rc, out = mscript("batch", "t1", spa, spb, spc)
    print("\n".join("     | " + l for l in out.splitlines()[-8:]))
    sc, ic = refs("batch-t1")
    check(rc == 0 and sc and ic, "the batch is accepted and batch-t1 exists in both repos (exit %d)" % rc)
    check(dbuild() == (s0, i0), "...and d-build did NOT move (the candidate is on its own branches)")
    check(all(f"fork pins: {l} " in out for l in ("lane-a", "lane-b", "lane-c")), "...every lane's fork pin is printed")
    check(f"fork pin P = {f2[:9]}" in out and "MOVED" in out, "...P = f2, the batch's one pin, said as a move")
    state = open(os.path.join(lead, "batch-t1.state")).read() if os.path.exists(os.path.join(lead, "batch-t1.state")) else ""
    check("SCRIPTING_REGEN=1" in state and "touched by 2 lanes" in out,
          "...two lanes touched docs/SCRIPTING.md: the state asks rc-gate to regenerate it")
    if sc and ic:
        check(git(os.path.join(D, "irisgl"), "rev-parse", f"{ic}:thirdparty/ogre-next") == f2,
              "...the irisgl candidate pins P")
        check(git(D, "rev-parse", f"{sc}:irisgl") == ic, "...the Studio candidate pins the irisgl candidate")
        chain = git(D, "rev-list", "--first-parent", f"{sbase}..{sc}").split()
        check(len(chain) == 3 and [git(D, "rev-parse", f"{c}^2") for c in chain] == [stc, stb, sta],
              "...one merge per lane on the first-parent line, in the lead's order, each lane's tip its second parent")
        check(git(D, "show", f"{sc}:src/a.txt") == "a1\nA2\na3" and git(D, "show", f"{sc}:src/c.txt") == "c"
              and git(os.path.join(D, "irisgl"), "show", f"{ic}:engine.txt") == "one\nTWO\nthree",
              "...the candidate holds every lane's change in both repos")
        check("JAH_GATE_TIER=fork" in out and f"JAH_GATE_RANGE={sbase}..{sc}" in out
              and "JAH_GATE_LANES=lane-a,lane-b,lane-c" in out and f"rc-gate.sh batch-t1 {sc}" in out,
              "...the ONE gate command: rc-gate.sh on the candidate, d-build..candidate, the fork tier, the lanes")
    for l in ("lane-a", "lane-b", "lane-c"):
        mc = git(D, "rev-parse", f"{sc}~{['lane-c', 'lane-b', 'lane-a'].index(l)}")
        check(git(D, "log", "-1", "--format=%an <%ae>", mc) == "jahshaka <jahshaka@gmail.com>",
              f"...the candidate's merge of {l} is authored jahshaka though the caller's git config names someone else")
    # RC-GATE'S OWN regenerate-and-rebuild branch (`rc-gate.sh batch-t1 --scripting <rc tree>`), with THIS build's real
    # binary: the rc tree's build-linux is a configured (toy) build dir whose bin/ is the real one
    T1 = rc_tree("t1", sc, ic)
    toy_build(T1)
    os.symlink(os.path.join(os.path.abspath(build_dir), "bin"), os.path.join(T1, "build-linux", "bin"))
    rcg = os.path.join(os.path.dirname(merge), "rc-gate.sh")
    p = subprocess.run(["bash", rcg, "batch-t1", "--scripting", T1], capture_output=True, text=True,
                       env=dict(menv), timeout=300)
    rst = open(os.path.join(lead, "rc-batch-t1-scripting.state")).read() if os.path.exists(
        os.path.join(lead, "rc-batch-t1-scripting.state")) else ""
    log = open(os.path.join(lead, "rc-batch-t1-scripting.log")).read() if os.path.exists(
        os.path.join(lead, "rc-batch-t1-scripting.log")) else ""
    new = log.split("REGENERATED ", 1)[1].split()[0] if "REGENERATED " in log else ""
    check(p.returncode == 0 and "SCRIPTING.md regenerated at" in rst and "rebuilt at" in rst,
          "rc-gate --scripting: the real binary's --dump-api-docs differed, the file was committed and the tree "
          "REBUILT (exit %d; %s)" % (p.returncode, rst.strip().splitlines()[-2:] if rst else p.stdout[-200:]))
    check(p.returncode == 0 and new and refs("batch-t1")[0] == new and git(D, "rev-parse", f"{new}^") == sc,
          "batch-scripting: the regenerated file is ONE commit on top of the candidate, and batch-t1 names it (%s)"
          % log.strip()[-80:])
    if new:
        check(git(D, "log", "-1", "--format=%an|%s", new) == "jahshaka|SCRIPTING.md regenerated at batch t1",
              "...authored jahshaka, its message names the batch")
        check(f"SC={new}" in open(os.path.join(lead, "batch-t1.state")).read(), "...and the state's candidate is it")
        check(git(D, "show", f"{new}:docs/SCRIPTING.md").strip() == open(os.path.join(source, "docs", "SCRIPTING.md")).read().strip(),
              "...its SCRIPTING.md is the REAL generator's output (byte-equal to this tree's checked-in file)")
    open(judge_mode, "w").write("green")
    rc, out = mscript("batch-land", "t1")
    calls = open(judge_log).read().splitlines() if os.path.exists(judge_log) else []
    check(calls == [f"{sbase}..{new} --build {T1}/build-linux"], "batch-land: ONE judge run, on d-build..<the "
          "regenerated candidate>, with the rc tree's build (%s)" % calls)
    check(rc == 0 and dbuild() == (new, ic), "...green -> d-build fast-forwarded to it in both repos (exit %d)" % rc)
    check(git(D, "show", "HEAD:docs/SCRIPTING.md") not in ("verbs vb", "verbs vc") and dbuild()[0] == new,
          "...and d-build's SCRIPTING.md is the REGENERATED one, not either lane's text (batch-land ran check-trailers "
          "on that commit)")
    check(git(ig, "rev-parse", "refs/heads/d-build") == ic, "...the main tree's irisgl d-build ref follows")
    check("HASHES" in out and "READY TO PUSH" in out, "...the HASHES line and the push line are printed")

    # ---- 2. refusals: conflicts (both repos), the judge diff, a trailer — nothing moved, no stray ref ---------
    print("2. refusals before any ref moves")
    s1 = dbuild()
    _, _, _, spd = lane("lane-d", {"src/a.txt": "a1\nD2\na3\n"})
    _, _, _, spe = lane("lane-e", {"src/a.txt": "a1\nE2\na3\n", "src/e.txt": "e\n"})
    rc, out = mscript("batch", "t2", spd, spe)
    check(rc != 0 and "lane-e CONFLICTS in Studio" in out and "src/a.txt" in out,
          "a Studio conflict: refused, naming the lane and the file (exit %d)" % rc)
    check(dbuild() == s1 and stray("t2", ["lane-d", "lane-e"]) == [], "...nothing moved, no stray ref")
    _, _, _, spk = lane("lane-k", {"src/k.txt": "k\n"}, {"engine.txt": "one\nK\nthree\n"})
    _, _, _, spl = lane("lane-l", {"src/l.txt": "l\n"}, {"engine.txt": "one\nL\nthree\n"})
    rc, out = mscript("batch", "t2i", spk, spl)
    check(rc != 0 and "lane-l CONFLICTS in irisgl" in out and "engine.txt" in out,
          "an irisgl conflict: refused, naming the lane and the file (exit %d)" % rc)
    check(dbuild() == s1 and stray("t2i", ["lane-k", "lane-l"]) == [], "...nothing moved, no stray ref")
    _, stm, _, spm = lane("lane-m", {"scripts/ci_gate_check.py": STUB_JUDGE + "# a lane's edit\n"})
    rc, out = mscript("batch", "t2j", spk, spm)
    check(rc == 6 and "lane lane-m changed the merge judge" in out and "JAH_JUDGE_READ" in out,
          "a lane that changed the judge: refused (exit %d)" % rc)
    check(dbuild() == s1 and stray("t2j", ["lane-k", "lane-m"]) == [],
          "...nothing moved, no stray ref (lane-k's batch-tip made before the refusal is gone)")
    rc, out = mscript("batch", "t2r", spk, spm, JAH_JUDGE_READ=stm)
    check(rc == 0 and "judge diff READ by the lead at lane-m" in out, "...and accepted once JAH_JUDGE_READ names its tip")
    _, _, _, spn = lane("lane-n", {"src/n.txt": "n\n"}, msg="lane-n: studio\n\nCo-Authored-By: Claude <noreply@anthropic.com>")
    rc, out = mscript("batch", "t2t", spk, spn)
    check(rc != 0 and "lane-n's Studio trailers" in out and "TRAILER" in out,
          "a lane with an attribution trailer: refused (exit %d)" % rc)
    check(dbuild() == s1 and stray("t2t", ["lane-k", "lane-n"]) == [], "...nothing moved, no stray ref")

    # ---- 3. the fork freeze -----------------------------------------------------------------------------------
    print("3. diverging fork pins")
    _, _, _, spf = lane("lane-f", {"src/f.txt": "f\n"}, None, pin=fx)
    _, _, _, spg = lane("lane-g", {"src/g.txt": "g\n"})
    rc, out = mscript("batch", "t3", spf, spg)
    check(rc == 5 and "two fork lines" in out and fx[:9] in out,
          "fx against d-build's f2: refused, the pins named (exit %d)" % rc)
    check(dbuild() == s1 and stray("t3", ["lane-f", "lane-g"]) == [], "...nothing moved, no stray ref")
    _, _, _, spg1 = lane("lane-g1", {"src/g1.txt": "g\n"}, None, pin=g1)
    _, _, _, spg2 = lane("lane-g2", {"src/g2.txt": "g\n"}, None, pin=g2)
    rc, out = mscript("batch", "t3b", spg1, spg2)
    check(rc == 5 and "two fork lines" in out and g1[:9] in out and g2[:9] in out,
          "g1 and g2, BOTH descending from d-build's pin, diverge from each other: refused, exit 5 (exit %d)" % rc)
    check(stray("t3b", ["lane-g1", "lane-g2"]) == [], "...no stray ref")
    _, _, _, spg3 = lane("lane-g3", {"src/g3.txt": "g\n"}, None, pin=g1)
    rc, out = mscript("batch", "t3c", spg3)
    check(rc == 5 and "not on the fork's jahshaka branch" in out,
          "ONE lane on g1 (a descendant of d-build's pin, OFF the fork's jahshaka branch): refused (exit %d)" % rc)
    check(stray("t3c", ["lane-g3"]) == [], "...no stray ref")

    # ---- 4. a red, attributed -----------------------------------------------------------------------------
    print("4. a red batch, attributed")
    pin_now = git(os.path.join(D, "irisgl"), "rev-parse", "HEAD:thirdparty/ogre-next")
    install = os.path.join(scratch, "install"); os.makedirs(install)
    open(os.path.join(install, "BUILT_FROM"), "w").write(pin_now + "\n")
    lanes4 = {}
    for name, files in (("lane-h", {"BREAKS_X": "1\n"}), ("lane-i", {"BREAKS_Y": "1\n", "HAS_Z": "1\n"})):
        W, st, it, sp = lane(name, dict(files, **{f"src/{name}.txt": "x\n"}))
        subprocess.run(["git", "clone", "-q", fork, os.path.join(W, "irisgl", "thirdparty", "ogre-next")],
                       env=genv, check=True)
        git(os.path.join(W, "irisgl", "thirdparty", "ogre-next"), "checkout", "-q", pin_now)
        toy_build(W)
        lanes4[name] = (W, st, sp)
    rc, out = mscript("batch", "t4", lanes4["lane-h"][2], lanes4["lane-i"][2])
    sc4, ic4 = refs("batch-t4")
    check(rc == 0 and sc4 and "JAH_GATE_TIER=scoped" in out and f"fork pin P = {pin_now[:9]}" in out
          and "SCRIPTING_REGEN=0" in open(os.path.join(lead, "batch-t4.state")).read(),
          "equal pins -> accepted, P = d-build's pin, the scoped gate, no regeneration (exit %d)" % rc)
    T4 = rc_tree("t4", sc4, ic4, pin_now); toy_build(T4)
    open(judge_mode, "w").write("row.x,row.combo,row.flake,row.z,row.noadmit,row.base,row.ctl")
    s4 = dbuild()
    # THE CONTROL is the BASE BUILD rc-base (TESTING_V3_SPEC §1.3.2), never $D: absent or behind d-build -> refused
    rc, out = mscript("batch-land", "t4")
    check(rc == 6 and "no control tree at" in out and "--attribute" not in out,
          "no rc-base: the attribution is refused with the reason (exit %d)" % rc)
    RB = os.path.join(M, ".claude", "worktrees", "rc-base")
    git(M, "worktree", "add", "-q", "--detach", RB, sbase)
    rc, out = mscript("batch-land", "t4")
    check(rc == 6 and f"is at {sbase}" in out and "rc-base is behind d-build" in out,
          "rc-base BEHIND d-build (at the toy's first commit): refused, naming both tips (exit %d)" % rc)
    git(RB, "checkout", "-q", "--detach", s4[0])
    clone_irisgl(RB, None, git(os.path.join(D, "irisgl"), "rev-parse", "HEAD"), pin_now)
    os.makedirs(os.path.join(RB, "build-linux"), exist_ok=True); shutil.rmtree(os.path.join(RB, "build-linux"))
    rc, out = mscript("batch-land", "t4")
    check(rc == 6 and "has no build-linux" in out, "rc-base at d-build's tip but UNBUILT: refused (exit %d)" % rc)
    write(os.path.join(RB, "BASE_BROKEN"), "1\n"); toy_build(RB)
    rc, out = mscript("batch-land", "t4", "--verdict", "row.flake=a toy verdict", DISPLAY=":0")
    calls = open(judge_log).read().splitlines()
    check(rc == 6 and dbuild() == s4 and refs("batch-t4") == (sc4, ic4),
          "batch-land on a red judge: refused, nothing moved (exit %d)" % rc)
    check(calls[-1].endswith("--verdict row.flake=a toy verdict"), "...--verdict passes through to the judge (%s)" % calls[-1][-40:])
    acmd = [l for l in out.splitlines() if "--attribute" in l]
    check(len(acmd) == 1 and "no --display" in acmd[0] and not os.path.exists(os.path.join(lead, "attribute-batch-t4.log")),
          "...with DISPLAY=:0 inherited and no --display it RUNS NOTHING and prints the command")
    check(acmd and "row.base,row.combo,row.ctl,row.flake,row.noadmit,row.x,row.z" in acmd[0] and "--display :NN" in acmd[0] and "--batch t4" in acmd[0]
          and f"--candidate {T4}:{sc4}" in acmd[0] and f"--control {RB}:{s4[0]}" in acmd[0]
          and all(f"{n}:{lanes4[n][0]}:{lanes4[n][1]}" in acmd[0] for n in lanes4),
          "...the command: the red rows, the tag, the candidate and d-build (the control), every lane's worktree and tip")
    rc, out = mscript("batch-land", "t4", "--display", ":0")
    check(rc == 64 and "not a rig display" in out, "--display :0 is refused (exit %d)" % rc)
    rc, out = mscript("batch-land", "t4", "--display", ":96")
    check(rc == 64 and "has no X server" in out, "--display :96 with no X lock is refused (exit %d)" % rc)
    # a live (fake) X server on :97 — the display the attribution is TOLD to use
    os.makedirs(os.path.join(x11, ".X11-unix"))
    sock = os.path.join(x11, ".X11-unix", "X97")
    fake_x = subprocess.Popen([sys.executable, "-c", "import socket,time,sys\ns=socket.socket(socket.AF_UNIX)\n"
                               "s.bind(sys.argv[1])\ns.listen(16)\nwhile True:\n    c, _ = s.accept(); c.close()", sock])
    for _ in range(50):
        if os.path.exists(sock): break
        time.sleep(0.1)
    open(os.path.join(x11, ".X97-lock"), "w").write("%10d\n" % fake_x.pid)
    # the attribution itself, with the gate slot HELD by another gate the whole time
    runs = os.path.join(scratch, "runs"); vdir = os.path.join(scratch, "vram")
    reg = os.path.join(scratch, "registry"); os.makedirs(reg)
    json.dump({"defects": []}, open(os.path.join(reg, "defects.json"), "w"))
    pend = os.path.join(reg, "defects.pending")      # next to the registry, as VERDICT-1's reader looks for it
    aenv = dict(os.environ, JAH_RUN_LOG_DIR=runs, JAH_VRAM_DIR=vdir, JAH_VRAM_TOKENS="3", JAH_VRAM_WAIT="5",
                OGRE_PREFIX=install, JAH_GATE_REQUEUE="0", JAH_X11_ROOT=x11, JAH_DEFECTS_FILE=os.path.join(reg, "defects.json"),
                DISPLAY=":0")      # inherited and WRONG: the attribution must use --display only
    for bad_args, why in ((["--display", ":0"], "not a rig display"), (["--display", ":96"], "has no X server")):
        a2 = [x if x != ":NN" else bad_args[1] for x in (acmd[0].split("gate-scope.py", 1)[1].split() if acmd else [])]
        p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + a2, capture_output=True,
                           text=True, env=aenv)
        check(p.returncode == 64 and why in p.stderr, f"gate-scope --attribute {bad_args[1]}: refused ({why})")
    a3 = [x for x in (acmd[0].split("gate-scope.py", 1)[1].split() if acmd else [])]
    i3 = a3.index("--control") if "--control" in a3 else -1
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + (a3[:i3] + a3[i3 + 2:] if i3 >= 0 else a3),
                       capture_output=True, text=True, env=aenv)
    check(p.returncode == 64 and "--control" in p.stderr, "gate-scope --attribute without --control: refused (required)")
    holder = subprocess.Popen([sys.executable, os.path.join(scripts, "vram_tokens.py"), "gate", "--label", "a-sibling-gate",
                               "--", "sleep", "120"], env=aenv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    args = [x if x != ":NN" else ":97" for x in (acmd[0].split("gate-scope.py", 1)[1].split() if acmd else [])]
    t0 = time.time()
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + args, capture_output=True,
                       text=True, env=aenv)
    took = time.time() - t0
    holder.kill(); holder.wait()
    fake_x.kill(); fake_x.wait()
    aout = p.stdout + p.stderr
    print("\n".join("     | " + l for l in aout.splitlines() if l.startswith(("row", "=>", "==="))))
    check(p.returncode == 3, "the attribution exits 3: a COMBINATION DEFECT refuses the batch (exit %d)" % p.returncode)
    check("gate-slot" not in aout and took < 100 and aout.count("vram: the whole card") == 1,
          "...it never took the gate slot (another gate held it throughout; %.0f s) and held the card ONCE" % took)
    check("row.x | lane-h | 3/3 red | FAIL: BREAKS_X is in this tree" in aout and "row.x | lane-i | 0/3 red | -" in aout
          and "=> row.x: NAMED lane-h (3/3 red on its own tip)" in aout, "row.x: NAMED lane-h, its first failing check")
    check("row.z | lane-h | ABSENT" in aout and "=> row.z: NAMED lane-i (3/3" in aout and "lane-h (" not in
          [l for l in aout.splitlines() if l.startswith("=> row.z")][0], "row.z: ABSENT from lane-h's build (not blamed), "
          "NAMED lane-i")
    check("=> row.combo: COMBINATION DEFECT" in aout and "row.combo | CANDIDATE | 3/3 red" in aout,
          "row.combo: green on each lane, red at the candidate -> COMBINATION DEFECT")
    check("=> row.flake: NOT REPRODUCED" in aout, "row.flake: green everywhere -> NOT REPRODUCED (the verdict door)")
    check("=> row.ctl: INCOMPLETE — a CONTROL" in aout and "row.ctl | lane-h | 3/3 red" in aout,
          "row.ctl: the control NOADMIT while lane-h is red -> INCOMPLETE, nobody named (the baseline first)")
    pending = {}
    for f in sorted(os.listdir(pend)) if os.path.isdir(pend) else []:
        pending[f] = json.load(open(os.path.join(pend, f)))
    kinds = {e["rows"][0]: e["kind"] for e in pending.values()}
    check(kinds == {"row.combo": "combination", "row.base": "defect", "row.flake": "nondeterminism"},
          "REGISTERED, not printed: one testing/defects.pending/<id>.json per finding (%s)" % kinds)
    base_keys = {"id", "rows", "kind", "cause", "first_seen", "state", "found_by", "recheck", "expires"}
    check(all(set(e) == (base_keys | ({"uses", "suspects", "census"} if e["kind"] == "nondeterminism" else set()))
              and e["state"] == "open" and e["found_by"] == "gate" and set(e["first_seen"]) == {"tip", "pin", "run"}
              and all(e["first_seen"].values()) and len(e["recheck"]) == 10 for e in pending.values()),
          "...each in the FULL §1.5 schema: state open, found_by gate, first_seen {tip, pin, run}, recheck/expires dates; "
          "NOT REPRODUCED also uses, suspects, census")
    nr = [e for e in pending.values() if e["kind"] == "nondeterminism"]
    check(nr and nr[0]["uses"] == 1 and nr[0]["suspects"] == ["lane-h", "lane-i"] and isinstance(nr[0]["census"], dict),
          "...NOT REPRODUCED: uses 1, the batch's lanes as suspects, the record's census")
    # VERDICT-1's reader QUARANTINES a malformed entry (the finding never reaches the door): every pending file must
    # load through it
    loader = rl
    if not hasattr(loader, "defects_load"):
        check(False, "VERDICT-1's defects_load is in this tree's gate_runlog")
    else:
        old = os.environ.get("JAH_DEFECTS_FILE"); os.environ["JAH_DEFECTS_FILE"] = os.path.join(reg, "defects.json")
        try:
            got, why = loader.defects_load()
        finally:
            if old is None: os.environ.pop("JAH_DEFECTS_FILE", None)
            else: os.environ["JAH_DEFECTS_FILE"] = old
        check(got is not None and set(got) == set(e["id"] for e in pending.values()),
              "...and every pending file LOADS through VERDICT-1's defects_load (%s)" % (why or sorted(got or {})))
    check(all(e["first_seen"]["tip"] == (s4[0] if e["kind"] == "defect" else sc4) for e in pending.values()),
          "...first seen at the candidate (a d-build defect: at d-build's tip)")
    check("row.base | CONTROL rc-base | 3/3 red" in aout and "=> row.base: D-BUILD DEFECT" in aout
          and "NAMED" not in [l for l in aout.splitlines() if l.startswith("=> row.base")][0],
          "row.base: red on d-build's own tip -> a D-BUILD DEFECT, naming nobody")
    check("row.noadmit | lane-h | INCOMPLETE" in aout and "=> row.noadmit: INCOMPLETE" in aout,
          "row.noadmit: a NOADMIT is no verdict — the cell is INCOMPLETE, the lane neither named nor cleared")
    recs = []
    for f in sorted(os.listdir(runs)) if os.path.isdir(runs) else []:
        recs += [json.loads(l) for l in open(os.path.join(runs, f))]
    tips = {lanes4["lane-h"][1]: ["lane-h"], lanes4["lane-i"][1]: ["lane-i"], sc4: ["lane-h", "lane-i"],
            s4[0]: ["rc-base"]}
    check(len(recs) == 78 and all(r.get("reason") == "attribute:t4" and r.get("retry") is True
                                  and r.get("tier") == "scoped" for r in recs),
          "78 records (7 rows x 4 trees x 3, less row.z where it is absent), each `reason: attribute:t4`, a retry, tier "
          "`scoped` (%d)" % len(recs))
    check(all(r["lanes"] == tips.get(r["tip"]["studio"]) for r in recs) and "lane" not in recs[0],
          "...each at ITS tree's own tip; `lanes` = that lane (the control's: rc-base), the candidate's = the batch's "
          "list; no `lane` string")
    check(all(r.get("batch") == "t4" for r in recs), "...and every one carries `batch: t4` (TESTING_V3 §1.6: never a "
          "lane's own record)")
    # THE EXIT CODES besides 3: only an unfinished row -> 7; only a red on d-build's tip -> 5
    def only(rows_):
        out_ = list(args); i_ = out_.index("--attribute"); out_[i_ + 1] = rows_
        return subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + out_, capture_output=True,
                              text=True, env=dict(aenv, JAH_RUN_LOG_DIR=os.path.join(scratch, "runs-codes")))
    fake_x = subprocess.Popen([sys.executable, "-c", "import socket,time,sys,os\nos.path.exists(sys.argv[1]) and "
                               "os.unlink(sys.argv[1])\ns=socket.socket(socket.AF_UNIX)\ns.bind(sys.argv[1])\n"
                               "s.listen(16)\nwhile True:\n    c, _ = s.accept(); c.close()", sock])
    for _ in range(50):
        time.sleep(0.1)
        if os.path.exists(sock): break
    open(os.path.join(x11, ".X97-lock"), "w").write("%10d\n" % fake_x.pid)
    p7, p5 = only("row.noadmit"), only("row.base")
    fake_x.kill(); fake_x.wait()
    check(p7.returncode == 7, "an attribution left INCOMPLETE (row.noadmit alone) exits 7 (exit %d)" % p7.returncode)
    check(p5.returncode == 5, "a d-build defect alone (row.base) exits 5 (exit %d)" % p5.returncode)
    git(lanes4["lane-i"][0], "commit", "-q", "--allow-empty", "-m", "lane-i moved on")
    p = subprocess.run([sys.executable, os.path.join(scripts, "gate-scope.py")] + args, capture_output=True,
                       text=True, env=aenv)
    check(p.returncode == 4 and "not the batch's tip" in p.stderr,
          "a lane worktree moved past its batch tip is refused (the exact-tip rule; exit %d)" % p.returncode)
    git(D, "commit", "-q", "--allow-empty", "-m", "d-build moved on")
    rc, out = mscript("batch-land", "t4")
    check(rc == 1 and "d-build moved since batch t4" in out, "a STALE candidate (d-build moved) is refused at batch-land")
    git(D, "reset", "-q", "--hard", s4[0])

    # ---- 5. the records' `lanes` is a list ------------------------------------------------------------------
    print("5. the run log's `lanes`")
    check(rl.lane_list("p1c-light-list,vr-reorder-1") == ["p1c-light-list", "vr-reorder-1"]
          and rl.lane_list(["a", "b,c", "a"]) == ["a", "b", "c"] and rl.lane_list("solo") == ["solo"],
          "lane_list: a comma list, repeats and one name all give the list")
    check("joint" not in rl.TIERS and not hasattr(rl, "record_lanes"),
          "the run log's tiers no longer list `joint`; no reader of the old single `lane` string remains")

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
    rc = 0
    try:
        rc = main(sys.argv[1], sys.argv[2])
    except Exception as e:                       # a broken fixture is a red, said plainly
        import traceback; traceback.print_exc()
        FAILURES.append(f"{type(e).__name__}: {e}")
    if rc == SKIP and not FAILURES:
        print("\ngate.batch: SKIP (lead tooling not present)"); sys.exit(SKIP)
    print(f"\ngate.batch: {'PASS' if not FAILURES else 'FAIL (%d)' % len(FAILURES)}")
    sys.exit(1 if FAILURES else 0)
