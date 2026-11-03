#!/usr/bin/env python3
"""source.moving_sample - EVERY MOVING SAMPLE SITE READS THE ONE RULE (TEST-STREAMLINE-1 3c, URGENT #157).

A stochastic producer's MOVING sample index is a view's frames since its scene's bind or its size change
(irisgl/engine/src/MovingSample.h, detail::MovingSequence) - never the raw per-view counter, which counted every frame the
view ever traced and made a moving picture a function of what the process drew before (the self-test's mover:
2d88c4ee after the fixture half alone, 122cb3a8 after the default poses). The law on the source:
  M1 no raw `<counter>.frame & 0xFFFF` sample index is left in the engine's sources (comments aside);
  M2 each known moving site reads the helper: the reflection's pp.knobs[2], the gather's rp.seq[0] and pp.knobs6[2], the
     area visibility's pp.strata[1];
  M3 the helper restarts on a scene id or a size change and on nothing else (its one `if`), with a unique scene id
     (OgreScene::historyId from reserveNodeId).
Fed a violation first. The rule's behaviour is engine.moving_sample (a pure-function unit test); the picture's is card
block B (the selftest's two shapes print the same mover hash).

Run: moving_sample.py <source-dir>
"""
import os
import re
import sys

FAIL = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAIL.append(what)


def code_only(text):
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


SITES = [
    ("OgreRayQuery.cpp", "pp.knobs[2] = float(rv.moving.index(rv.frame,", "the reflection's moving sample"),
    ("OgreScreenProbeGather.cpp", "rp.seq[0] = float(v.moving.index(v.frame,", "the gather's moving sample"),
    ("OgreScreenProbeGather.cpp", "pp.knobs6[2] = float(v.moving.index(v.frame,", "the gather's history rounding"),
    ("OgreAreaVis.cpp", "st.moving.index(st.frame, sceneId, w, h)", "the area visibility's moving stratum"),
]
RAW = re.compile(r"\b\w+\.frame\s*&\s*0x[fF]{4}u?\b")


def laws(files):
    out = []
    raw = sorted("%s: %s" % (f, m.group(0)) for f, t in files.items() for m in RAW.finditer(code_only(t)))
    out.append((not raw, "M1 no raw `.frame & 0xFFFF` moving index left in the engine (%s)" % raw))
    for f, text, what in SITES:
        out.append((text in files.get(f, ""), "M2 %s reads the helper (%s)" % (what, f)))
    h = code_only(files.get("MovingSample.h", ""))
    rule = re.search(r"if \(!mStarted \|\| sceneId != mScene \|\| width != mW \|\| height != mH \|\| counter < mBase\)", h)
    ifs = len(re.findall(r"\bif \(", h))
    hid = "const NodeId mHistoryId = reserveNodeId();" in files.get("EnginePrivate.h", "")
    out.append((bool(rule) and ifs == 1 and hid, "M3 the helper restarts on a scene id or a size change only (its one if: %d), "
                "the scene id unique (reserveNodeId)" % ifs))
    return out


def main(src):
    d = os.path.join(src, "irisgl", "engine", "src")
    files = {}
    for n in os.listdir(d):
        if n.endswith((".cpp", ".h")):
            files[n] = open(os.path.join(d, n), errors="replace").read()
    muts = [("M1", "OgreAreaVis.cpp", "st.moving.index(st.frame, sceneId, w, h)", "(st.frame & 0xFFFFu)"),
            ("M2", "OgreRayQuery.cpp", "rv.moving.index(rv.frame,", "rv.moving_index(rv.frame,"),
            ("M3", "MovingSample.h", " || width != mW || height != mH", "")]
    for law, f, a, b in muts:
        m = dict(files)
        m[f] = m.get(f, "").replace(a, b, 1)
        red = [w for ok, w in laws(m) if not ok and w.startswith(law)]
        check(bool(red) and a in files.get(f, ""), "the lint is fed a violation of %s (%s) and reds" % (law, f))
    for ok, what in laws(files):
        check(ok, what)
    print("source.moving_sample: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "all ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
