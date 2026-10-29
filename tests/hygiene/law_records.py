#!/usr/bin/env python3
"""tooling.law_records - THE RECORD KEEPS ITS ROW's LAW NUMBERS (HARNESS-3 (b)): scripts/gate_runlog.py run over a FAKE
ctest's verbose stream (no GPU, no build, a scratch HOME and run-log dir) records every `LAW <name> <value> <unit>
[<op> <bar>]` line a row printed (tests/support/lawline.h) in the row's record as `laws` - a green row's and a red row's,
one printed past the output cap (256 KB) too; a non-finite value keeps its text; a malformed line is named in
`lawsMalformed`, never a law; a row with no LAW line has no `laws` field; a red's fail line is kept past the cap, and an
engine fixture's `  FAIL (n) <law>` line is one (it never was).
usage: law_records.py <the tree's scripts/gate_runlog.py> (named on the command line: the selector reaches the row from it)"""
import glob
import json
import os
import subprocess
import sys
import tempfile

runlog = os.path.abspath(sys.argv[1])
fails = []
with tempfile.TemporaryDirectory(prefix="law-records-") as tmp:
    stream = os.path.join(tmp, "stream.txt")
    filler = "".join("2: filler line %06d %s\n" % (i, "x" * 60) for i in range(4500))   # ~320 KB: past OUTPUT_CAP
    with open(stream, "w") as f:
        f.write("UpdateCTestConfiguration  from :x\ntest 1\n    Start 1: fake.green\n"
                "1: Test command: /bin/true\n1: Working Directory: /tmp\n1: Test timeout computed to be: 60\n"
                "1: LAW k14.breaking 1.86 % <= 1\n1: an ordinary line\n1: LAW sky.teleport nan %R\n"
                "1: LAW bad 1x codes\n"
                "1/3 Test #1: fake.green .......................   Passed    1.23 sec\n"
                "test 2\n    Start 2: fake.red\n2: Test command: /bin/false\n2: Test timeout computed to be: 60\n"
                + filler +
                "2: LAW rest.trace 4.63 ms <= 3.31\n2: FAIL: the rest frame outweighs a tile frame\n"
                "2/3 Test #2: fake.red .........................***Failed    2.00 sec\n"
                "test 3\n    Start 3: fake.plain\n3: Test command: /bin/true\n3: Test timeout computed to be: 60\n"
                "3: no law here\n3/4 Test #3: fake.plain .......................   Passed    0.10 sec\n"
                "test 4\n    Start 4: fake.fixture\n4: Test command: /bin/false\n4: Test timeout computed to be: 60\n"
                "4:   ok   (1) the control\n4: LAW high.voxels 1.48 %R < 0.0531\n4:   FAIL (3) THE VOXELS: no voxel reads a step\n"
                "4: FAIL (1 failures, 445 frames)\n4/4 Test #4: fake.fixture .....................***Failed    3.00 sec\n")
    fake = os.path.join(tmp, "fake-ctest")
    with open(fake, "w") as f:
        f.write("#!/bin/sh\ncat '%s'\nexit 8\n" % stream)
    os.chmod(fake, 0o755)
    runs = os.path.join(tmp, "runs")
    env = dict(os.environ, HOME=tmp, JAH_RUN_LOG_DIR=runs)
    p = subprocess.run([sys.executable, runlog, "run", "--tier", "spec",
                        "--lane", "tooling", "--build", tmp, "--", fake], cwd=tmp, env=env, capture_output=True, text=True)
    recs = {}
    for fn in glob.glob(os.path.join(runs, "*.jsonl")):
        for line in open(fn):
            r = json.loads(line)
            recs[r["suite"]] = r
    g, r, n = recs.get("fake.green", {}), recs.get("fake.red", {}), recs.get("fake.plain", {})
    x = recs.get("fake.fixture", {})
    def want(cond, what):
        print("  %s %s" % ("ok  " if cond else "FAIL", what))
        if not cond:
            fails.append(what)
    want(len(recs) == 4, "four records written (%d; the run's rc %d)" % (len(recs), p.returncode))
    want(g.get("verdict") == "PASS" and g.get("laws", {}).get("k14.breaking") ==
         {"value": 1.86, "unit": "%", "op": "<=", "bar": 1.0}, "a green row keeps its law: value, unit, op and bar")
    want(g.get("laws", {}).get("sky.teleport") == {"value": None, "unit": "%R", "text": "nan"},
         "a non-finite value keeps its text")
    want("bad" not in g.get("laws", {}) and g.get("lawsMalformed") == ["LAW bad 1x codes"],
         "a malformed LAW line is named, never a law")
    want(r.get("verdict") == "FAIL" and r.get("laws", {}).get("rest.trace", {}).get("value") == 4.63,
         "a red row's law printed past the 256-KB output cap is kept")
    want(r.get("failLine", "").startswith("FAIL: the rest frame"), "the red's fail line still recorded")
    want(x.get("failLine") == "FAIL (3) THE VOXELS: no voxel reads a step" and
         x.get("laws", {}).get("high.voxels", {}).get("bar") == 0.0531,
         "an engine fixture's red: its first '  FAIL (n) <law>' line is the fail line, its law kept")
    want(n.get("verdict") == "PASS" and "laws" not in n and "lawsMalformed" not in n, "a row with no LAW line has no field")
print("tooling.law_records: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
