#!/usr/bin/env python3
"""source.scene_time_one_meaning - A SCENE's TIME HAS ONE MEANING AND ONE SOURCE (ONE-FRAME-1 stage 1, V2 plan 2.1).

A scene's time is its DOCUMENT's steps: its mirror stamps the document clock's lifetime serial on the scene's part, the
engine advances the scene by the steps the serial moved, once (FrameInput.h FrameInputScene::stepSerial). Ogre's frame clock
(ControllerManager's one process-wide frame delay) is set every frame to the scene time of the frame's first presented
view, for a stock Ogre controller only: NOTHING OF OURS READS IT FOR SCENE TIME, because another scene's view may have
set it. The law, held on the source (the behaviour is frame.clock_once / frame.two_scenes / frame.particle_scale and
the card's frame.meter_time):

  T1 NO READER OF OGRE's FRAME CLOCK in our C++ (irisgl/engine/src, irisgl/mirror, src/): no getFrameDelay,
     getFrameTimeSource, getElapsedTime, getTimeFactor. Its one WRITER is OgreEngine.cpp (setFrameDelay: the boot's 0
     and applyFrameDelta).
  T2 NO MEDIA READS OGRE's TIME AUTO-PARAMS (irisgl/engine/media: *.material, *.json, *.compositor, *.program): no
     frame_time, time, time_0_x, time_0_1, time_0_2pi, costime*, sintime*, tantime*, fps. The converted sites: the HDR
     meter's timeSinceLast (JahshakaCompute.material.json) and the OldMovie look's lookTime (JahLooks.material), both set
     per view by the engine (OgreChain.cpp).
  T2b ...NOR THE FORK MEDIA WE STAGE (irisgl/engine/CMakeLists.txt: Hlms/{Common,Pbs,Unlit}, 2.0/scripts/materials/{Common,
     HDR,Tutorial_SSAO,Tutorial_SMAA}, Compute/{Tools,Algorithms/IBL}), but for a program our graph never draws, each
     named here WITH its proof: no non-comment line of our C++ names it. Today one: HDR.material's
     HDR/DownScale03_SumLumEnd_ps (frame_time), replaced by the meter's resolve job since EXPOSURE-2.
  T3 THE METER's TIME IS ITS VIEWER's (the lead's ruling reversing R6): OgreChain.cpp hands the meter resolve's
     timeSinceLast the view's OgreEngine::meterSecondsFor (a picture: no history; a live view: presented time), and
     the job declares it a manual float.
  T4 NO HOST-WRITTEN DELTA (forward-only): FrameInput::delta / frameDelta / kDefaultFrameDelta / fixedFrameDelta appear
     nowhere in src/, irisgl/ (the vendored trees excepted) or tests/.
  T5 THE HARNESS DOOR IS THE TESTS' ALONE: harnessStep has no caller in src/ or irisgl/mirror (the engine defines it; the
     harness, tests/support/enginetesthelpers.h, calls it).
Fed a violation first: each rule is checked against a mutated copy that must fail (a lint that cannot fail guards nothing).

Run: scene_time_one_meaning.py <source-dir>
"""
import os
import re
import sys

FAIL = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAIL.append(what)


