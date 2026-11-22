#!/usr/bin/env python3
"""tooling.runlog_facts - WHAT A RUN RECORD MUST SAY FOR A PRICE TO BE TRUSTED, AND WHAT gate_runlog.py REFUSES (a fake ctest's
verbose stream, no GPU, no build, a scratch HOME and run-log dir):
  (1) a ctest `--test-dir` (and `--test-dir=<d>`) is REFUSED by name, exit 2, nothing recorded (DEV_ERRORS 52: gate_runlog runs
      ctest inside --build; three holds were void);
  (2) two calls started together get two run ids, each carrying its pid (URGENT #209: two solo calls in one second shared
      20261122T115627-1c3544dd9, and `spec.sh reprice` read neither as solo);
  (3) `inframe` counts a row's lines saying a frame COMPILED a shader - Ogre's ` - its shader COMPILED in the frame` and
      `HlmsCompute: job '<j>' parsed its template in the frame (` - one printed past the 256-KB output cap too; a warm PSO-only
      line (`built in the frame from CACHED shader code - a PSO only`) is 0; a row of <build>/tests/test-cold-rows.txt is
      `coldRow` (DEV_ERRORS 49: six rows priced from half-cold runs);
  (4) `load` is the box's 1-minute load over the row ($JAH_LOADAVG_FILE's 42.5 here) and `ncpu` our CPUs (the affinity).
usage: runlog_facts.py <the tree's scripts/gate_runlog.py> (named on the command line: the selector reaches the row from it)"""
import glob
import json
import os
import subprocess
import sys
import tempfile

runlog = os.path.abspath(sys.argv[1])
fails = []


def want(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what), flush=True)
    if not cond:
        fails.append(what)


def records(runs):
    out = []
    for fn in glob.glob(os.path.join(runs, "*.jsonl")):
        out += [json.loads(l) for l in open(fn)]
    return out


with tempfile.TemporaryDirectory(prefix="runlog-facts-") as tmp:
    filler = "".join("1: filler line %06d %s\n" % (i, "x" * 60) for i in range(4500))   # ~320 KB: past OUTPUT_CAP
    stream = os.path.join(tmp, "stream.txt")
    with open(stream, "w") as f:
        f.write("test 1\n    Start 1: fake.halfcold\n1: Test command: /bin/true\n1: Test timeout computed to be: 60\n"
                "1: Hlms: permutation 7 ('pbs', datablock 'm') built in the frame - its shader COMPILED in the frame (3.1 ms)\n"
                + filler +
                "1: HlmsCompute: job 'Photon/Gather' parsed its template in the frame (12 ms)\n"
                "1/3 Test #1: fake.halfcold ....................   Passed    1.23 sec\n"
                "test 2\n    Start 2: fake.warm\n2: Test command: /bin/true\n2: Test timeout computed to be: 60\n"
                "2: Hlms: permutation 7 ('pbs', datablock 'm') built in the frame from CACHED shader code - a PSO only\n"
                "2/3 Test #2: fake.warm ........................   Passed    0.50 sec\n"
                "test 3\n    Start 3: fake.cold\n3: Test command: /bin/true\n3: Test timeout computed to be: 60\n"
                "3: Hlms: permutation 2 ('pbs', datablock 'c') built in the frame - its shader COMPILED in the frame (2 ms)\n"
                "3/3 Test #3: fake.cold ........................   Passed    0.70 sec\n")
    fake = os.path.join(tmp, "fake-ctest")
    with open(fake, "w") as f:
        f.write("#!/bin/sh\ncat '%s'\n" % stream)
    os.chmod(fake, 0o755)
    os.makedirs(os.path.join(tmp, "tests"))
    with open(os.path.join(tmp, "tests", "test-cold-rows.txt"), "w") as f:
        f.write("fake.cold\t/x\t\tcmd\n")
    with open(os.path.join(tmp, "loadavg"), "w") as f:
        f.write("42.50 40.00 30.00 3/900 12345\n")

    def call(runs, *ctest):
        env = dict(os.environ, HOME=tmp, JAH_RUN_LOG_DIR=runs, JAH_LOADAVG_FILE=os.path.join(tmp, "loadavg"))
        return subprocess.Popen([sys.executable, runlog, "run", "--tier", "spec", "--lane", "tooling", "--build", tmp, "--",
                                 *ctest], cwd=tmp, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    # (1) --test-dir refused by name, nothing recorded
    for form in (["--test-dir", "build-linux"], ["--test-dir=build-linux"]):
        r1 = os.path.join(tmp, "runs-td")
        p = call(r1, fake, *form)
        out = p.communicate()[0]
        want(p.returncode == 2 and "REFUSED" in out and repr(form[0]) in out and "DEV_ERRORS 52" in out and not records(r1),
             "(1) %s is refused by name, exit 2, nothing recorded (rc %d: %s)" % (" ".join(form), p.returncode, out.strip()[:160]))

    # (2) two calls together: two run ids, each with its pid
    r2 = os.path.join(tmp, "runs-two")
    a, b = call(r2, fake), call(r2, fake)
    a.communicate(), b.communicate()
    ids = {r["run"] for r in records(r2)}
    want(len(ids) == 2 and {i.rsplit("-p", 1)[-1] for i in ids} == {str(a.pid), str(b.pid)},
         "(2) two calls started together: two run ids, each ending -p<its pid> (%s)" % sorted(ids))

    # (3) + (4) the price's facts on each record
    rec = {r["suite"]: r for r in records(r2) if r["run"].endswith("-p%d" % a.pid)}
    h, w, c = rec.get("fake.halfcold", {}), rec.get("fake.warm", {}), rec.get("fake.cold", {})
    want(h.get("inframe") == 2 and "coldRow" not in h,
         "(3) a half-cold row: inframe 2 (an Ogre compile line and a HlmsCompute parse printed past the 256-KB cap) (%s)"
         % h.get("inframe"))
    want(w.get("inframe") == 0, "(3) a warm row's PSO-only line is no compile: inframe 0 (%s)" % w.get("inframe"))
    want(c.get("inframe") == 1 and c.get("coldRow") is True,
         "(3) a row declared in tests/test-cold-rows.txt is coldRow (%s, %s)" % (c.get("inframe"), c.get("coldRow")))
    aff = len(os.sched_getaffinity(0))
    want(all(x.get("load") == 42.5 and x.get("ncpu") == aff for x in (h, w, c)),
         "(4) load 42.5 from the load file over each row, ncpu %d (our affinity) (%s)"
         % (aff, [(x.get("load"), x.get("ncpu")) for x in (h, w, c)]))
print("tooling.runlog_facts: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
