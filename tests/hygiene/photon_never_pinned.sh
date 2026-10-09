#!/usr/bin/env bash
#
# source.photon_never_pinned — THE PHOTON ROW IS NEVER A PIN (WORLD-MODE-1, owner 2026-10-09).
#
# Each World Mode runs Photon at the same name. Photon's own dropdown may move it (the World
# Mode then reads Custom), and re-picking any World Mode snaps it back — which only works if
# `photon` can never sit in scene->worldOverrides, because setMode skips a pinned row. Three
# doors could put it there: worldmodes::setRowValue, worldmodes::pinRowValue, and the scene
# READER (a document written while the old mapping let the row be pinned). The first two are
# driven at runtime by gi.tiers (photon_override_reads_custom); the reader cannot be linked
# by a unit test (tests/document/reader_defaults.py records why), so its half is asserted here
# on the source: the reader deletes the key right after it reads the map (forward-only: one
# remove, never honoured), and nothing first-party inserts a `photon` pin by hand.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: photon_never_pinned.sh <source-root>}"
cd "$ROOT" || { echo "source.photon_never_pinned: no such root $ROOT"; exit 1; }
failures=0

# 1. THE READER drops the key on the line after it reads the map.
if grep -A2 'scene->worldOverrides = sceneObj.value("worldOverrides").toObject();' src/io/scenereader.cpp \
       | grep -q 'scene->worldOverrides.remove(worldmodes::photonRowId());'; then
    echo "source.photon_never_pinned: ok — the reader deletes a 'photon' pin on read"
else
    echo "source.photon_never_pinned: FAIL — src/io/scenereader.cpp reads worldOverrides without"
    echo "    deleting the 'photon' key (scene->worldOverrides.remove(worldmodes::photonRowId()))"
    failures=1
fi

# 2. THE TWO SETTERS refuse it (the guard is in both bodies).
guards=$(grep -c 'if (id == photonRowId())' src/services/worldmodes.cpp || true)
if [ "${guards:-0}" -ge 2 ]; then
    echo "source.photon_never_pinned: ok — setRowValue and pinRowValue both refuse the photon pin"
else
    echo "source.photon_never_pinned: FAIL — worldmodes.cpp carries $guards photon-pin guard(s), want 2"
    echo "    (setRowValue and pinRowValue: 'if (id == photonRowId())')"
    failures=1
fi

# 3. NOBODY inserts it by hand (first-party sources; tests may build a hostile document).
hits=$(grep -rnE 'worldOverrides(\.|\[)(insert\()?[^;]*(photonRowId\(\)|"photon")' src irisgl/document irisgl/core \
           --include=*.cpp --include=*.h | grep -v '\.remove(' || true)
if [ -n "$hits" ]; then
    echo "source.photon_never_pinned: FAIL — a 'photon' pin is written by hand"
    echo "$hits" | sed 's/^/    /'
    failures=1
else
    echo "source.photon_never_pinned: ok — no first-party code writes a 'photon' pin"
fi

exit $failures
