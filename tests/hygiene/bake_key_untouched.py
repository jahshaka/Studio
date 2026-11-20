#!/usr/bin/env python3
# source.bake_key_untouched (ATOM-HOUSE-1 stage 1; SPECS/V2_CROSS_REFERENCE.md section 2 "the bake") - THIS BRANCH EDITS NO
# FILE OF THE BAKE KEY'S CONTENT HASH. The mesh bake's producer id is a SHA256 over the files irisgl/CMakeLists.txt lists in
# `_mesh_bake_hashed_sources` (meshbake.cpp/h, materialhelper.cpp, bakeshared.h, import/atomdag/*, four meshoptimizer files):
# a COMMENT edit in one of them re-bakes every library on every box. V2 edits none of them until its ONE bake stage (3.5 +
# 3.7 + 3.4's stored error, one kFormatVersion bump), so a branch cut from the shared branch must not name one in its diff.
# (source.bake_key_guard is the other half of the key: the WATCHED files and their `bake-output: unchanged` line.)
#
# THE LIST IS READ FROM irisgl/CMakeLists.txt, never a copy here: the `set(_mesh_bake_hashed_sources ...)` block, parsed the
# way CMake splits it (whitespace, `#` comments dropped). Every listed file must exist (CMake refuses otherwise too).
#
# THE BASE: the Studio merge-base of HEAD with the PARENT branch named by argv[2] (the shared branch the lane was cut from),
# its irisgl gitlink there, and the irisgl merge-base of that pin with irisgl HEAD. The DIFF is that base against irisgl's
# WORKING TREE (committed and uncommitted alike: the row stops an edit before it is committed). On the parent branch itself
# the merge-base is HEAD and the diff is irisgl's working tree only.
#
# FIRST, THE NEGATIVE CONTROL: the intersection rule must flag a diff that names a hashed file (a guard that cannot fail
# guards nothing), and must not flag one that names only its neighbours.
#
# argv: <studio source root> <parent branch>
import os
import re
import subprocess
import sys


def fail(msg):
    print("FAIL: " + msg)
    sys.stdout.flush()
    sys.exit(1)


def git(cwd, *args):
    p = subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def hashed_list(cmake_text):
    """The files of `set(_mesh_bake_hashed_sources ...)` in CMake's own split: whitespace-separated, `#` to end of line
    dropped. None when the block is gone."""
    m = re.search(r"^set\(_mesh_bake_hashed_sources\b(.*?)\)", cmake_text, re.S | re.M)
    if not m:
        return None
    body = "\n".join(line.split("#", 1)[0] for line in m.group(1).splitlines())
    return body.split()


def touched(changed, hashed):
    """The hashed files a diff's paths name (irisgl-relative)."""
    h = set(hashed)
    return sorted(p for p in changed if p in h)


def main():
    if len(sys.argv) != 3:
        fail("usage: bake_key_untouched.py <studio source root> <parent branch>")
    root, parent = sys.argv[1], sys.argv[2]
    iris = os.path.join(root, "irisgl")

    # ---- the negative control ------------------------------------------------------------------------------------
    sample = "set(_mesh_bake_hashed_sources\n    import/meshbake.cpp   # the serializer\n    import/atomdag/a.h)\n"
    lst = hashed_list(sample)
    if lst != ["import/meshbake.cpp", "import/atomdag/a.h"]:
        fail("the negative control: the list parse read %s from a two-file block" % lst)
    if touched(["engine/src/x.cpp", "import/atomdag/a.h"], lst) != ["import/atomdag/a.h"] or \
            touched(["import/meshbake.cpp.orig", "import/atomdag/b.h"], lst):
        fail("the negative control: the intersection rule does not flag exactly the hashed paths")
    print("ok:   the negative control: a diff naming a hashed file is flagged, its neighbours are not")

    # ---- the list ------------------------------------------------------------------------------------------------
    try:
        with open(os.path.join(iris, "CMakeLists.txt"), encoding="utf-8") as f:
            hashed = hashed_list(f.read())
    except OSError as e:
        fail("cannot read irisgl/CMakeLists.txt: %s" % e)
    if not hashed:
        fail("irisgl/CMakeLists.txt declares no _mesh_bake_hashed_sources: the bake key's producer half is gone or renamed")
    missing = [p for p in hashed if not os.path.isfile(os.path.join(iris, p))]
    if missing:
        fail("listed in the bake key but absent: %s" % ", ".join(missing))
    print("ok:   the bake key hashes %d files (irisgl/CMakeLists.txt)" % len(hashed))

    # ---- the base ------------------------------------------------------------------------------------------------
    rc, mb, err = git(root, "merge-base", "HEAD", parent)
    if rc != 0 or not mb:
        fail("no merge-base of HEAD with '%s' (%s): the branch's base cannot be named" % (parent, err or "no output"))
    rc, pin, err = git(root, "rev-parse", mb + ":irisgl")
    if rc != 0 or not pin:
        fail("no irisgl gitlink at the base %s (%s)" % (mb[:12], err))
    rc, ibase, err = git(iris, "merge-base", pin, "HEAD")
    if rc != 0 or not ibase:
        fail("irisgl has no merge-base of the base's pin %s with its HEAD (%s): fetch it" % (pin[:12], err))
    print("ok:   the base: Studio %s (merge-base with %s), irisgl %s" % (mb[:12], parent, ibase[:12]))

    # ---- the diff ------------------------------------------------------------------------------------------------
    # A hashed file inside a SUBMODULE (meshoptimizer's four) is diffed in that submodule, from its gitlink at the base
    # to its working tree: a moved pin and a dirty file alike. The rest are irisgl's own paths.
    rc, out, _ = git(iris, "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$")
    subs = sorted((line.split(None, 1)[1] for line in out.splitlines() if " " in line), key=len, reverse=True)
    own, inside = [], {}
    for p in hashed:
        sub = next((s for s in subs if p.startswith(s.rstrip("/") + "/")), None)
        if sub:
            inside.setdefault(sub, []).append(p)
        else:
            own.append(p)
    changed = []
    if own:
        rc, out, err = git(iris, "diff", "--name-only", "--no-renames", ibase, "--", *own)
        if rc != 0:
            fail("git diff in irisgl failed: %s" % err)
        changed += out.splitlines()
    for sub, paths in sorted(inside.items()):
        rc, link, err = git(iris, "rev-parse", "%s:%s" % (ibase, sub))
        if rc != 0 or not link:
            fail("no gitlink of %s at irisgl %s (%s)" % (sub, ibase[:12], err))
        rels = [p[len(sub.rstrip("/")) + 1:] for p in paths]
        rc, out, err = git(os.path.join(iris, sub), "diff", "--name-only", "--no-renames", link, "--", *rels)
        if rc != 0:
            fail("git diff in irisgl/%s from its base pin %s failed (%s)" % (sub, link[:12], err))
        changed += [sub.rstrip("/") + "/" + r for r in out.splitlines()]
    hit = touched(changed, hashed)
    if hit:
        print("FAIL: this branch edits %d file(s) of the bake key's content hash (each re-bakes every library):" % len(hit))
        for p in hit:
            print("        irisgl/" + p)
        print("      V2 edits them only in its ONE bake stage (SPECS/V2_CROSS_REFERENCE.md section 2): revert the edit,")
        print("      or hand it to that stage.")
        sys.stdout.flush()
        sys.exit(1)
    print("source.bake_key_untouched: ok (no hashed file differs from irisgl %s)" % ibase[:12])
    sys.stdout.flush()


if __name__ == "__main__":
    main()
