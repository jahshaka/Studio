#!/usr/bin/env python3
"""Generates beta_walk.dae — an ANIMATION-ONLY clip for the Beta mannequin.

The other half of make_beta_mannequin.py. That script strips the animation out
of the shipped Mixamo COLLADA export to make a NEUTRAL body; this one strips
the BODY out of the same file to make a clip that plays on it — which is
exactly the pair a user brings us: a character downloaded once, and a pile of
Mixamo animation files downloaded "without skin" afterwards.

Three edits, in this order, and every one of them is sanity-gated so a
re-exported sample that changes shape fails LOUDLY here instead of in a suite:

  1. Every <animation> element is RENAMED to "Walking". assimp's Collada
     loader merges the 53 single-channel per-joint animations into one
     aiAnimation and takes the FIRST one's name for it (measured: the
     untouched file yields exactly one clip, called "Hips"), so the name of
     the clip a user sees is whatever the first joint's <animation> was
     called. Naming it "Walking" is what makes this fixture prove the thing it
     exists to prove: a clip whose NAME says walk binds ClipRole::Walk with no
     authoring at all (locomotion.cpp's tolerant matcher).

  2. <library_geometries> and <library_controllers> go, and with them the
     three <instance_controller> blocks in the visual scene. What is left is
     the joint hierarchy and the channels that drive it: no meshes, no skins —
     the shape of a real Mixamo animation-only export, and 1.4 MB instead of
     6.5 MB.

  3. Nothing else is touched. The joint NAMES are the join key between a clip
     and a rig (SceneNode::updateAnimation matches on them), so they must stay
     byte-identical to beta_mannequin.dae's, which is guaranteed here by both
     files coming out of the same source document.

NOT a .bvh, deliberately: .bvh is in Constants::ANIMATION_EXTS (the Avatar
page's file dialog) but has NO importer — FileImporter sniffs only
Constants::WHITELIST — so a .bvh cannot become a project asset and therefore
cannot ride a scene reopen or an archive export. avatar.loadClip needs a format
the ONE import pipeline accepts, which is Constants::MODEL_EXTS.

Usage: make_beta_walk.py <repo-root> <output.dae>
"""

import re
import sys
import zipfile


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: make_beta_walk.py <repo-root> <output.dae>")
    root, out = sys.argv[1], sys.argv[2]

    archive = root + "/scenes/Skeletal Animation.zip"
    with zipfile.ZipFile(archive) as z:
        daes = [n for n in z.namelist() if n.lower().endswith(".dae")]
        if len(daes) != 1:
            sys.exit("expected exactly one .dae in %s, found %r" % (archive, daes))
        doc = z.read(daes[0]).decode("utf-8")

    # 1. the clip's name.
    doc, renamed = re.subn(r'(<animation id="[^"]*" name=")[^"]*(")', r"\1Walking\2", doc)
    if renamed < 40:
        sys.exit("expected the ~53 per-joint <animation> elements, renamed %d" % renamed)

    # 2. the body.
    for lib in ("library_geometries", "library_controllers"):
        doc, n = re.subn(r"<%s>.*?</%s>\s*" % (lib, lib), "", doc, flags=re.S)
        if n != 1:
            sys.exit("expected exactly one <%s> block, found %d" % (lib, n))
    doc, n = re.subn(r"<node id=\"[^\"]*\" name=\"[^\"]*\" type=\"NODE\">\s*"
                     r"<instance_controller.*?</instance_controller>\s*</node>\s*",
                     "", doc, flags=re.S)
    if n < 1:
        # Fall back to removing just the instance blocks: a node with no
        # instance is legal Collada, only useless.
        doc, n = re.subn(r"<instance_controller.*?</instance_controller>\s*", "", doc, flags=re.S)
        if n != 3:
            sys.exit("expected three <instance_controller> blocks, found %d" % n)

    # 3. gates.
    for must in ('type="JOINT"', 'name="Hips"', "<library_animations>",
                 '<unit meter="0.010000"/>', 'name="Walking"'):
        if must not in doc:
            sys.exit("post-strip sanity failed: %r missing" % must)
    for forbidden in ("<library_geometries>", "<library_controllers>", "<instance_controller"):
        if forbidden in doc:
            sys.exit("post-strip sanity failed: %r survived" % forbidden)

    with open(out, "w") as f:
        f.write(doc)
    print("wrote %s (%d bytes, animation-only, clip 'Walking')" % (out, len(doc)))


if __name__ == "__main__":
    main()
