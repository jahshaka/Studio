#!/usr/bin/env python3
"""source.scene_maps - A MAP KEYED BY A SCENE's ADDRESS DIES WITH THE SCENE (TEST-STREAMLINE-1 3c audit).

The ray tier keeps per-SCENE state in maps keyed by the OgreScene's address (the cards' movers / still / lamps views, the
card gather's, the voxel sides', the shot-view folds). A destroyed scene's address can come back for the next scene, so an
entry left behind is ANOTHER scene's lighting answering for the new one. The cards' and voxel sides' entries were erased
only past RayQueryTier::forgetScene's `if (!saP) return;`: a scene with no structures of its own kept them. The law on the
source (a device-free proof: the tier cannot be made on the CPU; the picture is card block B's):
  E1 every map keyed by a scene pointer declared in the engine (any subdirectory) is one this lint knows, with its erase;
  E2 RayQueryTier::forgetScene - called by OgreScene::destroy - reaches every erase BEFORE its first `return`;
  E3 each forget helper erases its maps (forgetCardMovers the three card views, forgetCardGather, forgetVoxelSides, the
     gather's own forgetScene its fold).
Fed a violation of each first. Run: scene_maps.py <source-dir>
"""
import os
import re
import sys

FAIL = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAIL.append(what)


def code_only(t):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", t, flags=re.S))


def body(text, head):
    i = text.find(head)
    if i < 0:
        return None
    j = text.index("{", i)
    d = 0
    for k in range(j, len(text)):
        d += {"{": 1, "}": -1}.get(text[k], 0)
        if d == 0:
            return text[j:k + 1]
    return None


# map -> (the call in RayQueryTier::forgetScene that erases it, the helper whose body must erase it, the erase text)
KNOWN = {
    "mForgottenReflects": ("mForgottenReflects.erase(scene);", None, None),
    "mCardMovers": ("forgetCardMovers(scene);", "void RayQueryTier::forgetCardMovers(OgreScene *scene) {", "&mCardMovers"),
    "mCardStill": ("forgetCardMovers(scene);", "void RayQueryTier::forgetCardMovers(OgreScene *scene) {", "&mCardStill"),
    "mCardLamps": ("forgetCardMovers(scene);", "void RayQueryTier::forgetCardMovers(OgreScene *scene) {", "&mCardLamps"),
    "mCardGather": ("forgetCardGather(scene);", "void RayQueryTier::forgetCardGather(OgreScene *scene) {", "mCardGather.erase(it);"),
    "mVoxelSides": ("forgetVoxelSides(scene);", "void RayQueryTier::forgetVoxelSides(OgreScene *scene) {", "mVoxelSides.erase(it);"),
    "mForgotten": ("if (mGather) mGather->forgetScene(scene);",
                   "void ScreenProbeGather::forgetScene(const detail::OgreScene *scene) {", "mForgotten.erase(scene);"),
}
DECL = re.compile(r"std::(?:unordered_)?map<\s*const\s+(?:detail::)?OgreScene\s*\*\s*,[^;]*>\s+(m\w+)\s*;")


def laws(files):
    out = []
    decl = sorted({m.group(1) for t in files.values() for m in DECL.finditer(code_only(t))})
    out.append((set(decl) <= set(KNOWN) and decl, "E1 every scene-pointer-keyed map is known with its erase (%s; unknown: %s)"
                % (decl, sorted(set(decl) - set(KNOWN)))))
    rq = code_only(files.get("OgreRayQuery.cpp", "")) + code_only(files.get("OgreScreenProbeGather.cpp", ""))
    fs = body(rq, "void RayQueryTier::forgetScene(OgreScene *scene) {") or ""
    first_ret = fs.find("return")
    head = fs if first_ret < 0 else fs[:first_ret]
    late = sorted({c for c, _, _ in KNOWN.values() if c not in head})
    out.append((fs and not late and "mRayTier->forgetScene(this)" in rq,
                "E2 forgetScene (called by OgreScene::destroy) erases every map before its first return (late or missing: %s)" % late))
    bad = []
    for name, (_, helper, text) in KNOWN.items():
        if helper:
            b = body(rq, helper) or ""
            if text not in b or (text.startswith("&") and "erase(it)" not in b):
                bad.append(name)
    out.append((not bad, "E3 each forget helper erases its maps (not: %s)" % bad))
    return out


def main(src):
    d = os.path.join(src, "irisgl", "engine", "src")
    files = {}
    for root, _, names in os.walk(d):
        for n in names:
            if n.endswith((".cpp", ".h")):
                p = os.path.join(root, n)
                files[os.path.relpath(p, d)] = open(p, errors="replace").read()
    rqf = "OgreRayQuery.cpp"
    muts = [
        ("E1", "ScreenProbeGather.h", "std::unordered_map<const detail::OgreScene *, ForgottenView> mForgotten;",
         "std::unordered_map<const detail::OgreScene *, ForgottenView> mForgotten;\n    std::unordered_map<const OgreScene *, int> mNewPerScene;"),
        ("E2", rqf, "    forgetVoxelSides(scene);\n    SceneAs *saP = asOf(scene);\n    if (!saP) return;",
         "    SceneAs *saP = asOf(scene);\n    if (!saP) return;\n    forgetVoxelSides(scene);"),
        ("E3", rqf, "    mCardGather.erase(it);", "    (void)it;"),
    ]
    for law, f, a, b in muts:
        m = dict(files)
        m[f] = m.get(f, "").replace(a, b, 1)
        red = [w for ok, w in laws(m) if not ok and w.startswith(law)]
        check(bool(red) and a in files.get(f, ""), "the lint is fed a violation of %s (%s) and reds" % (law, f))
    for ok, what in laws(files):
        check(ok, what)
    print("source.scene_maps: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "all ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
