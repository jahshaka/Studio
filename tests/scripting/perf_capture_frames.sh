#!/usr/bin/env bash
#
# scripting.e2e.perf_capture_frames — THE CAPTURE WINDOW IS COUNTED IN FRAMES
# (PERF-CAPTURE-SCRIPT-1, lane FENCE-1).
#
# Two halves, because the scripting engine has no file access: the app half
# (e2e_perf_capture_frames.js) records and asserts everything the verbs can see,
# and this half READS THE BUNDLE — which is the only assertion that cannot be
# fooled by a counter. It checks:
#
#   * frames.jsonl holds EXACTLY 60 records (one JSON object per line);
#   * machine.json's capture block says requestedFrames/plannedFrames 60,
#     framesWritten 60, and plannedSeconds 0 (no wall-clock window was armed);
#   * the timed CONTROL bundle beside it holds ZERO records — the defect this
#     whole change exists for, kept as a live control rather than a story.
#
# usage: perf_capture_frames.sh <jahshaka-binary> <script.js> <bundle-dir>
set -u

BIN="$1"
SCRIPT="$2"
BUNDLE="$3"
CONTROL="$BUNDLE-seconds-control"

# A bundle is never overwritten (FrameMonitor::start refuses), so a re-run of
# the suite starts from a clean slate.
rm -rf "$BUNDLE" "$CONTROL"

"$BIN" --script "$SCRIPT" > app.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "perf_capture_frames: the app exited $rc"
    tail -30 app.log
    exit "$rc"
fi
grep -E '^(ok|control):' app.log || true

fail() { echo "perf_capture_frames: $*"; exit 1; }

[ -f "$BUNDLE/frames.jsonl" ] || fail "no frames.jsonl in $BUNDLE"
[ -f "$BUNDLE/machine.json" ] || fail "no machine.json in $BUNDLE"

# EXACTLY SIXTY RECORDS. `wc -l` and not a grep: the writer emits one JSON
# object per line and a trailing newline, so the line count IS the record count.
n=$(wc -l < "$BUNDLE/frames.jsonl")
echo "perf_capture_frames: frames.jsonl carries $n record(s)"
[ "$n" -eq 60 ] || fail "expected exactly 60 frame records, found $n"

# ...and every one of them parses, so "60 lines" is 60 RECORDS.
bad=$(python3 - "$BUNDLE/frames.jsonl" <<'PY'
import json, sys
bad = 0
for line in open(sys.argv[1]):
    line = line.strip()
    if not line:
        continue
    try:
        json.loads(line)
    except Exception:
        bad += 1
print(bad)
PY
)
[ "$bad" = "0" ] || fail "$bad frame record(s) do not parse as JSON"

python3 - "$BUNDLE/machine.json" <<'PY' || exit 1
import json, sys
m = json.load(open(sys.argv[1]))
c = m.get("capture", {})
out = []
def want(key, value):
    got = c.get(key)
    ok = got == value
    print("  %s: %s %s" % ("ok" if ok else "FAIL", key, got))
    if not ok:
        out.append("%s is %r, expected %r" % (key, got, value))
want("requestedFrames", 60)
want("plannedFrames", 60)
want("framesWritten", 60)
# NO WALL-CLOCK WINDOW WAS ARMED. This is the assertion that says the frame
# window replaced the timer rather than running beside it.
want("plannedSeconds", 0)
if out:
    print("perf_capture_frames: machine.json disagrees: " + "; ".join(out))
    sys.exit(1)
PY

# THE CONTROL: a timed capture over a script that draws nothing records nothing.
[ -f "$CONTROL/frames.jsonl" ] || fail "no control bundle at $CONTROL"
cn=$(wc -l < "$CONTROL/frames.jsonl")
echo "perf_capture_frames: the TIMED control recorded $cn frame(s) (expected 0 — the render"
echo "                     driver takes no tick under --script, ScriptRunPolicy::Off)"
[ "$cn" -eq 0 ] || fail "the timed control recorded $cn frames; the driver now ticks under --script, so the premise of the frame window has changed — read this as a finding, not a pass"

echo "perf_capture_frames: PASSED"
exit 0
