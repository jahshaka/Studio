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
     `app/` or `src/` that this repository WROTE for the purpose. The
     exceptions are listed with their reasons: the vendored WebXR assets' and
     three.js's own notices (which live beside the code they cover, exactly
     like a thirdparty/ directory), the fonts' two upstream licence texts, and
     `app/notices/` — which is for the components whose source is NOT IN THIS
     TREE AT ALL (Qt, linked dynamically; the Vulkan loader and MoltenVK,
     redistributed inside the macOS bundle). Such an entry must carry
     `vendored: false`, and only such an entry may read from there.

  4. THE GAPS THE DIRECTORY WALK CANNOT SEE, named rather than hoped for
     (the fix-round read, item 2, which found three.js and the fonts in the
     binary and in no entry): `IN_TREE_VENDORED` lists vendored FILES that sit
     next to our own code, and `NOT_IN_TREE_BUT_SHIPPED` the components this
     binary links or redistributes without carrying their source.

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
    "ogre-next-install": "a BUILD OUTPUT — the engine's install prefix, produced by "
                         "build-ogre.sh from the ogre-next submodule beside it",
}

# A manifest entry may read its notice from one of these non-thirdparty paths,
# with the reason.
ALLOWED_OUTSIDE_THIRDPARTY = {
    "app/content/vr": "the vendored WebXR controller assets' PROVENANCE document, which is "
                      "where their upstream MIT licence is reproduced in full — the file the "
                      "assets were vendored with, beside the .obj files it describes",
    "src/export/viewer": "three.js is vendored as a single built file beside the viewer that "
                         "uses it, with its own THREE_LICENSE next to it — the same shape as a "
                         "thirdparty/ directory, in the place the viewer lives",
    "app/fonts": "the UI and icon FONTS are vendored as .ttf files with the two licence texts "
                 "their upstreams ship (Apache-2.0 and the SIL OFL)",
    "app/notices": "THE ONE EXCEPTION, and only for a `vendored: false` entry: a component "
                   "whose source is not in this tree at all (Qt, linked dynamically; the "
                   "Vulkan loader and MoltenVK, redistributed inside the macOS bundle) has no "
                   "file here to read its licence from, so the canonical text lives in "
                   "app/notices/ with its provenance recorded in the README beside it. Checked "
                   "below: an entry reading from here MUST carry `vendored: false`, and one "
                   "that does may not read from anywhere else",
}

# VENDORED CODE THAT IS NOT UNDER A thirdparty/ ROOT — the gap the coverage walk
# could not see (the fix-round read, item 2: three.js and the fonts were both in
# the binary and in no entry). Each path must be claimed by the named entry, so
# a future vendoring next to our own code fails here the way a submodule does.
IN_TREE_VENDORED = {
    "src/export/viewer/three-webgpu.iife.js": "threejs",
    "app/fonts/Roboto-Regular.ttf": "fonts-apache",
    "app/fonts/OpenSans-Regular.ttf": "fonts-apache",
    "app/fonts/DroidSans.ttf": "fonts-apache",
    "app/fonts/NotoSansUI-Regular.ttf": "fonts-apache",
    "app/fonts/Lato-Regular.ttf": "fonts-ofl",
    "app/fonts/fontawesome-4.7.0.ttf": "fonts-ofl",
    "app/content/vr/meta-quest-touch-pro/left.obj": "webxr-input-profiles",
}

# COMPONENTS THIS BINARY SHIPS OR LINKS THAT ARE NOT IN THE TREE AT ALL. They
# cannot be found by walking directories, so they are named: Qt is linked
# dynamically and the macOS bundle redistributes the Vulkan loader and MoltenVK
# (scripts/make-macos-bundle.sh), and all three have licences this application
# has to show.
NOT_IN_TREE_BUT_SHIPPED = ["qt", "vulkan-loader", "moltenvk"]

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
        # ...and app/notices/ is for the NOT-vendored ones only, both ways round.
        vendored = entry.get("vendored", True)
        if path == "app/notices" and vendored:
            fail("%s reads its notice from app/notices/, which is only for a component whose "
                 "source is NOT in this tree — mark it `vendored: false` or read the licence "
                 "from the code it covers." % cid)
        if not vendored and path != "app/notices":
            fail("%s is marked `vendored: false` but reads its notice from %s. A component that "
                 "is not in this tree keeps its text in app/notices/ with its provenance."
                 % (cid, path))

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

    # ---- 1b. the vendored code that is NOT under a thirdparty/ root -------
    for rel, want in sorted(IN_TREE_VENDORED.items()):
        if not os.path.exists(os.path.join(root, rel)):
            fail("the in-tree vendored file %r is gone — remove it from IN_TREE_VENDORED in "
                 "this script (and from the manifest if nothing else covers it)" % rel)
            continue
        if want not in ids:
            fail("%s is vendored in this tree and the manifest has no '%s' entry claiming it. "
                 "Vendored code next to our own still ships its licence." % (rel, want))
            continue
        claimed_dir = next((e.get("path", "") for e in components if e.get("id") == want), "")
        if not rel.startswith(claimed_dir.rstrip("/") + "/"):
            fail("%s is claimed by '%s', whose path is %r — the entry does not cover the file"
                 % (rel, want, claimed_dir))

    # ---- 1c. shipped or linked, and not in the tree -----------------------
    for cid in NOT_IN_TREE_BUT_SHIPPED:
        if cid in ids:
            continue
        fail("'%s' is shipped or linked by this application and the manifest has no entry for "
             "it. It cannot be found by walking the tree — that is why it is named here." % cid)

    stale = [d for d in NOT_A_COMPONENT
             if not any(os.path.isdir(os.path.join(root, r, d))
                        for r in ("thirdparty", "irisgl/thirdparty"))]
    for d in stale:
        fail("the exclusion %r matches no directory any more — remove it rather than leaving a "
             "licence nobody uses" % d)

    if failures:
        print("source.notices_coverage: FAILED (%d)" % len(failures))
        return 1
    print("  ok: %d component(s) declared — every notice file present, every vendored one read "
          "from its own tree, %d in-tree vendored file(s) claimed, %d shipped-but-not-vendored "
          "named (%d documented exclusion(s))"
          % (len(components), len(IN_TREE_VENDORED), len(NOT_IN_TREE_BUT_SHIPPED),
             len(NOT_A_COMPONENT)))
    print("source.notices_coverage: PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main(os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")))
