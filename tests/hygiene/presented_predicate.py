#!/usr/bin/env python3
"""source.presented_predicate - ONE PREDICATE OF "PRESENTED" (TEST-STREAMLINE-1 stage 3a, item 2b).

THE SCRIPTABLE HEADLESS HOST's editor view is an offscreen target born a PRESENTED STAND-IN
(Engine::createPresentedStandInView): it STANDS IN FOR the presented editor view, so every branch that tells a presented
view from an offscreen picture must take the window's route for it - or a host row guards a picture no user sees. The
law, held on the source (the behaviour - the host row's verdict and law lines equal to the app row's - is card block B,
B1; a NULL engine holds no View to ask):

  P1 THE PREDICATE: View::presentsAsWindow() is exactly `!mIdentityOffscreen || mIdentityStandIn` - a view not born a
     stand-in answers !isOffscreen(), its old branch, UNCHANGED; a stand-in answers presented.
  P2 BORN ONE, BY ITS ONE FACTORY (ROUTE-ROWS-1 stage 0): createPresentedStandInView is createOffscreenTarget(..., true)
     and createOffscreenView is (..., false); both birth paths (the host's message, the engine thread's createOffscreen)
     declare the identity only under `standIn`, before the view's first frame; declareStandIn is the identity's only
     writer - and no setter exists to change it later (P5).
  P3 EVERY BRANCH READS IT: each named presented-vs-offscreen site reads presentsAsWindow(), and NO isOffscreen() call is
     left in the engine's sources but the target's own fact (OgreVideoReadback.cpp: a window's readback vs a texture's).
  P4 ONLY THE HOST MAKES ONE: in Studio's sources createPresentedStandInView is named by the view widget alone (its
     declared offscreen path) and setPresentedStandIn is called by the host's factory alone (createOffscreenEditorViewport)
     - no product view is one; the selftest's views never are, so the six cannot move by this.
  P5 ONE PRODUCER, NO CONTRACT (ROUTE-ROWS-1 stage 0): no OffscreenContract, setOffscreenContract, initOffscreenContract,
     setFallbackContract or offscreenContract is left in the code of irisgl (engine, mirror, document), src or tests - the
     Live/StillPicture contract that pinned a live offscreen view off the gather (a second diffuse producer of one world)
     is deleted, and nothing may bring a view-kind input back into the chain's GI choice (detail::chainGiRoutes).
Fed a violation first: each rule is checked against a mutated copy that must fail (a lint that cannot fail guards nothing).

Run: presented_predicate.py <source-dir>
"""
import os
import re
import sys

FAIL = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAIL.append(what)


# (file, the text that must stand at the site, what the site is)
SITES = [
    ("irisgl/engine/src/OgreView.cpp", "d.lodHysteresis  = (presentsAsWindow() ||", "the LOD switch band"),
    ("irisgl/engine/src/OgreView.cpp", "if (!presentsAsWindow() && !mPostFx.allowOffscreen) {", "the post chain"),
    ("irisgl/engine/src/OgreView.cpp", "return presentsAsWindow() || mOverlay.allowOffscreen;", "the overlay"),
    ("irisgl/engine/src/OgreView.cpp", "return presentsAsWindow() || mPip.allowOffscreen;", "the PiP inset"),
    ("irisgl/engine/src/OgrePhotonView.cpp", "mScene->notePhotonView(this, presentsAsWindow() || stereo(), shape);",
     "the photon view's present"),
    ("irisgl/engine/src/OgreEngine.cpp", "if (pass == 0 && !v->presentsAsWindow()) continue;", "the GI driver's on-screen pass"),
    ("irisgl/engine/src/OgreEngine.cpp", "if (v->isEnabled() && v->presentsAsWindow()) { onscreen = true; break; }",
     "the frame monitor's on-screen frame"),
    ("irisgl/engine/src/OgreRayQuery.cpp", "mReflectListener->mFoldsOnForget = !presentsAsWindow();", "the reflection's fold"),
    ("irisgl/engine/src/OgreFrameMonitor.cpp", "if (v->isEnabled() && v->presentsAsWindow() && v->ogreScene())",
     "the monitor snapshot's scene"),
    ("irisgl/mirror/scenemirror.cpp", "if (view->presentsAsWindow())\n        part.sampleCount", "the mirror's sample count"),
    ("src/bridge/offscreenrenderscope.h", "if (!v || !v->presentsAsWindow() || !v->pushedEnabled()) continue;",
     "the offscreen render's suspension of presented views"),
    ("src/viewport/enginesceneviewport.cpp", "if (!view() || !view()->presentsAsWindow() || !mEngine) return;",
     "the editor's covered frames"),
    ("src/viewport/enginesceneviewport.cpp", "if (!view() || !view()->presentsAsWindow()) return;", "the editor's background frame"),
    ("src/viewport/enginesceneviewport.cpp", "if (!mEngine || !view() || !view()->presentsAsWindow()) return FramePace::Complete;",
     "the editor's frame pace"),
    ("src/viewport/enginesceneviewport.cpp", 'if (!view() || !view()->presentsAsWindow()) return QStringLiteral("offscreen");',
     "the editor's presentation state (editor.viewportState().state: the route a row gates on)"),
    ("src/modules/vr/vrapi.cpp", "if (v && v->presentsAsWindow() && v->scene() == eyes->scene()) return v;",
     "VR's desktop-view lookup"),
    ("src/modules/vr/editorvr.cpp", "if (v && v->presentsAsWindow() && v->scene() == scene) {", "the VR mirror's desktop view"),
]
# STUDIO's isOffscreen() READS THAT ARE THE TARGET's FACT, NEVER THE ROUTE (the 3a audit's finding 1): each named with why;
# any other isOffscreen() call in src/ is a route read that must be presentsAsWindow()
STUDIO_TARGET_FACTS = {
    "src/viewport/engineviewwidget.cpp": "the widget's own mechanics: Qt's updates muted only over a window it presents into, "
                                         "a WinIdChange rebuild only for a native-window view",
    "src/viewport/enginesceneviewport.h": "IEditorViewport::isOffscreen() - the target (a texture), the verb's `offscreen` field",
    "src/scripting/modules/editorapi.cpp": "editor.viewportState().offscreen - the target's fact as its doc says; no law reads it",
    "src/app/cli/selftestrunner.cpp": "the selftest's own check that its on-screen window backend took (a target fact)",
}
TARGET_FACT = {"irisgl/engine/src/OgreVideoReadback.cpp"}   # isOffscreen() as the TARGET's fact (window vs texture)


