#!/usr/bin/env python3
"""source.presented_predicate - ONE PREDICATE OF "PRESENTED" (TEST-STREAMLINE-1 stage 3a, item 2b).

THE SCRIPTABLE HEADLESS HOST's editor view is an offscreen target declared OffscreenContract::PresentedStandIn: it STANDS
IN FOR the presented editor view, so every branch that tells a presented view from an offscreen picture must take the
window's route for it - or a host row guards a picture no user sees. The law, held on the source (the behaviour - the
host row's verdict and law lines equal to the app row's - is card block B, B1; a NULL engine holds no View to ask):

  P1 THE PREDICATE: View::presentsAsWindow() is exactly `!mIdentityOffscreen || mIdentityStandIn` - an undeclared view
     (stand-in false) answers !isOffscreen(), its old branch, UNCHANGED; a declared stand-in answers presented.
  P2 BORN ONE: the stand-in identity is set only at creation - both birth paths (the host's message, initOffscreenContract;
     the engine thread's createOffscreen, declareStandIn before the contract) - and setOffscreenContract refuses a change
     either way.
  P3 EVERY BRANCH READS IT: each named presented-vs-offscreen site reads presentsAsWindow(), and NO isOffscreen() call is
     left in the engine's sources but the target's own fact (OgreVideoReadback.cpp: a window's readback vs a texture's).
  P4 ONLY THE HOST DECLARES IT: PresentedStandIn is named in Studio's sources by the host's factory alone
     (createOffscreenEditorViewport) - no product view changes; the selftest's views never take it, so the six cannot move
     by this.
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
    ("irisgl/engine/src/OgreView.cpp", "const bool presents = presentsAsWindow() || mStereo;", "the gather's contract"),
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
    ov = files.get("irisgl/engine/src/OgreView.cpp", "")
    born = ("void initOffscreenContract(OffscreenContract c) { declareStandIn(c);" in priv
            and "mIdentityStandIn = c == OffscreenContract::PresentedStandIn;" in priv
            and re.search(r"declareStandIn\(contract\);[^\n]*\n\s*mViews\.back\(\)->setOffscreenContract\(contract\);", oe)
            is not None)
    # a WRITE of the identity (its declaration's default member initializer, `bool mIdentityStandIn = false;`, is none)
    sets = sorted(f for f, t in files.items() if f.startswith(("irisgl/", "src/"))
                  and any(re.search(r"\bmIdentityStandIn\s*=[^=]", l) and not re.match(r"\s*bool\b", l)
                          for l in code_only(t).splitlines()))
    refuses = re.search(r"void OgreView::setOffscreenContract\(OffscreenContract c\) \{[^}]*"
                        r"\(c == OffscreenContract::PresentedStandIn\) != mIdentityStandIn", ov, re.S) is not None
    out.append((born and sets == ["irisgl/engine/src/EnginePrivate.h"] and refuses,
                "P2 the stand-in is born one (both birth paths set it, only declareStandIn writes it: %s) and "
                "setOffscreenContract refuses a change either way" % sets))
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
    decl = sorted(f for f, t in files.items() if f.startswith("src/") and "OffscreenContract::PresentedStandIn" in code_only(t)
                  .replace('""', ""))
    fac = files.get("src/viewport/enginesceneviewport.cpp", "")
    out.append((decl == ["src/viewport/enginesceneviewport.cpp"] and
                re.search(r"createOffscreenEditorViewport\([^{]*\{[^}]*setOffscreen\(jahshaka::engine::OffscreenContract::"
                          r"PresentedStandIn\)", fac, re.S) is not None,
                "P4 only the host's factory declares PresentedStandIn in Studio (%s)" % decl))
    return out


def tree_files(root):
    files = {}
    for top in ("irisgl/engine/src", "irisgl/engine/include/jahshaka/engine", "irisgl/mirror", "src"):
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
        ("P2", "irisgl/engine/src/OgreView.cpp", "(c == OffscreenContract::PresentedStandIn) != mIdentityStandIn", "false"),
        ("P3", "irisgl/engine/src/OgreView.cpp", "return presentsAsWindow() || mPip.allowOffscreen;",
         "return !isOffscreen() || mPip.allowOffscreen;"),
        ("P4", "src/viewport/recordingview.cpp", "OffscreenContract::StillPicture", "OffscreenContract::PresentedStandIn"),
        ("P3", "src/modules/vr/vrapi.cpp", "v->presentsAsWindow() && v->scene() == eyes->scene()",
         "!v->isOffscreen() && v->scene() == eyes->scene()"),
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
