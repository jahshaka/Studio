#!/usr/bin/env bash
# samples.archive_fidelity.<sample> (LIVE-PERSIST-1; rewritten by TESTING-SPEED-B2 against the ARCHIVE'S OWN
# INVARIANTS — no reference picture, forward-only): the SHIPPED archive, opened on a CLEAN machine, is complete.
# A clean machine means EVERYTHING per-user is new: the data root (library, store, shader cache, piece cache),
# HOME and XDG_CACHE_HOME — the piece cache used to live in ~/.cache, which no data root moved, and that is how
# a sample whose pieces could not be regenerated passed for one that could.
#   1. THE CLOSURE (here, before the app): every dependency an asset of the manifest names is an asset of the
#      archive, and every file it lists is a member of the zip (archive_invariants.py).
#   2. THE MEMBER COUNT (the app): the import brings in exactly the manifest's assets.
#   3. THE LIVE MATERIALS (the app): every layer re-emits its pieces from its graph with an EMPTY piece cache and
#      the shells keep their baked cut-out map — after the import, and again after a close and a REOPEN.
#   4. THE HOLES (the app's frame, judged here): the funnel is see-through from the sample's own camera — the row's
#      named failure is a sample that lost its cut-out and opens with no holes. No reference picture.
#   5. THE FIXED POINT (the app, then here): the project exported, imported and exported again is the same archive
#      — both exports close and hold the same assets (name and type) and objects. Never held against the shipped zip.
# usage: archive_fidelity.sh <app> <script.js.in> <archive.zip> <workdir> <invariants.py>
set -u
APP="$1"; SCRIPT_IN="$2"; ZIP="$3"; WORK="$4"; INV="$5"
rm -rf "$WORK"; mkdir -p "$WORK/home/.cache" || exit 1
HOME_DIR="$WORK/home"
DATA="$HOME_DIR/.local/share/Jahshaka"
COUNTS=$(python3 "$INV" counts "$ZIP") || { echo "FAIL: the shipped archive's closure: $COUNTS"; exit 1; }
echo "archive_fidelity: the shipped archive closes ($COUNTS)"
set -- $COUNTS   # "<assets> <objects>"
sed -e "s#@ZIP@#$ZIP#g" -e "s#@HOME@#$HOME_DIR#g" -e "s#@REEXPORT@#$WORK/reexport.zip#g" -e "s#@REEXPORT2@#$WORK/reexport2.zip#g" \
    -e "s#@FRAME@#$WORK/funnel.png#g" \
    -e "s#@ASSETS@#$1#g" -e "s#@OBJECTS@#$2#g" "$SCRIPT_IN" > "$WORK/run.js"
( cd "$(dirname "$APP")" && env HOME="$HOME_DIR" XDG_CACHE_HOME="$HOME_DIR/.cache" \
      timeout -k 10 400 "$APP" --data-root "$DATA" --script "$WORK/run.js" > "$WORK/app.log" 2>&1 )
rc=$?
grep -E '^ok:|assert failed|archive_fidelity' "$WORK/app.log"
if [ $rc -ne 0 ] || ! grep -q 'archive_fidelity: fixed point written' "$WORK/app.log"; then
    echo "FAIL: the app exited $rc before the fixed point (log: $WORK/app.log)"; tail -5 "$WORK/app.log"; exit 1
fi
fail=0
python3 "$INV" holes "$WORK/funnel.png" || fail=1
python3 "$INV" same "$WORK/reexport.zip" "$WORK/reexport2.zip" || fail=1
exit $fail
