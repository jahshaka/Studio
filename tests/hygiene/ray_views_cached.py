#!/usr/bin/env python3
# source.ray_views_cached — THE RAY TIER's VIEWS OF OGRE TEXTURES COME OUT OF ITS ONE CACHE (GATHER-HANG-1 stage 1).
#
# WHAT WAS WRONG. Every ray job made a fresh VkImageView of each Ogre texture it bound, every frame, and retired it:
# fifteen sites in three files (spikes/gather-hang-1/PREFLIGHT-stage-1.md, table 1), up to about 420 views made and
# destroyed a frame. irisgl/engine/src/RayQueryViewCache.h is the one cache (engine.ray_view_cache proves its law).
#
# THE RULE: in EVERY source file under irisgl/engine/src (recursively: a new file - a seam split of the ray tier, the
# surface cache, Photon - is held too), no code line makes an image view (`->createView(`, `.createView(`,
# `_createView(`, `vkCreateImageView(`) outside the functions named below, each with its exact count (six in all):
#   RayQueryTier::makeView           2  the cache's make: Ogre's `_createView` (P1/P2) and the layer views (P3/P4)
#   RayQueryTier::ensureDummyImages  3  the black stand-ins: our own 1x1 images, made once
#   RayQueryTier::makeStorageImage   1  our own storage images, made once per size
# A new site is a lookup through RayQueryTier::textureView / GatherHost::gatherView, never a new create.
#
# argv[1] = the repo root
import os
import re
import sys

SRC = "irisgl/engine/src"
EXTS = (".cpp", ".cc", ".h", ".hpp", ".inl")
ALLOWED = {"RayQueryTier::makeView": 2, "RayQueryTier::ensureDummyImages": 3, "RayQueryTier::makeStorageImage": 1}
CREATE = re.compile(r"(->|\.)createView\(|\b_createView\(|\bvkCreateImageView\(")
# a function definition opens at column 0 and names its qualified name before its parameter list
DEFN = re.compile(r"^[A-Za-z][^;]*?\b([A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)+)\s*\(")


def code_lines(text):
    """Each line with its comments removed (// to the end, /* ... */ across lines)."""
    out, block = [], False
    for line in text.split("\n"):
        code, i = "", 0
        while i < len(line):
            if block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                else:
                    block, i = False, end + 2
            elif line.startswith("//", i):
                break
            elif line.startswith("/*", i):
                block, i = True, i + 2
            else:
                code += line[i]
                i += 1
        out.append(code)
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    bad, counts = [], {name: 0 for name in ALLOWED}
    files = []
    for top, dirs, names in os.walk(os.path.join(root, SRC)):
        dirs.sort()
        files += [os.path.relpath(os.path.join(top, n), root) for n in sorted(names) if n.endswith(EXTS)]
    if not files:
        print("source.ray_views_cached: FAIL - no source file under %s/%s" % (root, SRC))
        return 1
    for rel in files:
        try:
            text = open("%s/%s" % (root, rel), encoding="utf-8").read()
        except OSError as e:
            print("source.ray_views_cached: FAIL - cannot read %s (%s)" % (rel, e))
            return 1
        owner = None
        for n, line in enumerate(code_lines(text), 1):
            m = DEFN.match(line)
            if m:
                owner = m.group(1)
            if not CREATE.search(line):
                continue
            if owner in ALLOWED:
                counts[owner] += 1
            else:
                bad.append("%s:%d (in %s): %s" % (rel, n, owner or "?", line.strip()))
    for name, want in ALLOWED.items():
        if counts[name] != want:
            bad.append("%s makes %d views, want exactly %d (a new create there is a new site)" % (name, counts[name], want))
    if bad:
        print("source.ray_views_cached: FAIL - an image view made outside the ray tier's cache (RayQueryViewCache.h):")
        for b in bad:
            print("    " + b)
        print("    A view of an Ogre texture is RayQueryTier::textureView(t, kind) / GatherHost::gatherView(t, kind).")
        return 1
    print("source.ray_views_cached: ok - %d engine source files: every view of an Ogre texture comes out of the ray "
          "tier's cache (makeView 2, ensureDummyImages 3, makeStorageImage 1)" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main())
