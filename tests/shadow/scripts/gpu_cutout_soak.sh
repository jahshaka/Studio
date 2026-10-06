#!/usr/bin/env bash
# gpu.cutout_soak (CUTOUT-CASTER-1) — one process, a fresh data root; the verdicts are the
# script's own asserts, its PASS line, and the row's FAIL_REGULAR_EXPRESSION (a validation
# error or a lost device).
# usage: gpu_cutout_soak.sh <app> <script.js.in> <fixture.png> <tornado.zip> <workdir>
set -u
APP="$1"; SCRIPT_IN="$2"; FIXTURE="$3"; TORNADO="$4"; WORK="$5"
rm -rf "$WORK"; mkdir -p "$WORK" || exit 1
sed -e "s#@OUT@#$WORK/shot#g" -e "s#@FIXTURE@#$FIXTURE#g" -e "s#@TORNADO@#$TORNADO#g" "$SCRIPT_IN" > "$WORK/soak.js"
( cd "$(dirname "$APP")" && timeout -k 10 1500 "$APP" --data-root "$WORK/root" --script "$WORK/soak.js" ) 2>&1
rc=$?
rm -rf "$WORK/root"
exit $rc
