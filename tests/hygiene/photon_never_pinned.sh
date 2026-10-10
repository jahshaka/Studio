#!/usr/bin/env bash
#
# source.photon_never_pinned — THE PHOTON ROW IS NEVER A PIN (WORLD-MODE-1, owner 2026-10-09).
#
# Each World Mode runs Photon at the same name. Photon's own dropdown may move it (the World
# Mode then reads Custom), and re-picking any World Mode snaps it back — which only works if
# `photon` can never sit in scene->worldOverrides, because setMode skips a pinned row. Three
# doors could put it there: worldmodes::setRowValue, worldmodes::pinRowValue, and the scene
# READER (a document written while the old mapping let the row be pinned). The first two are
# driven at runtime by gi.tiers (photon_override_reads_custom), the reader by
# scripting.e2e.photon_pin_read (a blob that pins photon, reopened); this lint is the source half:
# the reader deletes the key right after it reads the map (forward-only: one remove, never
# honoured), each setter refuses it before its insert, and nothing first-party writes a `photon`
# pin by hand.
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

# 2. THE TWO SETTERS refuse it — the guard INSIDE each body and BEFORE its first insert (a guard
#    elsewhere in the file, or one after the insert, does not count).
setter_report=$(python3 - src/services/worldmodes.cpp <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
bad = []
for sig in (r"bool setRowValue\(const iris::ScenePtr &scene, const QString &id",
            r"void pinRowValue\(const iris::ScenePtr &scene, const QString &id"):
    m = re.search(r"^" + sig + r"[^)]*\)\s*\{", src, re.M)
    if not m:
        bad.append("no definition matching /%s/" % sig); continue
    body_end = re.search(r"^\}", src[m.end():], re.M)
    body = src[m.end(): m.end() + (body_end.start() if body_end else 0)]
    guard = re.search(r"if \(id == photonRowId\(\)\)\s*return", body)
    insert = re.search(r"worldOverrides\s*\.\s*insert\s*\(", body)
    name = sig.split("(")[0].split()[-1].rstrip("\\")
    if not guard:
        bad.append("%s: no 'if (id == photonRowId()) return' in its body" % name)
    elif insert and insert.start() < guard.start():
        bad.append("%s: the guard comes after the insert" % name)
print("\n".join(bad))
PYEOF
)
if [ -z "$setter_report" ]; then
    echo "source.photon_never_pinned: ok — setRowValue and pinRowValue refuse the photon pin before any insert"
else
    echo "source.photon_never_pinned: FAIL — the setters' photon-pin guard:"
    echo "$setter_report" | sed 's/^/    /'
    failures=1
fi

# 3. NOBODY inserts it by hand (first-party sources; tests may build a hostile document). Read
#    across lines (an insert's arguments may wrap), and every WHOLE-MAP assignment of
#    worldOverrides must be one of the known three — EXACTLY ONE in each of these files — each
#    with the reason it cannot carry a `photon` key:
#      src/io/scenereader.cpp        the reader — check 1: the key is removed on the next line
#      src/commands/worldmodecommand.cpp  undo/redo restores a SNAPSHOT of scene->worldOverrides
#                                    (capture, :23) — a copy of a map the setters and the reader
#                                    already keep photon-free
#      irisgl/document/scenegraph/scene.cpp  the reset: an empty map
#    What a source read cannot see (an alias of the map, a QJsonObject built elsewhere and
#    assigned through one of those three) is covered at RUNTIME: gi.tiers
#    photon_override_reads_custom (the setters), scripting.e2e.photon_pin_read (the reader on a
#    blob that pins photon, and the save after it), ui.photon_panel (the panel and its undo).
hand_report=$(python3 - <<'PYEOF'
import os, re
roots = ("src", "irisgl/document", "irisgl/core")
allowed = {"src/io/scenereader.cpp", "src/commands/worldmodecommand.cpp",
           "irisgl/document/scenegraph/scene.cpp"}
ins = re.compile(r"worldOverrides\s*(?:\.\s*insert\s*\(|\[)([^;]*?)(?:;)", re.S)
assign = re.compile(r"worldOverrides\s*=(?!=)")
out = []
seen = set()
for root in roots:
    for d, _, files in os.walk(root):
        for f in files:
            if not f.endswith((".cpp", ".h")): continue
            p = os.path.join(d, f)
            t = open(p, errors="replace").read()
            for m in ins.finditer(t):
                if re.search(r'photonRowId\(\)|"photon"', m.group(1)):
                    out.append("%s:%d: a 'photon' pin written by hand" % (p, t.count("\n", 0, m.start()) + 1))
            hits = list(assign.finditer(t))
            for m in hits:
                line = t.count("\n", 0, m.start()) + 1
                if p not in allowed:
                    out.append("%s:%d: a whole-map worldOverrides assignment outside the known three" % (p, line))
            # EXACTLY ONE in each allowed file: a second assignment there is a new door the
            # reason above does not cover.
            if p in allowed and len(hits) != 1:
                out.append("%s: %d whole-map worldOverrides assignments, want exactly 1" % (p, len(hits)))
            seen.add(p)
for p in sorted(allowed - seen):
    out.append("%s: the allowed file is gone (update the list)" % p)
print("\n".join(out))
PYEOF
)
if [ -n "$hand_report" ]; then
    echo "source.photon_never_pinned: FAIL — a 'photon' pin can be written by hand"
    echo "$hand_report" | sed 's/^/    /'
    failures=1
else
    echo "source.photon_never_pinned: ok — no first-party code writes a 'photon' pin; the three whole-map assignments are the known ones"
fi

exit $failures
