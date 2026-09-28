#!/usr/bin/env bash
#
# source.fork_stamp — a configure against a STALE engine install is refused.
#
# D6-FORK-TOOLING (audit V2-D F11): irisgl/scripts/build-ogre.sh records in
# `<install>/BUILT_FROM` the fork commit it built (+ `dirty`, + `buildsettings
# <sha256 of OgreBuildSettings.h>`), and cmake/OgreInstallStamp.cmake refuses a
# configure whose ogre-next checkout is at another commit — the pin bump that used
# to link the old engine's C++ against the new commit's staged media, silently.
# cmake/IncludeOgre.cmake is the caller. This drives the SAME function in script
# mode against scratch fixtures (a one-commit git repo as the checkout, a fake
# install as the prefix) and asserts every refusal and every acceptance, then that
# the writer (build-ogre.sh) and the caller (IncludeOgre.cmake) are still wired, and
# that the install prune (prune-ogre-install.sh) survives a trailing-slash prefix.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: fork_stamp.sh <source-root>}"
MODULE="$ROOT/cmake/OgreInstallStamp.cmake"
[ -f "$MODULE" ] || { echo "source.fork_stamp: FAIL no $MODULE"; exit 1; }
command -v git > /dev/null && command -v cmake > /dev/null ||
    { echo "source.fork_stamp: FAIL needs git and cmake on PATH"; exit 1; }

T="$(mktemp -d "${TMPDIR:-/tmp}/fork-stamp.XXXXXX")"
trap 'rm -rf "$T"' EXIT
fails=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fails=$((fails + 1)); }

# The checkout: irisgl/thirdparty/ogre-next's shape, so the remedy's paths resolve.
SRC="$T/irisgl/thirdparty/ogre-next"
mkdir -p "$SRC"
git -C "$SRC" init -q
git -C "$SRC" -c user.name=t -c user.email=t@t commit -q --allow-empty -m one
HEAD_SHA="$(git -C "$SRC" rev-parse HEAD)"
git -C "$SRC" -c user.name=t -c user.email=t@t commit -q --allow-empty -m two
NEW_SHA="$(git -C "$SRC" rev-parse HEAD)"
OLD_SHA="$HEAD_SHA"

PREFIX="$T/install"
mkdir -p "$PREFIX/include/OGRE-Next"
printf '#define OGRE_NO_FINE_LIGHT_MASK_GRANULARITY 0\n' > "$PREFIX/include/OGRE-Next/OgreBuildSettings.h"
BS="$(sha256sum "$PREFIX/include/OGRE-Next/OgreBuildSettings.h" | cut -d' ' -f1)"

run() {  # run <expect: pass|refuse> <label> [grep pattern the refusal must carry]
    local out rc
    out="$(cmake -DPREFIX="$PREFIX" -DSRC="$SRC" -P "$MODULE" 2>&1)"; rc=$?
    if [ "$1" = pass ]; then
        [ $rc -eq 0 ] && ok "$2" || { bad "$2 — refused: $(echo "$out" | head -3)"; }
    else
        if [ $rc -eq 0 ]; then bad "$2 — ACCEPTED"; return; fi
        if [ -n "${3:-}" ] && ! echo "$out" | grep -q "$3"; then
            bad "$2 — refused, but without '$3': $(echo "$out" | head -3)"; return; fi
        echo "$out" | grep -q "build-ogre.sh" || { bad "$2 — refused with no remedy line"; return; }
        ok "$2"
    fi
}

