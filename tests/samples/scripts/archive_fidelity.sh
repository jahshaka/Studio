#!/usr/bin/env bash
# samples.archive_fidelity.<sample> (LIVE-PERSIST-1): the SHIPPED archive, opened on a CLEAN
# machine, against the reference still. A clean machine means EVERYTHING per-user is new: the
# data root (library, store, shader cache, piece cache), HOME and XDG_CACHE_HOME — the piece
# cache used to live in ~/.cache, which no data root moved, and that is how a sample whose
# pieces could not be regenerated passed for one that could.
# usage: archive_fidelity.sh <app> <script.js.in> <archive.zip> <workdir> <reference> <metrics.py>
set -u
APP="$1"; SCRIPT_IN="$2"; ZIP="$3"; WORK="$4"; REF="$5"; METRICS="$6"
rm -rf "$WORK"; mkdir -p "$WORK/home/.cache" "$WORK/frames" || exit 1
HOME_DIR="$WORK/home"
DATA="$HOME_DIR/.local/share/Jahshaka"
sed -e "s#@ZIP@#$ZIP#g" -e "s#@OUT@#$WORK/frames#g" -e "s#@HOME@#$HOME_DIR#g" "$SCRIPT_IN" > "$WORK/run.js"
( cd "$(dirname "$APP")" && env HOME="$HOME_DIR" XDG_CACHE_HOME="$HOME_DIR/.cache" \
      timeout -k 10 400 "$APP" --data-root "$DATA" --script "$WORK/run.js" > "$WORK/app.log" 2>&1 )
rc=$?
grep -E '^ok:|assert failed|archive_fidelity' "$WORK/app.log"
if [ $rc -ne 0 ] || ! grep -q 'archive_fidelity: frames written' "$WORK/app.log"; then
    echo "FAIL: the app exited $rc before the frames (log: $WORK/app.log)"; tail -5 "$WORK/app.log"; exit 1
fi
python3 "$METRICS" "$REF" "$WORK"/frames/t*.png
