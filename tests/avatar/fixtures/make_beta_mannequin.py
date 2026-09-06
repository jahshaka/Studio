#!/usr/bin/env python3
"""Generates beta_mannequin.dae — the ANIMATION-FREE humanoid avatar fixture.

Source: scenes/"Skeletal Animation.zip" (already shipped with the app). The
model inside is a genuine Mixamo COLLADA export of Mixamo's "Beta" character
(the Y-Bot/X-Bot mesh family): full ~65-joint standard Mixamo skeleton with
BARE joint names (`Hips`, `LeftArm` — no `mixamorig:` prefix; the module's
socket canonicalizer treats bare and prefixed names as the same key),
centimeter units, Y-up, three skinned mesh pieces.

Why strip the animation: the export carries ONE baked 4.8 s clip as per-joint
channels. As a default avatar the body must be NEUTRAL — stand at bind pose,
bind no locomotion role by accident, and take whatever Mixamo clips are handed
to it. (rig2.glb taught this the hard way: its bend-test clip is named "Idle",
auto-bound to ClipRole::Idle, and looped a 60-degree swing that read as a bug.)
So this script removes the whole <library_animations> element and nothing
else — controllers, skins, materials, the visual scene all survive verbatim.

Why GENERATED, not checked in: the stripped .dae is still ~5 MB. The bytes
already live in the shipped sample archive; deriving the fixture at build time
(tests/avatar/CMakeLists.txt custom command) keeps the repo at +0 MB, same
policy as make_rig_glb.py.

Usage: make_beta_mannequin.py <repo-root> <output.dae>
"""

import re
import sys
import zipfile

def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_beta_mannequin.py <repo-root> <output.dae>")
    root, out = sys.argv[1], sys.argv[2]

    archive = root + "/scenes/Skeletal Animation.zip"
    with zipfile.ZipFile(archive) as z:
        daes = [n for n in z.namelist() if n.lower().endswith(".dae")]
        if len(daes) != 1:
            sys.exit("expected exactly one .dae in %s, found %r" % (archive, daes))
        doc = z.read(daes[0]).decode("utf-8")

    # The one structural edit: drop the animation library wholesale. Collada
    # scenes reference controllers/geometry, never animations, so nothing
    # dangles. Sanity-gate every assumption so a re-exported sample that
    # changes shape fails LOUDLY here instead of downstream in a suite.
    stripped, n = re.subn(r"<library_animations>.*?</library_animations>\s*",
                          "", doc, flags=re.S)
    if n != 1:
        sys.exit("expected exactly one <library_animations> block, found %d" % n)
    for must in ('type="JOINT"', 'name="Hips"', "<library_controllers>",
                 '<unit meter="0.010000"/>'):
        if must not in stripped:
            sys.exit("post-strip sanity failed: %r missing" % must)
    if "<animation " in stripped:
        sys.exit("post-strip sanity failed: stray <animation> element survived")

    with open(out, "w") as f:
        f.write(stripped)
    print("wrote %s (%d bytes, animation-free)" % (out, len(stripped)))

if __name__ == "__main__":
    main()
