#!/usr/bin/env python3
"""THE BUILD's CONTENT AND WHAT NINJA COULD NOT SEE (URGENT #187; cmake/BuiltFrom.cmake runs it as the build's LAST step).

Ninja rebuilds by mtime: a source restored with an OLD mtime (tar xf, cp -p, rsync -a, touch -r) is older than its objects, so
nothing rebuilds - and the always-run BUILT_FROM step then stamped the tree as it is NOW, so the stamp named content the binary
was never built from (STUDIO-HYGIENE-1: a code-test batch ran a stale binary; kept reproduction: spikes/v2-lane-f/repro-187.sh).
Content decides; an mtime is read only to answer "could ninja have seen this change":

  * THE CONTENT MAP (<build>/BUILT_FROM.content, JSON {"at": <ns>, "files": {<repo-relative path>: <git blob id>}}): every
    tracked and untracked (not ignored) file of Studio, irisgl and the fork (whose Hlms media the build stages into bin/media),
    by git's own content ids - the index's blob for a clean file, `git hash-object` of a modified or untracked one. Gitlinks and
    the vendored submodules' working trees (their applied patch stacks) are not in it, as in the stamp's dirtiness.
  * A file whose id differs from the last stamp's map AND whose mtime is not newer than the last stamp's start = a change ninja
    could not see. The stamp HEALS it (the lead's ruling, 2026-11-20): `touch -c` the file, print ONE loud line
    "BUILT_FROM: N file(s) changed with an old mtime, touched: <files>; this pass is STALE - build again", and stamp
    `stale=N` for THIS pass; the next pass rebuilds them (util.build re-passes on that line). A FORK file in the list is
    touched too, and the line names `./irisgl/scripts/build-ogre.sh` (the Studio build never compiles the fork's sources): the
    file stays PENDING in the map, and `stale_fork` stays set, until the install's BUILT_FROM is newer than the touch.
  * `content=<16 hex>`: one hash over the map - the tree's content as this stamp read it (no tree path in it).
The first stamp of a build (no map yet) trusts the build it finds and seeds the map. Prints the stamp's extra lines on stdout
("content=..", "stale=..", "stale_fork=..", "stale_files=.."), the loud line on stderr.
usage: built_from.py <source dir> <build dir> [<ogre-next install prefix>]
Measured 2026-11-20 on a clone of photon-7 273b8e638 (6987 files): 0.03 s warm, 0.31 s on a cold index."""
import hashlib
import json
import os
import subprocess
import sys
import time

FORK = "irisgl/thirdparty/ogre-next/"
REPOS = (("", ""), ("irisgl", "irisgl/"), ("irisgl/thirdparty/ogre-next", FORK))


def _git(root, *args, data=None):
    r = subprocess.run(["git", *args], cwd=root, input=data, capture_output=True)
    if r.returncode:
        raise OSError(f"git {' '.join(args)} in {root}: rc {r.returncode}")
    return r.stdout


def repo_map(root, prefix):
    """{prefix + path: blob id} of one repo's working tree (tracked + untracked, ignored files out, gitlinks out)."""
    m = {}
    for e in _git(root, "ls-files", "-s", "-z").split(b"\0"):
        if e:
            meta, path = e.split(b"\t", 1)
            mode, sha = meta.split(b" ")[:2]
            if mode != b"160000":
                m[prefix + path.decode("utf-8", "surrogateescape")] = sha.decode()
    st = _git(root, "status", "--porcelain", "-z", "--untracked-files=all", "--ignore-submodules=all").split(b"\0")
    changed, i = [], 0
    while i < len(st):
        e = st[i]
        i += 1
        if len(e) < 4:
            continue
        if e[:1] in (b"R", b"C"):
            i += 1                                   # -z: a rename's source path follows as its own entry
        p = e[3:].decode("utf-8", "surrogateescape")
        if os.path.isfile(os.path.join(root, p)) or os.path.islink(os.path.join(root, p)):
            changed.append(p)
        else:
            m.pop(prefix + p, None)                  # deleted in the working tree
    if changed:
        ids = _git(root, "hash-object", "--no-filters", "--stdin-paths",
                   data="\n".join(changed).encode("utf-8", "surrogateescape")).decode().split()
        m.update({prefix + p: h for p, h in zip(changed, ids)})
    return m


