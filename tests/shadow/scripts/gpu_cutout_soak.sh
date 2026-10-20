#!/usr/bin/env bash
# gpu.cutout_soak.<part> (CUTOUT-CASTER-1) — one process, a fresh data root; the verdicts are the
# script's own asserts, its PASS line, and the row's FAIL_REGULAR_EXPRESSION (a validation
# error or a lost device). <part>: primitives (the plain and the live graph cut-outs) | tornado.
# usage: gpu_cutout_soak.sh <app> <script.js.in> <fixture.png> <tornado.zip> <workdir> <part>
set -u
APP="$1"; SCRIPT_IN="$2"; FIXTURE="$3"; TORNADO="$4"; WORK="$5"; PART="$6"
rm -rf "$WORK"; mkdir -p "$WORK" || exit 1
sed -e "s#@OUT@#$WORK/shot#g" -e "s#@FIXTURE@#$FIXTURE#g" -e "s#@TORNADO@#$TORNADO#g" -e "s#@PART@#$PART#g" \
    "$SCRIPT_IN" > "$WORK/soak.js"
( cd "$(dirname "$APP")" && timeout -k 10 300 "$APP" --data-root "$WORK/root" --script "$WORK/soak.js" ) 2>&1
rc=$?
rm -rf "$WORK/root"
exit $rc
