#!/usr/bin/env python3
"""tooling.evidence_cap - THE EVIDENCE IS WHAT EXPLAINS A RED, AND IT IS CAPPED (TEST-STREAMLINE-1 1f; photon-2's tests/.evidence
held 21 GB over 90 red homes, rtreflect.contact's 461 of 461 MB its shader cache - bytes that explain no red).
On a toy build dir (a CMakeCache.txt beside tests/), no GPU:
  (1) fresh_home.sh's red run moves the home to tests/.evidence/<home> WITHOUT its shader cache - its logs and pictures kept,
      the cache's manifest kept as shadercache-manifest.txt;
  (2) ...unless the row's subject is the cache (JAH_EVIDENCE_CACHE=keep): the cache kept, the home marked .evidence-keeps-cache;
  (3) gate_runlog.prune_evidence sheds the cache of an older unmarked red home (one written before the rule) and keeps a
      marked one's;
  (4) output/<run>/: the newest 40 runs kept; an older run keeps only the files that are some row's NEWEST red log - the
      last word on a still-red row is never removed; an emptied run dir removed.
usage: evidence_cap.py <source dir>
"""
import os
import subprocess
import sys
import tempfile

src = sys.argv[1]
sys.path.insert(0, os.path.join(src, "scripts"))
import gate_runlog  # noqa: E402

FH = os.path.join(src, "tests", "support", "fresh_home.sh")
fails = []


def law(ok, text):
    print(("  ok   " if ok else "  FAIL ") + text)
    if not ok:
        fails.append(text)


def put(path, data="x"):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(data)


CACHE = os.path.join(".local", "share", "Jahshaka", "shadercache")
with tempfile.TemporaryDirectory() as t:
    b = os.path.join(t, "b")
    put(os.path.join(b, "CMakeCache.txt"), "")
    ev = os.path.join(b, "tests", ".evidence")

    def red_run(name, keep):
        home = os.path.join(b, "tests", "x", name)
        cmd = ("mkdir -p %s/%s && echo m > %s/%s/cache-manifest.txt && head -c 65536 /dev/zero > %s/%s/pipeline.cache "
               "&& echo log > %s/run/app.log && exit 1") % ((home, CACHE) * 3 + (home,))
        env = dict(os.environ, HOME=t)
        env.pop("JAH_EVIDENCE_CACHE", None)
        if keep:
            env["JAH_EVIDENCE_CACHE"] = "keep"
        return subprocess.run(["sh", FH, home, "--", "sh", "-c", cmd], env=env, capture_output=True, text=True).returncode

    rc = red_run("home-red", False)
    e = os.path.join(ev, "home-red")
    law(rc == 1 and os.path.isfile(os.path.join(e, "run", "app.log")) and not os.path.exists(os.path.join(e, CACHE))
        and open(os.path.join(e, "shadercache-manifest.txt")).read().strip() == "m",
        "(1) a red home is the evidence WITHOUT its shader cache: its log kept, the cache's manifest kept beside it")
    rc = red_run("home-cache-subject", True)
    e2 = os.path.join(ev, "home-cache-subject")
    law(rc == 1 and os.path.isfile(os.path.join(e2, CACHE, "pipeline.cache")) and os.path.exists(os.path.join(e2, ".evidence-keeps-cache")),
        "(2) JAH_EVIDENCE_CACHE=keep (the cache is the row's subject): the cache kept, the home marked")
    old = os.path.join(ev, "home-old")
    put(os.path.join(old, CACHE, "pipeline.cache"), "z" * 4096)
    put(os.path.join(old, CACHE, "cache-manifest.txt"), "m-old")
    put(os.path.join(old, "run", "app.log"), "log")
    out = os.path.join(ev, "output")
    runs = ["20261101T%06d-abcdef012" % i for i in range(45)]   # oldest first
    for i, r in enumerate(runs):
        put(os.path.join(out, r, "row.common.log"), "red %d" % i)
    put(os.path.join(out, runs[0], "row.once.log"), "the only red of row.once")    # its newest red log: in the oldest run
    # (10) KEEP BY THE REGISTRATION (the stage-1 audit's finding 10): a home of a row registered JAH_EVIDENCE_CACHE=keep keeps its
    # cache though it carries no marker (written before the rule) - red on the base: the marker was the only keep
    subj = os.path.join(ev, "home-subject-old")
    put(os.path.join(subj, CACHE, "pipeline.cache"), "s" * 512)
    tests = [{"name": "shadercache.x", "command": ["sh", FH, "--warm", os.path.join(b, "tests", "x", "home-subject-old"), "--", "x"],
              "properties": [{"name": "ENVIRONMENT", "value": ["JAHSHAKA_VR=0", "JAH_EVIDENCE_CACHE=keep"]}]},
             {"name": "gi.y", "command": ["sh", FH, os.path.join(b, "tests", "x", "home-old"), "--", "y"],
              "properties": [{"name": "ENVIRONMENT", "value": ["HOME=/b/tests/x/home-old"]}]}]
    keep = gate_runlog.evidence_keep_homes(tests)
    law(keep == {"home-subject-old"}, "(5) the registration names the homes whose cache is the subject (%s)" % sorted(keep))
    os.link(os.path.join(old, CACHE, "pipeline.cache"), os.path.join(t, "a-second-link"))   # a byte shared: not freed
    shed, gone, freed = gate_runlog.prune_evidence(b, keep_runs=40, keep_homes=keep)
    law(os.path.isfile(os.path.join(subj, CACHE, "pipeline.cache")), "(5) ...and the cap keeps that home's cache, no marker needed")
    law(freed < 4096, "(6) 'freed' counts only the last links: the old home's 4,096-byte file is linked elsewhere - not freed (%d)" % freed)
    law(not os.path.exists(os.path.join(old, CACHE)) and open(os.path.join(old, "shadercache-manifest.txt")).read() == "m-old"
        and os.path.isfile(os.path.join(old, "run", "app.log")) and os.path.isfile(os.path.join(e2, CACHE, "pipeline.cache")),
        "(3) the cap sheds an older unmarked red home's cache (manifest and log kept) and keeps the marked one's (shed %d)" % shed)
    left = sorted(os.listdir(out))
    law(len(left) == 41 and left[1:] == sorted(runs[5:]) and os.listdir(os.path.join(out, runs[0])) == ["row.once.log"]
        and gone == 4,
        "(4) output: the newest 40 runs kept; an older run keeps only a row's newest red log (row.once); 4 emptied runs "
        "removed (%d runs left, %d removed)" % (len(left), gone))
    again = gate_runlog.prune_evidence(b, keep_runs=40, keep_homes=keep)
    law(again[0] == 0 and again[1] == 0, "...and a second call removes nothing more (%s)" % (again,))
print("tooling.evidence_cap: %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