def tree_map(src):
    m = {}
    for rel, prefix in REPOS:
        root = os.path.join(src, rel)
        if os.path.exists(os.path.join(root, ".git")):
            m.update(repo_map(root, prefix))
    return m


def mtime_ns(path):
    try:
        return os.lstat(path).st_mtime_ns
    except OSError:
        return None


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__.split("\n")[-3] + "\n")
        return 2
    src, build = argv[1], argv[2]
    prefix = argv[3] if len(argv) > 3 else os.path.join(src, "irisgl", "thirdparty", "ogre-next-install")
    at = time.time_ns()
    path = os.path.join(build, "BUILT_FROM.content")
    try:
        now = tree_map(src)
    except OSError as e:
        sys.stderr.write(f"BUILT_FROM: the content map could not be read ({e}): this pass is STALE\n")
        print("content=?\nstale=?\nstale_fork=0\nstale_files=")
        return 0
    try:
        prev = json.load(open(path))
        files, last, pending = prev["files"], int(prev["at"]), dict(prev.get("pending_fork") or {})
    except (OSError, ValueError, KeyError, TypeError):
        files, last, pending = None, None, {}
    blind = []
    if files is not None:
        for p, h in now.items():
            if files.get(p) != h:
                t = mtime_ns(os.path.join(src, p))
                if t is not None and t <= last:
                    blind.append(p)
    healed = []
    for p in blind:
        try:
            os.utime(os.path.join(src, p), follow_symlinks=not os.path.islink(os.path.join(src, p)))
            healed.append(p)
        except OSError:
            pass
    keep = dict(now)
    for p in blind:
        if p not in healed and files is not None:   # a file that could not be touched stays stale (its old id kept)
            if p in files:
                keep[p] = files[p]
            else:
                keep.pop(p, None)
    # THE FORK: its sources build in build-ogre.sh, never in this build - a touched fork file stays PENDING until the install's
    # stamp is newer than its touch
    inst = mtime_ns(os.path.join(prefix, "BUILT_FROM")) or 0
    for p in healed:
        if p.startswith(FORK):
            pending[p] = time.time_ns()
    pending = {p: t for p, t in pending.items() if t >= inst and p in now}
    stale = len(blind)   # every blind file makes THIS pass stale, healed or not (its objects are the old content's)
    if blind:
        fork = sorted(p for p in blind if p.startswith(FORK))
        sys.stderr.write(f"BUILT_FROM: {len(blind)} file(s) changed with an old mtime, touched: {' '.join(sorted(healed))}"
                         + (f"; NOT touched (still stale): {' '.join(sorted(set(blind) - set(healed)))}" if len(healed) < len(blind) else "")
                         + "; this pass is STALE - build again"
                         + (f"; {len(fork)} of them the FORK's: run ./irisgl/scripts/build-ogre.sh first" if fork else "") + "\n")
    elif pending:
        sys.stderr.write(f"BUILT_FROM: {len(pending)} FORK file(s) touched and not built since: run ./irisgl/scripts/build-ogre.sh, "
                         "then build again; this pass is STALE\n")
        stale = len(pending)
    doc = {"at": at, "files": keep, "pending_fork": pending}
    try:
        old = open(path).read()
    except OSError:
        old = None
    new = json.dumps(doc, sort_keys=True, separators=(",", ":"))
    if old != new:
        tmp = path + ".tmp"
        with open(tmp, "w") as fh:
            fh.write(new)
        os.replace(tmp, path)
    content = hashlib.sha256("".join(f"{p}\0{h}\n" for p, h in sorted(keep.items())).encode("utf-8", "surrogateescape"))
    worst = sorted(set(blind) | set(pending))
    print(f"content={content.hexdigest()[:16]}\nstale={stale}\nstale_fork={len([p for p in worst if p.startswith(FORK)])}\n"
          f"stale_files={','.join(worst[:20])}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