def strip_cpp(text):
    """Comments and string literals blanked (a comment may name what it forbids)."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)


def strip_media(text):
    return re.sub(r"//[^\n]*", "", text)


CLOCK_READ = re.compile(r"\b(getFrameDelay|getFrameTimeSource|getElapsedTime|getTimeFactor)\s*\(")
CLOCK_WRITE = re.compile(r"\bsetFrameDelay\s*\(")
TIME_AUTOS = {"frame_time", "time", "time_0_x", "time_0_1", "time_0_2pi", "time_0_x_packed", "time_0_1_packed",
              "time_0_2pi_packed", "costime_0_x", "sintime_0_x", "tantime_0_x", "costime_0_1", "sintime_0_1",
              "tantime_0_1", "costime_0_2pi", "sintime_0_2pi", "tantime_0_2pi", "fps"}
V1_AUTO = re.compile(r"\bparam_named_auto\s+\S+\s+(\S+)")              # a material's: param_named_auto <name> <auto> ...
JOB_AUTO = re.compile(r'\[\s*"[^"]+"\s*,\s*"([^"]+)"')                  # a compute job's: ["name", "auto", n]
# (the record's field and its helpers - never a word: test_rt_reflect.cpp's frameDelta() is an image difference)
DELTA = re.compile(r"FrameInput::delta\s*\(|(?:\.|->)frameDelta\b|\bkDefaultFrameDelta\b|\bfixedFrameDelta\s*\(")
VENDORED = ("irisgl/thirdparty/", "thirdparty/")


def cpp_files(root, dirs):
    for d in dirs:
        for base, _, files in os.walk(os.path.join(root, d)):
            rel = os.path.relpath(base, root) + "/"
            if rel.startswith(VENDORED) or "/thirdparty/" in rel or "/build" in rel:
                continue
            for f in files:
                if f.endswith((".cpp", ".h", ".hpp", ".cc")):
                    yield os.path.join(base, f)


def t1(texts):
    """texts: {relpath: stripped text} -> (readers, foreign writers)."""
    readers = [p for p, t in texts.items() if CLOCK_READ.search(t)]
    writers = [p for p, t in texts.items() if CLOCK_WRITE.search(t) and not p.endswith("irisgl/engine/src/OgreEngine.cpp")]
    return readers, writers


def t2(media):
    bad = []
    for p, t in media.items():
        for i, line in enumerate(strip_media(t).split("\n")):
            for m in list(V1_AUTO.finditer(line)) + list(JOB_AUTO.finditer(line)):
                if m.group(1) in TIME_AUTOS:
                    bad.append(f"{p}:{i + 1}: {line.strip()}")
    return bad


FORK_STAGED = ["Hlms/Common", "Hlms/Pbs", "Hlms/Unlit", "2.0/scripts/materials/Common", "2.0/scripts/materials/HDR",
               "2.0/scripts/materials/Tutorial_SSAO", "2.0/scripts/materials/Tutorial_SMAA", "Compute/Tools",
               "Compute/Algorithms/IBL"]
# (the staged file, the program or material the time auto-param sits in) -> why it is never drawn
FORK_UNUSED = {("HDR.material", "HDR/DownScale03_SumLumEnd_ps"): "replaced by Jahshaka/HdrMeterResolve (EXPOSURE-2)"}
BLOCK = re.compile(r"^\s*(?:fragment_program|vertex_program|material|compute_job|abstract\s+\w+)\s+(\S+)")


def t2b(media):
    """media: {relpath: text} of staged fork files -> [(file, block, line)] time auto-params outside FORK_UNUSED."""
    bad = []
    for p, t in media.items():
        block = ""
        for i, line in enumerate(strip_media(t).split("\n")):
            b = BLOCK.match(line)
            if b:
                block = b.group(1)
            for m in list(V1_AUTO.finditer(line)) + list(JOB_AUTO.finditer(line)):
                if m.group(1) in TIME_AUTOS and (os.path.basename(p), block) not in FORK_UNUSED:
                    bad.append(f"{p}:{i + 1} ({block}): {line.strip()}")
    return bad


def t3(chain, json):
    set_ok = ('meterSeconds = meterTime;' in chain
              and 'setMeterParam(kMeterResolveJob, "timeSinceLast", meterSeconds);' in chain
              and chain.count('mView->mEngine->meterSecondsFor(*mView)') == 2)
    decl_ok = re.search(r'\[\s*"timeSinceLast"\s*,\s*\[\s*0\.0\s*\]\s*,\s*"float"\s*\]', json) is not None
    return set_ok and decl_ok


def t4(texts):
    return [p for p, t in texts.items() if DELTA.search(t)]


def t5(texts):
    return [p for p, t in texts.items()
            if re.search(r"\bharnessStep\s*\(", t) and (p.startswith("src/") or p.startswith("irisgl/mirror/"))]


def main(root):
    rel = lambda p: os.path.relpath(p, root)
    ours = {rel(p): strip_cpp(open(p, encoding="utf-8", errors="replace").read())
            for p in cpp_files(root, ["irisgl/engine/src", "irisgl/engine/include", "irisgl/mirror", "src"])}
    everywhere = dict(ours)
    everywhere.update({rel(p): strip_cpp(open(p, encoding="utf-8", errors="replace").read())
                       for p in cpp_files(root, ["irisgl/document", "irisgl/core", "irisgl/import", "tests"])})
    media = {}
    for base, _, files in os.walk(os.path.join(root, "irisgl/engine/media")):
        for f in files:
            if f.endswith((".material", ".json", ".compositor", ".program")):
                p = os.path.join(base, f)
                media[rel(p)] = open(p, encoding="utf-8", errors="replace").read()
    chain = open(os.path.join(root, "irisgl/engine/src/OgreChain.cpp"), encoding="utf-8").read()
    json = media.get("irisgl/engine/media/Hlms/Jahshaka/JahshakaCompute.material.json", "")

    # ---- fed a violation first ----
    r, w = t1({"src/x.cpp": "float d = ControllerManager::getSingleton().getFrameDelay();",
               "irisgl/engine/src/OgreSky.cpp": "cm.setFrameDelay(0.0f);"})
    check(bool(r) and bool(w), "T1 flags a reader of Ogre's frame clock and a writer outside OgreEngine.cpp (mutant)")
    check(bool(t2({"m.material": "    param_named_auto lookTime time_0_x 3600\n"}))
          and bool(t2({"j.json": '    ["timeSinceLast", "frame_time", 1]\n'})),
          "T2 flags a time auto-param in a material and in a compute job (mutants)")
    check(not t2({"m.material": "    // param_named_auto lookTime time_0_x 3600 (the old line)\n    param_named lookTime float 0\n"}),
          "T2 ignores a comment naming the old auto-param")
    check(not t3(chain.replace("meterSeconds = meterTime;", "meterSeconds = float(scene->frameSceneSeconds());"), json),
          "T3 flags a meter time that is not the viewer's (mutant)")
    check(bool(t4({"tests/x.cpp": "e->submitFrame(FrameInput::delta(1.0f / 60.0f));"})), "T4 flags a host delta (mutant)")
    check(bool(t5({"src/viewport/x.cpp": "echo->harnessStep(1);"})), "T5 flags a src/ caller of the harness door (mutant)")

    check(bool(t2b({"x/Sky.material": "fragment_program Sky_ps glsl\n{\n  param_named_auto t time_0_x 1\n}\n"})) and
          not t2b({"HDR/HDR.material": "fragment_program HDR/DownScale03_SumLumEnd_ps unified\n{\n"
                                       "  param_named_auto timeSinceLast frame_time 1\n}\n"}) and
          bool([1 for t in ['m->getMaterial("HDR/DownScale03_SumLumEnd");'] if "HDR/DownScale03_SumLumEnd" in
                re.sub(r"//[^\n]*", "", t)]),
          "T2b flags a staged fork program on a time auto-param, never a named unused one (mutants)")

    # ---- the tree ----
    r, w = t1(ours)
    check(not r, f"T1 no reader of Ogre's frame clock in our C++ ({len(ours)} files; readers: {r})")
    check(not w, f"T1 its one writer is OgreEngine.cpp (others: {w})")
    check(any(CLOCK_WRITE.search(t) for p, t in ours.items() if p.endswith("irisgl/engine/src/OgreEngine.cpp")),
          "T1 ...and OgreEngine.cpp does write it (the frame's delay is set)")
    bad = t2(media)
    check(not bad, f"T2 no media reads Ogre's time auto-params ({len(media)} files; {bad[:4]})")
    fork_root = os.path.join(root, "irisgl/thirdparty/ogre-next/Samples/Media")
    fork_media = {}
    for d in FORK_STAGED:
        for base, _, files in os.walk(os.path.join(fork_root, d)):
            for f in files:
                if f.endswith((".material", ".json", ".compositor", ".program")):
                    p = os.path.join(base, f)
                    fork_media[rel(p)] = open(p, encoding="utf-8", errors="replace").read()
    check(len(fork_media) > 0, f"T2b the staged fork media is found ({len(fork_media)} files)")
    bad = t2b(fork_media)
    check(not bad, f"T2b no staged fork program our graph draws reads Ogre's time auto-params ({bad[:4]})")
    # (comments stripped, STRINGS KEPT: a material is named in a string; our own media's scripts may name it too)
    named = {rel(p): re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", " ", open(p, encoding="utf-8", errors="replace").read(),
                                                  flags=re.S))
             for p in cpp_files(root, ["irisgl/engine/src", "irisgl/mirror", "src"])}
    named.update({p: strip_media(t) for p, t in media.items()})
    for (_, prog), why in FORK_UNUSED.items():
        name = prog[:-3] if prog.endswith("_ps") else prog   # the material's own name, as our code would name it
        users = [p for p, t in named.items() if name in t]
        check(not users, f"T2b ...{prog} is never drawn ({why}): no non-comment line of our C++ names it ({users})")
    check(t3(chain, json), "T3 the meter's timeSinceLast is its viewer's time (meterSecondsFor), a manual float")
    d = t4(everywhere)
    check(not d, f"T4 no host-written delta anywhere ({len(everywhere)} files; {d[:6]})")
    h = t5(everywhere)
    check(not h, f"T5 the harness door has no src/ or mirror caller ({h})")
    print("source.scene_time_one_meaning: " + ("FAILED (%d)" % len(FAIL) if FAIL else "all checks passed"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else os.getcwd()))
