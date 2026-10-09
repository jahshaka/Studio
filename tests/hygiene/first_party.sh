#!/usr/bin/env bash
# first_party.sh — sourced by the source lints that walk irisgl (GATE-COST-2; the band-aid audit's #6).
#
#   first_party_paths <repo-dir>   prints the paths of <repo-dir> a first-party walk reads: everything in it
#                                  EXCEPT its SUBMODULES (the `path =` lines of <repo-dir>/.gitmodules — the
#                                  vendored code pinned from elsewhere: assimp, bullet3, zip, the Ogre fork,
#                                  meshoptimizer), its git-IGNORED build output (the Ogre install,
#                                  thirdparty/ogre-next-install: build-ogre.sh's product, in no commit) and .git.
#                                  The in-tree vendored directories (meshoptimizer-clusterlod, the *-patches
#                                  stacks) ARE walked: they are files of this tree, and a copy could land there.
#   submodule_paths <repo-dir>     prints <repo-dir>/<path> for each .gitmodules entry.
#   ignored_paths <repo-dir>       prints the git-ignored files and directories of <repo-dir> (build output).
#   pruned_paths <repo-dir>        both: what a walk skips.
#
# Why prune at all: the submodules are ~6,300 C++ files (104 MB) in a lane tree; walking them on the
# USB-stick root under `ionice -c 3` stalled 0-1 s rows past their 60 s budget
# (SPECS/audits/GATE_COST_2026-10-09.md §2).
submodule_paths() {
    local repo="$1"
    [ -f "$repo/.gitmodules" ] || return 0
    sed -nE 's/^[[:space:]]*path[[:space:]]*=[[:space:]]*(.+[^[:space:]])[[:space:]]*$/\1/p' "$repo/.gitmodules" \
        | while read -r p; do printf '%s/%s\n' "$repo" "$p"; done
}
ignored_paths() {
    local repo="$1"
    git -C "$repo" ls-files --others --ignored --exclude-standard --directory 2>/dev/null \
        | sed -e 's#/$##' -e "s#^#$repo/#"
}
pruned_paths() {
    { submodule_paths "$1"; ignored_paths "$1"; } | sort -u
}
first_party_paths() {
    python3 - "$1" $(pruned_paths "$1") <<'PY'
import os, sys
repo, prune = sys.argv[1].rstrip("/"), {p.rstrip("/") for p in sys.argv[2:]}
def walk(d):
    for e in sorted(os.listdir(d)):
        p = os.path.join(d, e)
        if e == ".git" or p in prune:
            continue
        if os.path.isdir(p) and not os.path.islink(p) and any(q.startswith(p + "/") for q in prune):
            walk(p)                     # a submodule lives below: read around it
        else:
            print(p)
walk(repo)
PY
}
