#!/usr/bin/env bash
#
# source.bake_key_guard — the hand-bumped half of the mesh-bake key has a gate.
#
# THE KEY (BAKEKEY-1, 2026-09-15; irisgl/CMakeLists.txt carries the full
# reasoning, SPECS/MESH_BAKE_SPEC.md the decision). A .jmb bake is derived data
# keyed on `<format>|<producer>|<assimp>|<flags>|<sourceOid>`. The `producer`
# term is a configure-time SHA256 over the THREE files that WRITE a bake; it
# used to cover twelve, including the document's mesh/mesh-node/skeleton
# classes, which every lane edits for reasons that cannot move a baked byte —
# measured: 25 of the 408 irisgl commits since the bake landed touched one of
# them, on 10 of 11 working days, so nearly every build threw away every bake in
# every library and every open went back to parsing with assimp.
#
# Those files now ride the HAND-BUMPED `kFormatVersion` in
# irisgl/import/meshbake.cpp instead. A hand bump that can be forgotten is a
# silent wrong answer — a library serving bakes that no longer describe what the
# code produces — so this gate makes forgetting it loud.
#
# THE RULE. For every commit in <base>..HEAD that touches one of the watched
# files, either
#   * the format version moved in that same range (the window resets on a bump:
#     <base> IS the last commit that touched the version line), or
#   * the commit message carries the exact line
#         bake-output: unchanged
#     which is the author stating that the edit cannot change a baked byte.
#
# THE BASE, and why it is not a merge-base. A merge-base needs a branch, and
# this has to answer on the main tree (no branch of its own), in a lane
# worktree, and in a fresh clone. So the base is derived from the history
# itself: the last commit whose diff touched the version line
#     git log -1 -G'^constexpr int kFormatVersion' -- import/meshbake.cpp
# Every bump therefore resets the window, no tag or recorded sha is needed, and
# the answer is identical in every checkout that has the history.
#
# WHAT THIS DOES NOT JUDGE: uncommitted work. A working-tree edit has no commit
# message to acknowledge it, and failing on a dirty tree would red the gate for
# every unrelated lane. Dirty watched files are REPORTED (a note, not a
# failure); the rule applies the moment the edit is committed, which is when a
# lane gates.
#
# $1 = the repo root (Studio)
set -u

ROOT="${1:?usage: bake_key_guard.sh <source-root>}"
IRIS="$ROOT/irisgl"
cd "$IRIS" 2>/dev/null || { echo "source.bake_key_guard: no such tree $IRIS"; exit 1; }

VERSION_FILE="import/meshbake.cpp"
VERSION_RE='^constexpr int kFormatVersion'
CMAKE_FILE="CMakeLists.txt"
ACK_RE='^bake-output: unchanged[[:space:]]*$'

# The files that LEFT the producer hash and are covered by the version instead,
# plus the parse twin the bake has to agree with (import/graphicshelper.cpp).
# This is the original twelve minus what irisgl/CMakeLists.txt still hashes and
# minus import/importflags.{h,cpp} — whose only contribution to a bake is the
# VALUE of ImportFlags::Canonical, which the key already carries as an exact
# integer (`flags%4` in MeshBake::producerId), not as a hash of the prose.
WATCHED=(document/assets/mesh.cpp
         document/assets/mesh.h
         document/assets/skeleton.cpp
         document/assets/skeleton.h
         document/scenegraph/meshnode.cpp
         core/geometry/trimesh.cpp
         import/graphicshelper.cpp)

failures=0

# --- 0. the two halves of the key must not overlap -------------------------
hashed=$(sed -n '/^set(_mesh_bake_hashed_sources/,/)/p' "$CMAKE_FILE" \
         | grep -oE '[a-z0-9_]+/[a-z0-9_/]+\.(cpp|h)' || true)
if [ -z "$hashed" ]; then
    echo "FAIL: irisgl/CMakeLists.txt no longer declares _mesh_bake_hashed_sources —"
    echo "      the producer half of the bake key is gone or was renamed."
    failures=$((failures + 1))
