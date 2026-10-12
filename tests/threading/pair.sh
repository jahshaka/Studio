#!/bin/sh
# threading.mode / threading.gi_resolve — THE PAIR IN ONE ROW (TESTING-SPEED-B3): the same script run
# TWICE in sequence, each in its own VIRGIN home (tests/support/fresh_home.sh, the one wipe), first with
# the editor scene's real worker pool (parallel shader compilation), then with JAHSHAKA_SCENE_THREADS=1
# (serial compilation: Ogre's serial branch, OgreRenderQueue.cpp needs > 1 worker), and the two PNGs
# compared BYTE FOR BYTE (cmp, no tolerance). Both halves run every time: the gi_resolve defect (COLDGI,
# fork 1bccc3f93+a98e2b0af) showed in the SERIAL half, so neither half is a stored control.
# usage: pair.sh <fresh_home.sh> <app> <script.js> <home-parallel> <home-serial> <shot.png>
set -u
FRESH="$1"; APP="$2"; SCRIPT="$3"; HOME_PAR="$4"; HOME_SER="$5"; SHOT="$6"
half() {   # <name> <home> [env...]
    name="$1"; home="$2"; shift 2
    sh "$FRESH" "$home" || { echo "FAIL: $name: the home wipe failed"; exit 1; }
    echo "---- $name half: $home"
    ( cd "$home/run" && env HOME="$home" JAHSHAKA_DATA_ROOT="$home/.local/share/Jahshaka" "$@" \
          "$APP" --script "$SCRIPT" )
    rc=$?
    if [ $rc -ne 0 ]; then echo "FAIL: the $name half exited $rc"; exit 1; fi
    if [ ! -s "$home/run/$SHOT" ]; then echo "FAIL: the $name half wrote no $SHOT"; exit 1; fi
}
half parallel "$HOME_PAR"
half serial "$HOME_SER" JAHSHAKA_SCENE_THREADS=1
if cmp -s "$HOME_PAR/run/$SHOT" "$HOME_SER/run/$SHOT"; then
    echo "ok: the parallel and the serial compile's $SHOT are byte-identical"
    echo "PAIR: PASS"
    exit 0
fi
echo "FAIL: the parallel and the serial compile's $SHOT differ (both kept: $HOME_PAR/run, $HOME_SER/run)"
exit 1
