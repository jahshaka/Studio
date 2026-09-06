#!/usr/bin/env bash
#
# log.perf — the periodic performance sampler (SESSION_LOG_SPEC §10 phase 4
# gate).
#
# TWO HALVES, because the sampler has two halves.
#
#   1. THE LINE. Driven by --script (e2e_log_perf.js): shape, content, the
#      `perf` category, on-demand sampling, 0-means-off, no frame cost.
#
#   2. THE TIMER. A QTimer only fires while an EVENT LOOP is turning, and
#      --script does not turn one (renderFrames steps frames synchronously and
#      never calls processEvents). So the timer half runs the app under
#      --mcp-port, which leaves the loop running, sets the interval to 1 s over
#      MCP, waits in REAL time, and counts the lines that appeared. This is the
#      same shape app.pacing_undo uses, and for the same reason: the only way
#      to observe a timer is to let it run.
#
# usage: log_perf.sh <jahshaka-binary> <script.js> <mcp-port>
set -u

BIN="$1"
SCRIPT="$2"
PORT="$3"
URL="http://127.0.0.1:${PORT}/mcp"
fail=0

# ---------------------------------------------------------------------------
# Half 1 — the line, through --script.
LOGDIR="$PWD/logs-perf"
rm -rf "$LOGDIR"; mkdir -p "$LOGDIR"

"$BIN" --script "$SCRIPT" --log-dir "$LOGDIR" > run.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then
    echo "log.perf: the script run exited $rc"
    tail -60 run.log
    exit 1
fi
if ! grep -q "log.perf: all checks passed" run.log; then
    echo "log.perf: FAIL — the script half did not report success"
    tail -40 run.log
    exit 1
fi
echo "log.perf: ok — the script half passed (line shape, category, off-means-off, no frame cost)"

SESSION=$(ls "$LOGDIR"/jahshaka-*.log 2>/dev/null | grep -v -- '-ogre\.log$' | head -1)
if grep -Eq "\]perf: fps [0-9.]+ work [0-9.]+ms" "$SESSION"; then
    echo "log.perf: ok — the perf line is in the FILE, not just the ring"
    grep -m1 -E "\]perf: " "$SESSION"
else
    echo "log.perf: FAIL — no perf line in the session file"
    fail=1
fi

# ---------------------------------------------------------------------------
# Half 2 — the timer, with the event loop running.
for tool in curl jq; do
    command -v "$tool" > /dev/null 2>&1 || { echo "log.perf: $tool missing"; exit 1; }
done

TLOGDIR="$PWD/logs-perf-timer"
rm -rf "$TLOGDIR"; mkdir -p "$TLOGDIR"
APPLOG="$PWD/app.log"

cleanup() {
    if [ -n "${APP_PID:-}" ] && kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null
        for _ in $(seq 1 50); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 0.1; done
        kill -9 "$APP_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

"$BIN" --mcp-port="$PORT" --log-dir "$TLOGDIR" > "$APPLOG" 2>&1 &
APP_PID=$!

TOKEN=""
for _ in $(seq 1 240); do
    kill -0 "$APP_PID" 2>/dev/null || break
    TOKEN=$(grep -m1 '^MCP: token ' "$APPLOG" 2>/dev/null | sed 's/^MCP: token //')
    [ -n "$TOKEN" ] && break
    sleep 0.5
done
if [ -z "$TOKEN" ]; then
    echo "log.perf: FAIL — the app never published an MCP token"
    tail -40 "$APPLOG"
    exit 1
fi
echo "log.perf: ok — the app is up on port $PORT with its event loop running"

js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",
          params:{name:"run_script",arguments:{script:$s,label:"log_perf"}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
        -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    if [ -z "$inner" ]; then
        echo "log.perf: (transport) ${response:-<no response>}" >&2
        return 1
    fi
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "log.perf: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2
        return 1
    fi
    printf '%s' "$inner" | jq -r '.result'
}

# A scene, so the render loop has something to draw and the counters move.
js 'project.create("Log Perf Timer " + Date.now())' > /dev/null || fail=1
js 'scene.addPrimitive("Cube")' > /dev/null || fail=1
js 'app.space("editor")' > /dev/null || true

# One second per sample, then wait FIVE in real time with the loop turning.
js 'JSON.stringify(log.perf(1))' > /dev/null || fail=1
MARK=$(js 'log.mark("timer window")')
sleep 5
COUNT=$(js 'log.since('"$MARK"', {}).filter(function(r){return /\]perf: /.test(r);}).length')

if [ "${COUNT:-0}" -ge 3 ]; then
    echo "log.perf: ok — the 1 s timer produced $COUNT lines in a 5 s window"
else
    echo "log.perf: FAIL — only ${COUNT:-0} perf lines in a 5 s window at a 1 s interval"
    fail=1
fi

# MONOTONICITY across the real series (the counters are cumulative).
MONO=$(js '
var s = log.since('"$MARK"', {}).filter(function(r){return /\]perf: /.test(r);});
var pr = -1, ps = -1, ok = true;
for (var i = 0; i < s.length; ++i) {
  var r = /rendered (\d+)/.exec(s[i]);
  var w = /slow (\d+)/.exec(s[i]);
  if (!r || !w) { ok = false; break; }
  var rendered = parseInt(r[1], 10), slow = parseInt(w[1], 10);
  if (rendered < pr || slow < ps) ok = false;
  pr = rendered; ps = slow;
}
ok ? "yes" : "no"')
if [ "$MONO" = "yes" ]; then
    echo "log.perf: ok — the cumulative counters never went backwards across the real series"
else
    echo "log.perf: FAIL — the perf series is not monotonic"
    fail=1
fi

# THE LOOP IS ACTUALLY RUNNING, which is what makes the count above meaningful:
# a sampler that produced lines while nothing rendered would prove nothing.
RENDERED=$(js 'app.frameStats().rendered')
if [ "${RENDERED:-0}" -gt 0 ]; then
    echo "log.perf: ok — the render loop drew $RENDERED frames during the window"
else
    echo "log.perf: FAIL — nothing rendered; the series says nothing about a running app"
    fail=1
fi

# 0 = OFF, with the event loop still turning — the assertion the --script half
# cannot make, because there the timer could never have fired anyway.
js 'JSON.stringify(log.perf(0))' > /dev/null || fail=1
OFFMARK=$(js 'log.mark("timer off window")')
sleep 3
OFFCOUNT=$(js 'log.since('"$OFFMARK"', {}).filter(function(r){return /\]perf: /.test(r);}).length')
if [ "${OFFCOUNT:-1}" = "0" ]; then
    echo "log.perf: ok — the sampler is silent for 3 s after log.perf(0), with the loop running"
else
    echo "log.perf: FAIL — $OFFCOUNT perf lines after log.perf(0)"
    fail=1
fi

js 'app.quit()' > /dev/null 2>&1 || true
for _ in $(seq 1 100); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 0.1; done

exit $fail
