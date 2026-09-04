#!/usr/bin/env bash
#
# app.startup_quiet — assert the second boot under one scratch HOME prints none
# of the three startup-noise defects. See tests/app/CMakeLists.txt for why the
# boot has to happen twice and why each pattern is here.
#
# usage: startup_quiet.sh <jahshaka-binary> <script.js>
# cwd is the scratch run dir (ctest sets it); HOME is the scratch home.
set -u

BIN="$1"
SCRIPT="$2"

run_boot() {   # $1 = log file
    "$BIN" --script "$SCRIPT" > "$1" 2>&1
    return $?
}

# Boot 1 creates JahLibrary.db. Its own output is not asserted on: on a fresh
# HOME the Upgrader takes its early return, so the 6b warning cannot appear.
run_boot boot1.log
rc1=$?
if [ "$rc1" -ne 0 ]; then
    echo "startup_quiet: first boot exited $rc1"
    tail -40 boot1.log
    exit 1
fi

# Boot 2 is the asserted one: the database now exists, so Upgrader opens the
# default SQL connection before MainWindow does.
run_boot boot2.log
rc2=$?
if [ "$rc2" -ne 0 ]; then
    echo "startup_quiet: second boot exited $rc2"
    tail -40 boot2.log
    exit 1
fi

fail=0
assert_absent() {   # $1 = grep -E pattern, $2 = what it means
    if grep -Eq "$1" boot2.log; then
        echo "startup_quiet: FAIL — $2"
        grep -En "$1" boot2.log | head -5
        fail=1
    else
        echo "startup_quiet: ok — $2"
    fi
}

assert_absent "duplicate connection name" \
    "no duplicate SQL connection warning (Lane 6b)"
assert_absent "invalid parameters in SMAA\.material" \
    "no SMAA.material parse error (Lane 6c / ogre-patch 0012)"
assert_absent "qt\.multimedia\.ffmpeg: Using Qt multimedia" \
    "Qt Multimedia is not constructed at boot (Lane 6a)"

# A boot that printed nothing at all would pass everything above; make sure the
# script actually ran.
if ! grep -q "startup_quiet: booted" boot2.log; then
    echo "startup_quiet: FAIL — the payload script did not run (empty/broken boot?)"
    tail -40 boot2.log
    fail=1
fi

exit $fail