printf '%s\nbuildsettings %s\n' "$NEW_SHA" "$BS" > "$PREFIX/BUILT_FROM"
run pass   "the install built from the checkout's commit is accepted"
printf '%s\ndirty\nbuildsettings %s\n' "$NEW_SHA" "$BS" > "$PREFIX/BUILT_FROM"
run pass   "a dirty build of the checkout's commit is accepted (the gate refuses it, not configure)"
printf '%s\nbuildsettings %s\n' "$OLD_SHA" "$BS" > "$PREFIX/BUILT_FROM"
run refuse "an install built from ANOTHER fork commit is refused" "STALE ENGINE"
rm -f "$PREFIX/BUILT_FROM"
run refuse "an install with no BUILT_FROM record is refused" "no BUILT_FROM"
: > "$PREFIX/BUILT_FROM"
run refuse "an EMPTY BUILT_FROM (a build-ogre.sh that died writing it) is refused with the remedy" "EMPTY"
printf '%s\n' "$NEW_SHA" > "$PREFIX/BUILT_FROM"
run refuse "a record without the buildsettings hash is refused" "buildsettings"
printf '%s\nbuildsettings %s\n' "$NEW_SHA" "0000$BS" > "$PREFIX/BUILT_FROM"
run refuse "an OgreBuildSettings.h the script did not install is refused" "OgreBuildSettings.h"
# The configure-time half: moving the checkout (a pin bump) turns an accepted
# install into a refused one with nothing else touched.
printf '%s\nbuildsettings %s\n' "$NEW_SHA" "$BS" > "$PREFIX/BUILT_FROM"
git -C "$SRC" -c user.name=t -c user.email=t@t commit -q --allow-empty -m three
run refuse "the same install after the checkout moved one commit is refused" "STALE ENGINE"

# THE PRUNE (irisgl/scripts/prune-ogre-install.sh): an orphan goes, a listed file
# stays - and a prefix spelled with a trailing slash (the manifest never has one) prunes
# exactly the same, never the whole install; a manifest that matches nothing refuses.
PRUNE="$ROOT/irisgl/scripts/prune-ogre-install.sh"
P2="$T/prune"; mkdir -p "$P2/include/OGRE-Next" "$P2/lib" "$P2/bin"
: > "$P2/include/OGRE-Next/Kept.h"; : > "$P2/include/OGRE-Next/Orphan.h"; : > "$P2/lib/libKept.so"
printf '%s\n' "$P2/include/OGRE-Next/Kept.h" "$P2/lib/libKept.so" > "$T/manifest.txt"
if bash "$PRUNE" "$P2/" "$T/manifest.txt" > /dev/null && [ -f "$P2/include/OGRE-Next/Kept.h" ] &&
   [ -f "$P2/lib/libKept.so" ] && [ ! -e "$P2/include/OGRE-Next/Orphan.h" ]; then
    ok "the prune with a trailing-slash prefix removes the orphan and keeps the listed files"
else bad "the prune with a trailing-slash prefix did not keep exactly the manifest"; fi
printf '%s\n' "/elsewhere/include/OGRE-Next/Kept.h" > "$T/manifest-other.txt"
if ! bash "$PRUNE" "$P2" "$T/manifest-other.txt" > /dev/null 2>&1 && [ -f "$P2/include/OGRE-Next/Kept.h" ]; then
    ok "a manifest that names none of the install's files is REFUSED and nothing is pruned"
else bad "a manifest spelled with another prefix pruned the install (or was accepted)"; fi
grep -q 'prune-ogre-install.sh' "$ROOT/irisgl/scripts/build-ogre.sh" &&
    ok "build-ogre.sh prunes through prune-ogre-install.sh" ||
    bad "build-ogre.sh no longer prunes through prune-ogre-install.sh"

# The wiring: the writer writes the format the reader reads, and configure calls it.
grep -q 'echo "buildsettings ' "$ROOT/irisgl/scripts/build-ogre.sh" &&
grep -q 'BUILT_FROM' "$ROOT/irisgl/scripts/build-ogre.sh" &&
    ok "build-ogre.sh writes BUILT_FROM with its buildsettings line" ||
    bad "build-ogre.sh no longer writes the buildsettings line OgreInstallStamp.cmake reads"
grep -q 'jah_ogre_install_stamp_problem' "$ROOT/cmake/IncludeOgre.cmake" &&
grep -q 'jah_ogre_install_stamp_inputs' "$ROOT/cmake/IncludeOgre.cmake" &&
    ok "IncludeOgre.cmake refuses a stale install and re-checks when the stamp or HEAD moves" ||
    bad "IncludeOgre.cmake no longer calls the stamp check (or no longer makes it a configure input)"

if [ $fails -ne 0 ]; then echo "source.fork_stamp: FAILED ($fails)"; exit 1; fi
echo "source.fork_stamp: all ok"
