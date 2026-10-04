#!/usr/bin/env bash
#
# scripting.e2e.tex_ref — A TEXTURE IS REFERENCED BY ITS ASSET GUID (TEX-REF-1),
# as CLIP-REF-1 made clips.
#
# The defect: a save RE-DERIVED a material map's guid (and an emitter image's)
# from the resolved PATH, and the reader fell back to getAbsolutePath(guid) when
# the store could not resolve a guid — so after one miss the next save wrote a
# meaningless relative path, and the reference was gone for good. Now a map row
# carries its guid from the bind (a verb, a pick, an import, a read) to the
# write, and a miss keeps the guid and says texture.missing.
#
# ARM 1 (process 1, data root R1, projects OUTSIDE the data root): a cube with a
#   map bound from a FILE (imported on the way) and one by asset guid, an
#   imported textured model, an emitter with an image by guid, a GRAPH material
#   (a texture node by guid -> graph.toMaterial) on a second cube -> zero issues,
#   every row names its asset, renders from R1, saves the guid; save -> switch
#   -> reopen, twice.
# ARM 2 (process 2): the data root COPIED to R2 and opened with --data-root
#   (the projects stay where they were): the same guids, every file from R2,
#   zero issues, after open and after save -> switch -> reopen, twice.
# ARM 3 (process 3): a third copy R3 with the cube's base-colour OBJECT deleted
#   from its store: ONE texture.missing naming the slot and the asset, and every
#   save -> switch -> reopen (twice) writes the SAME guid back, never a path.
#
# $1 = the Jahshaka binary, $2 = an image, $3 = another image, $4 = a textured
# model (textured_pbr_quad.glb), $5 = the JS half (scripts/e2e_tex_ref.js)
set -u
BIN="$1"; IMG="$2"; IMG2="$3"; MODEL="$4"; LIB="$5"
fail=0
check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

: "${JAHSHAKA_DATA_ROOT:?the suite must run with its own data root}"
case "$HOME" in
    */e2e-home-tex_ref) ;;
    *) echo "FAIL: refusing to run outside the suite's scratch HOME ($HOME)"; exit 1 ;;
esac
case "$JAHSHAKA_DATA_ROOT:$PWD" in
    "$HOME"/*:"$HOME"/run) ;;
    *) echo "FAIL: the data root and the working directory must be inside $HOME"; exit 1 ;;
esac
R1="$JAHSHAKA_DATA_ROOT"
R2="$HOME/relocated/Jahshaka"
R3="$HOME/missing/Jahshaka"
LOC="$HOME/Documents/Jahshaka"
rm -rf "$R1" "$HOME/relocated" "$HOME/missing" "$HOME/Documents"
find "$PWD" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null
mkdir -p staged "$LOC"
cp "$IMG" staged/brick.png && cp "$IMG2" staged/normal.png && cp "$IMG" staged/spark.png \
    && cp "$IMG2" staged/graph.png && cp "$MODEL" staged/quad.glb
# Three distinct pictures: the store is content-addressed, so equal bytes would be
# one object and a deleted object would take every row that shares it.
printf 'n' >> staged/normal.png; printf 's' >> staged/spark.png; printf 'g' >> staged/graph.png

{ cat "$LIB"; echo "arm1(\"$PWD/staged\", \"$LOC\", \"$R1\");"; } > arm1.run.js
"$BIN" --headless --script "$PWD/arm1.run.js" > arm1.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm1.log
check $rc "arm 1 (bind by file and guid, import, emitter, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 1: ALL OK" arm1.log
check $? "arm 1 reached its end"

W="$(grep -o 'TEXREF {.*}' arm1.log | head -1 | cut -c8-)"
BASEPATH="$(grep -o 'TEXREF BASEPATH=.*' arm1.log | head -1 | cut -d= -f2-)"
[ -n "$W" ] && [ -n "$BASEPATH" ] || { echo "FAIL: arm 1 named no world; arms 2-3 cannot run"; exit 1; }

mkdir -p "$(dirname "$R2")" && cp -a "$R1" "$R2"
check $? "the data root is copied to $R2"
{ cat "$LIB"; echo "arm2($W, \"$R2\");"; } > arm2.run.js
JAHSHAKA_DATA_ROOT="$R2" "$BIN" --headless --data-root "$R2" --script arm2.run.js > arm2.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm2.log
check $rc "arm 2 (relocated data root, save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 2: ALL OK" arm2.log
check $? "arm 2 reached its end"

mkdir -p "$(dirname "$R3")" && cp -a "$R1" "$R3"
check $? "the data root is copied to $R3"
case "$BASEPATH" in
    "$R1"/*) GONE="$R3/${BASEPATH#"$R1"/}" ;;
    *) echo "FAIL: the cube's map ($BASEPATH) is not under $R1"; exit 1 ;;
esac
rm -f "$GONE" && [ ! -e "$GONE" ]
check $? "the cube's base-colour object is deleted from $R3's store"
{ cat "$LIB"; echo "arm3($W);"; } > arm3.run.js
JAHSHAKA_DATA_ROOT="$R3" "$BIN" --headless --data-root "$R3" --script arm3.run.js > arm3.log 2>&1
rc=$?
grep -E "^ok:|assert failed|issues:|ALL OK" arm3.log
check $rc "arm 3 (a miss keeps its guid across save/switch/reopen twice) passes (exit $rc)"
grep -q "arm 3: ALL OK" arm3.log
check $? "arm 3 reached its end"

exit $fail
