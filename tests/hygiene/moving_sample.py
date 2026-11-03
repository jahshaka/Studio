#!/usr/bin/env python3
"""source.moving_sample - EVERY MOVING SAMPLE SITE READS THE ONE RULE (TEST-STREAMLINE-1 3c, URGENT #157).

A stochastic producer's MOVING sample index is a view's frames since its scene's bind or its size change
(irisgl/engine/src/MovingSample.h, detail::MovingSequence) - never the raw per-view counter, which counted every frame the
view ever traced and made a moving picture a function of what the process drew before. (URGENT #157 suspected it for the
self-test's two mover hashes; measured, their carrier was fixture B's editor view SIZE - selftestrunner.cpp pins it - and
this rule stands on its own law.) The law on the source:
  M1 NO RAW FRAME-COUNT SEED is left anywhere in the engine's sources - every subdirectory of irisgl/engine/src (photon/,
     rayquery/, atom/, ...), comments aside - in any form (the 3c audit: the glass escape's `frameNow() & 0xFFFF` hid from
     a pattern that knew only `.frame`, in a file list that did not walk): the process frame (frameNow()), a view's or a
     state's `.frame`, a member counter (mFrame..., m...Frame...) or a bare `frame`, masked to 16 bits (`& 0xFFFF`) or
     wrapped (`% 65536`, `% 0x10000`), or written straight into a float parameter (`float(frameNow())`, `float(x.frame)`);
  M2 each known moving site reads the helper: the reflection's pp.knobs[2], the gather's rp.seq[0] and pp.knobs6[2], the
     area visibility's pp.strata[1], the glass escape's pp.knobs3[0] (its moving draws; at rest its rest sequence);
  M3 the helper restarts on a scene id or a size change and on nothing else (its one `if`), with a unique scene id
     (OgreScene::historyId from reserveNodeId);
  M4 A STORED VIEW COMPARES THE SCENE's ID, NEVER ITS ADDRESS (the 3c audit): no per-view `<view>.scene != scene` in the
     reflection's or the gather's sources - a view outlives the scenes it drew and a freed address comes back - and the
     gather's history reset goes through detail::rebindScene.
Fed a violation first. The rule's behaviour is engine.moving_sample (a pure-function unit test); "a second scene renders
as if it were opened first" is app.engine_selftest_shapes (the fixture half's bytes alone and after the default half).

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
    ("OgreRayQuery.cpp", ": ev.moving.index(ev.frame, scene->historyId(), ev.width, ev.height));",
     "the glass escape's moving area draws (the 3c audit: it sampled by the process frame count)"),
    ("OgreRayQuery.cpp", "pp.knobs3[0] = float(restFrames > 0u ? kGlassRestSequence + std::min(restFrames - 1u, (1u << 20) - 1u)",
     "the glass escape's REST area draws (the rest sequence, keyed on the view's rest frame K)"),
]
FRAME = r"(?:frameNow\(\)|\b\w+\.frame\b|\bm[A-Z]\w*Frame\w*\b|\bmFrame\w*\b|\bframe\b)"
RAW = re.compile(FRAME + r"\s*(?:&\s*0x[fF]{4}u?\b|%\s*(?:65536|0x10000)u?\b)|\bfloat\(\s*(?:frameNow\(\)|\w+\.frame)\s*\)")


def laws(files):
    out = []
    raw = sorted("%s: %s" % (f, m.group(0)) for f, t in files.items() for m in RAW.finditer(code_only(t)))
    out.append((not raw, "M1 no raw frame-count seed left in the engine, any subdirectory, any form (%s)" % raw))
    for f, text, what in SITES:
        out.append((text in files.get(f, ""), "M2 %s reads the helper (%s)" % (what, f)))
    h = code_only(files.get("MovingSample.h", ""))
    rule = re.search(r"if \(!mStarted \|\| sceneId != mScene \|\| width != mW \|\| height != mH \|\| counter < mBase\)", h)
    ifs = len(re.findall(r"\bif \(", h))
    hid = "const NodeId mHistoryId = reserveNodeId();" in files.get("EnginePrivate.h", "")
    out.append((bool(rule) and ifs == 1 and hid, "M3 the helper restarts on a scene id or a size change only (its one if: %d), "
                "the scene id unique (reserveNodeId)" % ifs))
    addr = sorted("%s: %s" % (f, m.group(0)) for f in ("OgreRayQuery.cpp", "OgreScreenProbeGather.cpp")
                  for m in re.finditer(r"\b(?:rv|sv|av|hv|ev|v|o|kv\.second)\.scene\s*!=\s*(?:in\.)?scene\b",
                                       code_only(files.get(f, ""))))
    reset = "if (detail::rebindScene(v.sceneId, in.scene->historyId())) v.age = 0u;" in files.get("OgreScreenProbeGather.cpp", "")
    out.append((not addr and reset, "M4 a stored view compares the scene's history id, never its address (address compares: %s; "
                "the gather's reset through rebindScene: %s)" % (addr, reset)))
    return out


def main(src):
    d = os.path.join(src, "irisgl", "engine", "src")
    files = {}
    for root, _, names in os.walk(d):           # EVERY subdirectory (the 3c audit: photon/, rayquery/, atom/, ...)
        for n in names:
            if n.endswith((".cpp", ".h")):
                p = os.path.join(root, n)
                files[os.path.relpath(p, d)] = open(p, errors="replace").read()
    subdirs = sorted({f.split("/")[0] for f in files if "/" in f})
    check(len(subdirs) >= 2, "the walk reaches the engine's subdirectories (%s)" % subdirs)
    # M1 FED ONCE PER FORM, each in a SUBDIRECTORY's file (the walk's own proof): appended code, never a comment
    sub = next(f for f in sorted(files) if f.startswith("photon/") and f.endswith(".cpp"))
    for form in ("x = frameNow() & 0xFFFFu;", "x = st.frame & 0xFFFF;", "x = mFrameCounter & 0xFFFFu;",
                 "x = mCardFrame % 65536u;", "x = frame % 0x10000u;", "p.k = float(frameNow());", "p.k = float(hv.frame);"):
        m = dict(files)
        m[sub] = m[sub] + "\nvoid jahMutation() { " + form + " }\n"
        red = [w for ok, w in laws(m) if not ok and w.startswith("M1")]
        check(bool(red), "M1 is fed the form `%s` in %s and reds" % (form, sub))
    muts = [("M1", "OgreAreaVis.cpp", "st.moving.index(st.frame, sceneId, w, h)", "(st.frame & 0xFFFFu)"),
            ("M2", "OgreRayQuery.cpp", "rv.moving.index(rv.frame,", "rv.moving_index(rv.frame,"),
            ("M3", "MovingSample.h", " || width != mW || height != mH", ""),
            ("M4", "OgreRayQuery.cpp", "if (rv.sceneId != scene->historyId()) continue;", "if (rv.scene != scene) continue;")]
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