# THE DELETED CONTRACT's names (P5): the type, its setters and its frame field
CONTRACT = re.compile(r"\b(OffscreenContract|setOffscreenContract|initOffscreenContract|setFallbackContract|offscreenContract)\b")


def code_only(text):
    """The text with // comments and string literals blanked (a site in a comment is no site)."""
    out = re.sub(r'"(?:\\.|[^"\\])*"', '""', text)
    return re.sub(r"//[^\n]*", "", out)


def read(root, rel):
    try:
        return open(os.path.join(root, rel), errors="replace").read()
    except OSError:
        return ""


def laws(files):
    """[(ok, what)] of the four laws over `files` ({rel: text})."""
    out = []
    eng = files.get("irisgl/engine/include/jahshaka/engine/Engine.h", "")
    out.append((re.search(r"bool presentsAsWindow\(\) const \{ return !mIdentityOffscreen \|\| mIdentityStandIn; \}", eng)
                is not None, "P1 View::presentsAsWindow() is `!mIdentityOffscreen || mIdentityStandIn` (an undeclared view: "
                             "!isOffscreen(), unchanged)"))
    priv = files.get("irisgl/engine/src/EnginePrivate.h", "")
    oe = files.get("irisgl/engine/src/OgreEngine.cpp", "")
    # the two public factories, one body: the stand-in's says true, the picture's false
    fac = (re.search(r"View \*OgreEngine::createOffscreenView\([^)]*\) \{\s*return createOffscreenTarget\([^;]*, false\);", oe)
           is not None and
           re.search(r"View \*OgreEngine::createPresentedStandInView\([^)]*\) \{\s*return createOffscreenTarget\([^;]*, true\);",
                     oe) is not None)
    # both birth paths declare only under `standIn` (the host's message: raw; the engine thread's: mViews.back())
    calls = re.findall(r"[^\n]*declareStandIn\(\)[^\n]*", code_only(oe))
    births = (len(calls) == 2 and all(re.match(r"\s*if \(standIn\) (raw|mViews\.back\(\))->declareStandIn\(\);", c)
                                      for c in calls))
    writer = "void declareStandIn() { mIdentityStandIn = true; }" in priv
    # a WRITE of the identity (its declaration's default member initializer, `bool mIdentityStandIn = false;`, is none)
    sets = sorted(f for f, t in files.items() if f.startswith(("irisgl/", "src/"))
                  and any(re.search(r"\bmIdentityStandIn\s*=[^=]", l) and not re.match(r"\s*bool\b", l)
                          for l in code_only(t).splitlines()))
    out.append((fac and births and writer and sets == ["irisgl/engine/src/EnginePrivate.h"],
                "P2 the stand-in is born one, by its one factory (the factories %s, both birth paths under `standIn` %s, "
                "declareStandIn the only writer: %s)" % ("ok" if fac else "WRONG", "ok" if births else calls, sets)))
    for f, text, what in SITES:
        out.append((text in files.get(f, ""), "P3 %s reads presentsAsWindow() (%s)" % (what, f)))
    left = sorted(f for f, t in files.items() if f.startswith("irisgl/engine/src/") and f not in TARGET_FACT
                  and re.search(r"\bisOffscreen\(\)", code_only(t)))
    out.append((not left, "P3 no isOffscreen() call left in the engine's sources but the target's fact %s (left: %s)"
                % (sorted(TARGET_FACT), left)))
    sleft = sorted(f for f, t in files.items() if f.startswith("src/") and f not in STUDIO_TARGET_FACTS
                   and re.search(r"\bisOffscreen\(\)", re.sub(r"bool isOffscreen\(\) const", "", code_only(t))))
    out.append((not sleft, "P3 no isOffscreen() route read left in Studio's sources but the named target facts %s (left: %s)"
                % (sorted(STUDIO_TARGET_FACTS), sleft)))
    named = sorted(f for f, t in files.items() if f.startswith("src/") and "createPresentedStandInView(" in code_only(t))
    callers = sorted(f for f, t in files.items() if f.startswith("src/")
                     and re.search(r"\bsetPresentedStandIn\(\)\s*;", code_only(t)))
    host = files.get("src/viewport/enginesceneviewport.cpp", "")
    out.append((named == ["src/viewport/engineviewwidget.cpp"] and callers == ["src/viewport/enginesceneviewport.cpp"] and
                re.search(r"createOffscreenEditorViewport\([^{]*\{[^}]*setPresentedStandIn\(\);", host, re.S) is not None,
                "P4 only the host's factory makes a stand-in in Studio (createPresentedStandInView named by %s; "
                "setPresentedStandIn called by %s)" % (named, callers)))
    left = sorted("%s:%d" % (f, i + 1) for f, t in files.items() if f.startswith(("irisgl/", "src/", "tests/"))
                  for i, l in enumerate(code_only(t).splitlines()) if CONTRACT.search(l))
    out.append((not left, "P5 no offscreen contract left in the code of irisgl, src or tests (left: %s)" % left[:12]))
    return out