else
    for must in import/meshbake.cpp import/meshbake.h; do
        if ! printf '%s\n' "$hashed" | grep -qx "$must"; then
            echo "FAIL: $must is not in the producer hash. The serializer must always be"
            echo "      hashed — nothing else can notice that the written bytes changed."
            failures=$((failures + 1))
        fi
    done
    overlap=0
    for w in "${WATCHED[@]}"; do
        if printf '%s\n' "$hashed" | grep -qx "$w"; then
            echo "FAIL: $w is BOTH hashed by irisgl/CMakeLists.txt and watched here."
            echo "      A hashed file invalidates bakes by itself: drop it from WATCHED"
            echo "      in this script (and say so in the comment above)."
            overlap=$((overlap + 1))
        fi
    done
    failures=$((failures + overlap))
    [ "$overlap" = 0 ] && echo "ok:   the hashed half and the version-covered half do not overlap"
fi

# --- 1. every watched file still exists ------------------------------------
for w in "${WATCHED[@]}" "$VERSION_FILE"; do
    [ -f "$w" ] && continue
    echo "FAIL: $w does not exist — this gate's file list is stale (a rename?)."
    failures=$((failures + 1))
done

# --- 2. the version constant is still there and readable -------------------
if ! grep -qE "$VERSION_RE" "$VERSION_FILE"; then
    echo "FAIL: no '$VERSION_RE' line in irisgl/$VERSION_FILE — the hand-bumped half"
    echo "      of the key was renamed; update this gate with it."
    failures=$((failures + 1))
else
    echo "ok:   the bake format version is $(grep -E "$VERSION_RE" "$VERSION_FILE" \
          | grep -oE '[0-9]+' | head -1)"
fi

# --- 3. the window: every commit since the last bump ----------------------
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo "note: no git history here — the commit-message half of this gate cannot run"
elif ! base=$(git log -1 --format=%H -G"$VERSION_RE" -- "$VERSION_FILE" 2>/dev/null) \
     || [ -z "$base" ]; then
    echo "note: no commit in this history touches the version line (shallow clone?) —"
    echo "      the commit-message half of this gate cannot run"
else
    head=$(git rev-parse HEAD)
    echo "ok:   the window starts at $(git log -1 --format='%h %s' "$base")"
    if [ "$base" = "$head" ]; then
        echo "ok:   the format version moved at HEAD — nothing to acknowledge"
    else
        touching=$(git log --no-merges --format=%H "$base..$head" -- "${WATCHED[@]}" 2>/dev/null || true)
        unacked=0
        for sha in $touching; do
            if git log -1 --format=%B "$sha" | grep -qE "$ACK_RE"; then continue; fi
            if [ "$unacked" = 0 ]; then
                echo "FAIL: a version-covered file changed without a verdict on the bake."
                echo "      Every commit below touched a file whose text can change what a"
                echo "      .jmb bake HOLDS, and the bake key can no longer notice by itself."
                echo "      Either bump kFormatVersion in irisgl/$VERSION_FILE (every library"
                echo "      re-bakes once), or put this exact line in the commit message:"
                echo "          bake-output: unchanged"
            fi
            unacked=$((unacked + 1))
            echo "      $(git log -1 --format='%h %s' "$sha")"
            git show --name-only --format= "$sha" -- "${WATCHED[@]}" | sed 's/^/          /'
        done
        if [ "$unacked" != 0 ]; then
            failures=$((failures + 1))
        else
            n=$(printf '%s\n' "$touching" | grep -c . || true)
            echo "ok:   $n commit(s) since that bump touched a version-covered file; each"
            echo "      says so ('bake-output: unchanged') or there were none"
        fi
    fi
fi

# --- 4. uncommitted work: a note, never a failure -------------------------
if git rev-parse --git-dir > /dev/null 2>&1; then
    dirty=$(git diff --name-only HEAD -- "${WATCHED[@]}" 2>/dev/null || true)
    if [ -n "$dirty" ]; then
        echo "note: uncommitted changes to version-covered file(s):"
        printf '%s\n' "$dirty" | sed 's/^/          /'
        echo "note: the commit that lands them needs 'bake-output: unchanged' or a bump."
    fi
fi

if [ "$failures" -ne 0 ]; then
    echo "source.bake_key_guard: $failures failure(s)"
    exit 1
fi
echo "source.bake_key_guard: ok"
