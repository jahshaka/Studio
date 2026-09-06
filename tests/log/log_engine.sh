#!/usr/bin/env bash
#
# log.engine — the engine half of the session log (SESSION_LOG_SPEC §10 phase 3
# gate). Needs a reachable display: this is the one log suite that boots a real
# render system, because the whole point is what only a real device can say.
#
# TWO RUNS, and the second is the interesting one:
#   1. WINDOWED (Vulkan). The DEVICE block must name a vendor, a device, a
#      driver version and — the scrape — a Vulkan API version. The per-session
#      ogre sibling must exist and must be Ogre's, not ours.
#   2. HEADLESS (the NULL render system). The same block must DEGRADE, not
#      crash: no device exists, so "(not reported)" is the correct answer and a
#      missing block or a dead process is not.
#
# usage: log_engine.sh <jahshaka-binary> <script.js>
set -u

BIN="$1"
SCRIPT="$2"
fail=0

# ---------------------------------------------------------------------------
# Run 1 — a real device.
LOGDIR="$PWD/logs-engine"
rm -rf "$LOGDIR"; mkdir -p "$LOGDIR"

"$BIN" --script "$SCRIPT" --log-dir "$LOGDIR" > run.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "log.engine: the windowed run exited $rc"
    tail -60 run.log
    exit 1
fi

SESSION=$(ls "$LOGDIR"/jahshaka-*.log 2>/dev/null | grep -v -- '-ogre\.log$' | head -1)
if [ -z "$SESSION" ]; then
    echo "log.engine: FAIL — no session file in $LOGDIR"; ls -la "$LOGDIR"; exit 1
fi
echo "log.engine: session file $SESSION"

have() {   # $1 = grep -E pattern, $2 = meaning, $3 = file (default $SESSION)
    if grep -Eq "$1" "${3:-$SESSION}"; then
        echo "log.engine: ok — $2"
    else
        echo "log.engine: FAIL — $2"
        fail=1
    fi
}

have "e2e: LOG ENGINE COMPLETE" "the payload script reached its sentinel"
have "=== DEVICE ===" "the device block was written"

# The four device rows, each with a NON-EMPTY value that is not the
# "(not reported)" placeholder. On a real Vulkan device all four are knowable.
for row in "render system" "gpu vendor" "gpu device" "driver version" "api version"; do
    if grep -Eq "app: Display:   ${row} *: *\(not reported\)" "$SESSION"; then
        echo "log.engine: FAIL — '${row}' is (not reported) on a real device"
        fail=1
    elif grep -Eq "app: Display:   ${row} *: *\S" "$SESSION"; then
        echo "log.engine: ok — '${row}' = $(grep -m1 -E "app: Display:   ${row} *:" "$SESSION" | sed 's/.*: //')"
    else
        echo "log.engine: FAIL — no '${row}' row in the device block"
        fail=1
    fi
done

# THE SCRAPE, specifically. RenderSystemCapabilities has no API version — this
# row can only exist because the Ogre LogListener was attached before the render
# system initialised and read the line it prints exactly once.
have "app: Display:   api version *: *[0-9]+\.[0-9]+" "the Vulkan API version was scraped from Ogre's own log line"

# Fork F3-B: a per-session ogre SIBLING, not the one fixed name every run used
# to overwrite.
STEM="${SESSION%.log}"
if [ -f "${STEM}-ogre.log" ]; then
    echo "log.engine: ok — the per-session ogre log is beside the session log"
    # It is OGRE's file, not a copy of ours: Ogre's format is "HH:MM:SS: text".
    if grep -Eq "^[0-9]{2}:[0-9]{2}:[0-9]{2}: " "${STEM}-ogre.log"; then
        echo "log.engine: ok — the sibling is Ogre's own log, in Ogre's format"
    else
        echo "log.engine: FAIL — ${STEM}-ogre.log is not in Ogre's format"
        head -3 "${STEM}-ogre.log"
        fail=1
    fi
else
    echo "log.engine: FAIL — no ${STEM}-ogre.log"
    ls -la "$LOGDIR"
    fail=1
fi
if [ -f "$PWD/jahshaka-ogre.log" ]; then
    echo "log.engine: FAIL — the legacy fixed-name jahshaka-ogre.log was written too"
    fail=1
else
    echo "log.engine: ok — the legacy fixed-name ogre log is no longer written"
fi

