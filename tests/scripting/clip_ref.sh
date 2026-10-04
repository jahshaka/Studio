#!/usr/bin/env bash
#
# scripting.e2e.clip_ref — A SKELETAL CLIP IS REFERENCED BY ITS ASSET GUID
# (CLIP-REF-1, the owner's smoke bug #4: two "missing models" and an avatar
# standing in her bind pose).
#
# Two defects, both arms below fail on the code before the lane:
#   A. a save re-derived the clip's guid from its PATH, and the reader kept the
#      persisted path (relative to the PROJECT folder, which need not live in
#      the data root). Once that path named another data root's store, a save
#      wrote the clip without its guid and the next open lost the model.
#   B. a placed model or a spawned avatar sourced its OWN clips from the file
#      it was imported from, which the bake store refuses — the own clip came
#      back null at every placement while that file still existed.
#
# ARM 1 (process 1, data root R1, projects OUTSIDE the data root, the staged
#   source files still on disk): import + spawn an avatar with embedded clips,
#   load an extra clip, place a model with its own clip, DUPLICATE the avatar ->
#   zero issues, every clip resolved (the copy's too); save -> switch project ->
#   reopen, twice.
# ARM 2 (process 2): the data root COPIED to R2 and opened with --data-root
#   (the projects stay where they were, so any path they persisted points at
#   R1's store): zero issues, every clip resolved, after open and after
#   save -> switch -> reopen, twice.
#
# $1 = the Jahshaka binary, $2 = a rig with embedded clips (rig2.glb),
# $3 = an animation file for it (rig2_walk_anim.glb), $4 = a model with its own
# clip (mixamo_tpose.fbx), $5 = the JS half (scripts/e2e_clip_ref.js)
set -u
BIN="$1"; RIG="$2"; WALK="$3"; MODEL="$4"; LIB="$5"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the suite must run with its own data root}"
case "$HOME" in
    */e2e-home-clip_ref) ;;
    *) echo "FAIL: refusing to run outside the suite's scratch HOME ($HOME)"; exit 1 ;;
esac
case "$JAHSHAKA_DATA_ROOT:$PWD" in
    "$HOME"/*:"$HOME"/run) ;;
    *) echo "FAIL: the data root and the working directory must be inside $HOME"; exit 1 ;;
esac
R1="$JAHSHAKA_DATA_ROOT"
R2="$HOME/relocated/Jahshaka"
LOC="$HOME/Documents/Jahshaka"
rm -rf "$R1" "$HOME/relocated" "$HOME/Documents" "$HOME/cwd"
find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
mkdir -p staged "$LOC"
cp "$RIG" staged/Hero.glb && cp "$WALK" staged/HeroWalk.glb && cp "$MODEL" staged/Prop.fbx

# The JS half is $5 (scripts/e2e_clip_ref.js); each process runs it plus ONE call.
{ cat "$LIB"; echo "arm1(\"$PWD/staged\", \"$LOC\");"; } > arm1.run.js
# THE PROCESS RUNS FROM A DIRECTORY AS DEEP UNDER \$HOME AS A PROJECT FOLDER
# ($LOC/Projects/<guid>). Before the lane, an import blob recorded the model's
# own clips by the import path RELATIVE to the open project's folder, and a
# placement resolved that string against the process's working directory: at
# equal depths it named the staged file again — present, outside the store,
# refused by the bake store — and the own clip came back null (defect B; the
# owner's box hit exactly that). At any other depth the path missed and an
# own-model fallback hid the bug. This makes the failing case the tested one.
mkdir -p "$HOME/cwd/a/b/c"
RUN="$PWD"
( cd "$HOME/cwd/a/b/c" && "$BIN" --headless --script "$RUN/arm1.run.js" ) > arm1.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm1.log
check $rc "arm 1 (import, spawn, place, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 1: ALL OK" arm1.log
check $? "arm 1 reached its end"

P1="$(grep -o 'CLIPREF P1=[^ ]*' arm1.log | cut -d= -f2)"
P2="$(grep -o ' P2=[^ ]*' arm1.log | cut -d= -f2)"
AV="$(grep -o ' AV=[^ ]*' arm1.log | cut -d= -f2)"
PL="$(grep -o ' PL=[^ ]*' arm1.log | cut -d= -f2)"
DUP="$(grep -o ' DUP=[^ ]*' arm1.log | cut -d= -f2)"

[ -n "$P1" ] && [ -n "$P2" ] || { echo "FAIL: arm 1 named no projects; arm 2 cannot run"; exit 1; }

# No path is persisted as a clip's reference.
mkdir -p "$(dirname "$R2")" && cp -a "$R1" "$R2"
check $? "the data root is copied to $R2"

{ cat "$LIB"; echo "arm2(\"$P1\", \"$P2\", \"$AV\", \"$PL\", \"$DUP\");"; } > arm2.run.js
JAHSHAKA_DATA_ROOT="$R2" "$BIN" --headless --data-root "$R2" --script arm2.run.js > arm2.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm2.log
check $rc "arm 2 (relocated data root, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 2: ALL OK" arm2.log
check $? "arm 2 reached its end"

exit $fail