def tree_files(root):
    files = {}
    for top in ("irisgl/engine/src", "irisgl/engine/include/jahshaka/engine", "irisgl/mirror", "irisgl/document", "src",
                "tests"):
        for d, _, names in os.walk(os.path.join(root, top)):
            for n in names:
                if n.endswith((".cpp", ".h")):
                    p = os.path.join(d, n)
                    files[os.path.relpath(p, root)] = open(p, errors="replace").read()
    return files


def main(root):
    files = tree_files(root)
    # FED A VIOLATION FIRST: each mutation must turn its law red
    muts = [
        ("P1", "irisgl/engine/include/jahshaka/engine/Engine.h", "!mIdentityOffscreen || mIdentityStandIn",
         "!mIdentityOffscreen"),
        ("P2", "irisgl/engine/src/OgreEngine.cpp", "if (standIn) raw->declareStandIn();", "raw->declareStandIn();"),
        ("P2", "irisgl/engine/src/OgreEngine.cpp", "return createOffscreenTarget(name, width, height, background, false);",
         "return createOffscreenTarget(name, width, height, background, true);"),
        ("P3", "irisgl/engine/src/OgreView.cpp", "return presentsAsWindow() || mPip.allowOffscreen;",
         "return !isOffscreen() || mPip.allowOffscreen;"),
        ("P4", "src/viewport/recordingview.cpp", "engine.createOffscreenView(", "engine.createPresentedStandInView("),
        ("P3", "src/modules/vr/vrapi.cpp", "v->presentsAsWindow() && v->scene() == eyes->scene()",
         "!v->isOffscreen() && v->scene() == eyes->scene()"),
        ("P5", "tests/gi/test_gi_screen_bind.cpp", "View *v = e->createOffscreenView(name, w, h, Colour(0, 0, 0));",
         "View *v = e->createOffscreenView(name, w, h, Colour(0, 0, 0), OffscreenContract::Live);"),
    ]
    for law, f, a, b in muts:
        m = dict(files)
        m[f] = m.get(f, "").replace(a, b, 1)
        red = [w for ok, w in laws(m) if not ok and w.startswith(law)]
        check(bool(red) and a in files.get(f, ""), "the lint is fed a violation of %s (%s) and reds" % (law, f))
    for ok, what in laws(files):
        check(ok, what)
    print("source.presented_predicate: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "all ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