# ---------------------------------------------------------------------------
# Run 2 — the NULL render system. The block must DEGRADE, not crash.
HLOGDIR="$PWD/logs-engine-headless"
rm -rf "$HLOGDIR"; mkdir -p "$HLOGDIR"
"$BIN" --script "$SCRIPT" --headless --log-dir "$HLOGDIR" > run-headless.log 2>&1
hrc=$?
HSESSION=$(ls "$HLOGDIR"/jahshaka-*.log 2>/dev/null | grep -v -- '-ogre\.log$' | head -1)
if [ -z "$HSESSION" ]; then
    echo "log.engine: FAIL — the headless run produced no session file (exit $hrc)"
    tail -40 run-headless.log
    fail=1
else
    # The SCRIPT may legitimately fail headless (no view to render frames into);
    # what is gated here is that the DEVICE block exists and is honest.
    if grep -q "=== DEVICE ===" "$HSESSION"; then
        echo "log.engine: ok — the headless run still writes a device block"
    else
        echo "log.engine: FAIL — no device block under the NULL render system"
        fail=1
    fi
    if grep -Eq "app: Display:   render system *: *\S" "$HSESSION"; then
        echo "log.engine: ok — the NULL render system names itself"
    else
        echo "log.engine: FAIL — the headless device block names no render system"
        fail=1
    fi
    if grep -Eq "app: Display:   headless *: *true" "$HSESSION"; then
        echo "log.engine: ok — the headless run is marked as such"
    else
        echo "log.engine: FAIL — the headless run is not marked headless"
        fail=1
    fi
    # DEGRADE, not crash: the fields it cannot know say so instead of being
    # absent or being a lie.
    if grep -Eq "app: Display:   api version *: *(\(not reported\)|[0-9])" "$HSESSION"; then
        echo "log.engine: ok — the unknowable rows degrade cleanly"
    else
        echo "log.engine: FAIL — the api version row is missing headless"
        fail=1
    fi
fi

# ---------------------------------------------------------------------------
# Run 3 — the FORWARDING itself, proved deterministically.
#
# The `ogre` category sits at Warning in a dev build and Off in a release one,
# so at default verbosity Ogre's thousands of boot lines cost a suppressed-level
# compare and nothing else — that is the noise-control half of fork F3-B and it
# is asserted first. Raising ONE level must then bring them through, which is
# the only assertion that proves the LogListener is attached and forwarding at
# all (LML_CRITICAL is by definition rare and cannot be provoked on demand).
if [ "$(grep -c '\]ogre: ' "$SESSION")" != "0" ]; then
    echo "log.engine: FAIL — ogre non-criticals leaked into the session log at default verbosity"
    fail=1
else
    echo "log.engine: ok — Ogre's ordinary boot lines stay OUT of the session log by default"
fi

OLOGDIR="$PWD/logs-engine-ogre"
rm -rf "$OLOGDIR"; mkdir -p "$OLOGDIR"
"$BIN" --script "$SCRIPT" --log-dir "$OLOGDIR" --log-level=ogre=log > run-ogre.log 2>&1
OSESSION=$(ls "$OLOGDIR"/jahshaka-*.log 2>/dev/null | grep -v -- '-ogre\.log$' | head -1)
if [ -z "$OSESSION" ]; then
    echo "log.engine: FAIL — the --log-level=ogre=log run produced no session file"
    tail -40 run-ogre.log
    fail=1
else
    forwarded=$(grep -c '\]ogre: ' "$OSESSION")
    if [ "$forwarded" -gt 100 ]; then
        echo "log.engine: ok — --log-level=ogre=log forwards Ogre's own log ($forwarded records)"
    else
        echo "log.engine: FAIL — only $forwarded ogre records with the category raised; the LogListener is not forwarding"
        fail=1
    fi
    # The forwarded text is Ogre's, verbatim. Pick a sentence Ogre always logs.
    if grep -Eq '\]ogre: .*(Registering ResourceManager|Installing plugin|RenderSystem)' "$OSESSION"; then
        echo "log.engine: ok — the forwarded records carry Ogre's own text"
    else
        echo "log.engine: FAIL — the forwarded records do not look like Ogre's"
        grep -m3 '\]ogre: ' "$OSESSION"
        fail=1
    fi
fi

if ! grep -q "log.engine: all checks passed" run.log; then
    echo "log.engine: FAIL — the script half did not report success"
    tail -40 run.log
    fail=1
fi

exit $fail
