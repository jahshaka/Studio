#!/usr/bin/env python3
"""source.notices_coverage — EVERY VENDORED COMPONENT IS ACKNOWLEDGED
(NOTICES-1, 2026-09-18).

The app ships ten third-party components inside one executable and, until this
lane, named none of them anywhere a user could look (found by the VR-INPUT-1E
read while checking the controller meshes' MIT licence — ledger §666 item 10).
`app/notices.json` is the manifest and cmake/Notices.cmake reads each
component's licence text out of its own vendored tree, so nothing is copied into
this repository.

WHAT THIS ASSERTS, and why each half needs a command rather than a habit:

  1. COVERAGE — every directory under `thirdparty/` and `irisgl/thirdparty/`
     that is a vendored COMPONENT is claimed by an entry. The next
     `git submodule add` therefore cannot ship unacknowledged: it fails here.
     The exclusions are listed below WITH THEIR REASONS (our own patch stacks,
     a build output directory), in the shape document.reader_defaults uses.

  2. THE MANIFEST IS HONEST — every entry names a path that exists, and a
     notice file that exists unless the entry is `optional` (a component a
     checkout may not carry: an uninitialised submodule). A `lines` range must
     lie inside its file and must actually contain a licence word, which is
     what catches the range going stale when the file above it grows.

  3. NO LICENCE TEXT IS COPIED — an entry may not point at a file inside
     `app/` or `src/` that this repository WROTE for the purpose. (The one
     `app/` entry is a PROVENANCE document beside vendored assets, which is
     where their upstream licence is reproduced; that is the file the assets
     were vendored with, and it is named as an exception.)

Run: notices_coverage.py <source-dir>
"""

import io
import json
import os
import sys

# Directories under a thirdparty/ root that are NOT a vendored component, with
# the reason. Anything else must be claimed by a manifest entry.
NOT_A_COMPONENT = {
    "assimp-patches": "OUR OWN patch stack for assimp (irisgl/cmake/ApplyVendorPatches.cmake "
                      "applies it) — Jahshaka source, not a third party's",
    "ogre-patches": "OUR OWN patch stack for Ogre-Next (build-ogre.sh applies it)",
    "ogre-next-install": "a BUILD OUTPUT — the engine's install prefix, produced by "
                         "build-ogre.sh from the ogre-next submodule beside it",
}

# A manifest entry may read its notice from one of these non-thirdparty paths,
# with the reason.
ALLOWED_OUTSIDE_THIRDPARTY = {
    "app/content/vr": "the vendored WebXR controller assets' PROVENANCE document, which is "
                      "where their upstream MIT licence is reproduced in full — the file the "
                      "assets were vendored with, beside the .obj files it describes",
}

LICENCE_WORDS = ("licen", "copyright", "permission is hereby granted")


def main(root):
    failures = []

    def fail(msg):
        print("  FAIL " + msg)
        failures.append(msg)

    manifest_path = os.path.join(root, "app", "notices.json")
    if not os.path.isfile(manifest_path):
        print("notices_coverage: cannot find app/notices.json")
        return 2
    with io.open(manifest_path, encoding="utf-8") as f:
        manifest = json.load(f)
    components = manifest.get("components", [])
    if not components:
        fail("the manifest declares no components")

    # ---- 2. the manifest is honest ---------------------------------------
    claimed = set()
    ids = set()
    for entry in components:
        cid = entry.get("id", "")
        if not cid:
            fail("an entry has no id")
            continue
        if cid in ids:
            fail("two entries share the id %r" % cid)
        ids.add(cid)
        for key in ("name", "role", "homepage", "licence", "path", "file"):
            if not entry.get(key):
                fail("%s: no %s" % (cid, key))
        path = entry.get("path", "")
        claimed.add(path)
        abs_dir = os.path.join(root, path)
        optional = bool(entry.get("optional"))
        if not os.path.isdir(abs_dir):
            if not optional:
                fail("%s: the vendored directory %s does not exist" % (cid, path))
            continue
        notice = os.path.join(abs_dir, entry.get("file", ""))
        if not os.path.isfile(notice):
            if not optional:
                fail("%s: the notice file %s/%s does not exist (mark the entry optional if a "
                     "checkout may not carry it)" % (cid, path, entry.get("file")))
            continue
        with io.open(notice, encoding="utf-8", errors="replace") as f:
            lines = f.read().split("\n")
        rng = entry.get("lines")
        if rng:
            if len(rng) != 2 or rng[0] < 1 or rng[1] < rng[0]:
                fail("%s: the lines range %r is not [first, last]" % (cid, rng))
                continue
            if rng[1] > len(lines):
                fail("%s: lines %d-%d asked of %s/%s, which has %d lines — the range went stale"
                     % (cid, rng[0], rng[1], path, entry.get("file"), len(lines)))
                continue
            text = "\n".join(lines[rng[0] - 1:rng[1]])
        else:
            text = "\n".join(lines)
        low = text.lower()
        if not any(w in low for w in LICENCE_WORDS):
            fail("%s: the notice text (%s/%s%s) contains no licence word %r — the wrong file or "
                 "a stale range" % (cid, path, entry.get("file"),
                                    (" lines %d-%d" % (rng[0], rng[1])) if rng else "",
                                    LICENCE_WORDS))
        # ---- 3. nothing this repository wrote for the purpose -------------
        if not path.startswith("thirdparty/") and not path.startswith("irisgl/thirdparty/"):
            if path not in ALLOWED_OUTSIDE_THIRDPARTY:
                fail("%s reads its notice from %s, which is not a vendored tree. A licence must "
                     "be read from the code it covers; add the path to "
                     "ALLOWED_OUTSIDE_THIRDPARTY in this script WITH THE REASON if it really is "
                     "vendored content." % (cid, path))

    # ---- 1. coverage ------------------------------------------------------
    for vendor_root in ("thirdparty", os.path.join("irisgl", "thirdparty")):
        abs_root = os.path.join(root, vendor_root)
        if not os.path.isdir(abs_root):
            continue
        for name in sorted(os.listdir(abs_root)):
            if not os.path.isdir(os.path.join(abs_root, name)):
                continue
            if name in NOT_A_COMPONENT:
                continue
            rel = vendor_root.replace(os.sep, "/") + "/" + name
            # A claim may be the directory itself or something inside it (the
            # breakpad wrapper holds the submodule one level down).
            if any(c == rel or c.startswith(rel + "/") for c in claimed):
                continue
            fail("%s is vendored and NO manifest entry claims it. Add it to app/notices.json "
                 "(with its licence file) so the app can show its notice, or add it to "
                 "NOT_A_COMPONENT in this script with the reason it is not a third party's "
                 "code." % rel)

    stale = [d for d in NOT_A_COMPONENT
             if not any(os.path.isdir(os.path.join(root, r, d))
                        for r in ("thirdparty", "irisgl/thirdparty"))]
    for d in stale:
        fail("the exclusion %r matches no directory any more — remove it rather than leaving a "
             "licence nobody uses" % d)

    if failures:
        print("source.notices_coverage: FAILED (%d)" % len(failures))
        return 1
    print("  ok: %d vendored component(s) declared, every notice file present and read from its "
          "own tree (%d documented exclusion(s))" % (len(components), len(NOT_A_COMPONENT)))
    print("source.notices_coverage: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main(os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")))
