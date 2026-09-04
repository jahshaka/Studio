#!/usr/bin/env bash
#
# app.pacing_undo — the frame-pacing loop and the Ctrl+Z routing, both on the
# REAL binary with a REAL X display and REAL key events (deep audit 2026-09,
# area 7 F8 + area 1's dead Materials-page undo).
#
# Why this shape. Neither half is observable any other way:
#
#   * The render loop is a QTimer. Nothing a script can call moves it —
#     editor.frame(n) deliberately bypasses the driver — so the app has to be
#     LEFT RUNNING with its event loop turning while the test watches the
#     counters. --mcp-port is the only mode that does that (--script evaluates
#     and exits), so the test drives run_script over the MCP endpoint.
#
#   * "Ctrl+Z is ambiguous, so nothing happens" is a property of Qt's shortcut
#     MAP, not of any function: it only exists when real key events reach a real
#     window with real widget visibility. xdotool + QT_LOGGING_RULES=
#     qt.gui.shortcutmap.debug is how the audit measured it and is how it is
#     gated here.
#
# WM-less Xvfb facts this depends on (CLAUDE.md): the window must be CLICKED
# before Qt treats it as active (Qt::WindowShortcut needs an active window), and
# letters and modified chords arrive clean (unlike F-keys, which arrive as Alt+F*).
#
# Note on the first space switch: --mcp-port boots through beginEngineSelftest,
# which shows the editor PAGE by setting the stack index directly while
# `currentSpace` stays DESKTOP. Part 1 therefore switches to the editor space
# properly first, so that leaving it runs the real transition.
#
# usage: pacing_undo.sh <jahshaka-binary> <port>
# cwd is a scratch run dir (ctest sets it); HOME is a scratch home.
set -u

BIN="$1"
PORT="$2"
URL="http://127.0.0.1:${PORT}/mcp"
LOG="$PWD/app.log"

fail=0
ok()   { echo "pacing_undo: ok — $1"; }
bad()  { echo "pacing_undo: FAIL — $1"; fail=1; }
note() { echo "pacing_undo: .. $1"; }

cleanup() {
    if [ -n "${APP_PID:-}" ] && kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null
        for _ in $(seq 1 50); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 0.1; done
        kill -9 "$APP_PID" 2>/dev/null
    fi
    if [ -n "${XVFB_PID:-}" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null
    fi
}
trap cleanup EXIT

for tool in xdotool curl jq Xvfb; do
    command -v "$tool" > /dev/null 2>&1 || { echo "pacing_undo: $tool missing"; exit 1; }
done

# ------------------------------------------------------- its own display -----
# THIS SUITE OWNS ITS DISPLAY, and deliberately ignores JAH_TEST_DISPLAY.
#
# Every other display-using suite only RENDERS; this one synthesises mouse
# clicks and key chords through XTEST, which go to whatever window the server
# considers focused. JAH_TEST_DISPLAY defaults to :0 — a developer's live
# session — so honouring it would fire Ctrl+Z and a click-drag into whatever the
# person at the keyboard happens to have open. A private Xvfb is the only safe
# arrangement, and it also makes the window geometry the drag coordinates are
# computed from predictable.
DISP=""
for n in $(seq 90 130); do
    [ -e "/tmp/.X11-unix/X${n}" ] && continue
    Xvfb ":${n}" -screen 0 1920x1080x24 -nolisten tcp > /dev/null 2>&1 &
    XVFB_PID=$!
    for _ in $(seq 1 40); do
        if DISPLAY=":${n}" xdpyinfo > /dev/null 2>&1; then DISP=":${n}"; break; fi
        kill -0 "$XVFB_PID" 2>/dev/null || break
        sleep 0.25
    done
    [ -n "$DISP" ] && break
    kill "$XVFB_PID" 2>/dev/null; XVFB_PID=""
done
[ -n "$DISP" ] || { echo "pacing_undo: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

# ---------------------------------------------------------------- boot -------
# The shortcut map's own debug output is the evidence for the ambiguity half.
export QT_LOGGING_RULES="qt.gui.shortcutmap.debug=true"
"$BIN" --mcp-port="$PORT" > "$LOG" 2>&1 &
APP_PID=$!

TOKEN=""
for _ in $(seq 1 240); do
    kill -0 "$APP_PID" 2>/dev/null || break
    TOKEN=$(grep -m1 '^MCP: token ' "$LOG" 2>/dev/null | sed 's/^MCP: token //')
    [ -n "$TOKEN" ] && break
    sleep 0.5
done
if [ -z "$TOKEN" ]; then
    echo "pacing_undo: the app never published an MCP token"
    tail -40 "$LOG"
    exit 1
fi
ok "app is up on port $PORT"

# run_script over MCP. The transport wraps twice (JSON-RPC result ->
# content[0].text -> {ok,result}); this unwraps both and echoes the verb's
# return value. Scripts that return a JSON.stringify get the raw object text.
js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",
          params:{name:"run_script",arguments:{script:$s,label:"pacing_undo"}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
        -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    if [ -z "$inner" ]; then
        echo "pacing_undo: (transport) ${response:-<no response>}" >&2
        return 1
    fi
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "pacing_undo: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2
        return 1
    fi
    printf '%s' "$inner" | jq -r '.result'
}
# js for a verb whose value is a JSON string built with JSON.stringify.
num() { printf '%s' "$1" | jq -r "if has(\"$2\") then .$2 else \"?\" end" 2>/dev/null || echo "?"; }

# The app's MAIN window. The process owns four (a 3x3 selection owner, a 1x1
# helper, the main window and a small popup), and `search --pid` returns them in
# no useful order — take the LARGEST, which is the only one with a menu bar to
# click and the only one the shortcuts belong to.
WIN=""
WIN_W=0; WIN_H=0
for _ in $(seq 1 40); do
    best=""; bestarea=0
    for w in $(xdotool search --pid "$APP_PID" 2>/dev/null); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null) || continue
        ww=$(printf '%s' "$g" | sed -n 's/^WIDTH=//p'); hh=$(printf '%s' "$g" | sed -n 's/^HEIGHT=//p')
        [ -n "${ww:-}" ] && [ -n "${hh:-}" ] || continue
        area=$((ww*hh))
        if [ "$area" -gt "$bestarea" ]; then best=$w; bestarea=$area; WIN_W=$ww; WIN_H=$hh; fi
    done
    # Anything smaller than this is a helper window, not the editor.
    if [ -n "$best" ] && [ "$bestarea" -gt 200000 ]; then WIN=$best; break; fi
    sleep 0.25
done
[ -n "$WIN" ] || { echo "pacing_undo: no main window for pid $APP_PID"; tail -20 "$LOG"; exit 1; }
note "window $WIN (${WIN_W}x${WIN_H})"

# WM-less Xvfb: no window manager ever gives focus (windowactivate prints a
# _NET_ACTIVE_WINDOW complaint and does nothing), so a CLICK is what makes Qt
# treat the window as active — and Qt::WindowShortcut needs an active window.
activate() {
    xdotool windowactivate --sync "$WIN" 2>/dev/null
    xdotool mousemove --window "$WIN" 40 400 click 1 2>/dev/null
    sleep 0.25
}
# XTEST (no --window): xdotool's --window form sends XSendEvent keys, which Qt
# discards. The window has been clicked, so plain XTEST keys land in it.
key() { xdotool key --clearmodifiers "$1"; sleep 0.45; }
# A press-drag-release in window-relative coordinates, given as PER-MILLE of the
# window so the gesture survives a differently sized display. Qt's drag loop
# needs real motion events, which XTEST provides; the intermediate steps are not
# optional (one jump never starts a QDrag).
drag() {   # $1..$4 = from-x, from-y, to-x, to-y, per mille
    local sx sy dx dy i x y
    sx=$((WIN_W*$1/1000)); sy=$((WIN_H*$2/1000))
    dx=$((WIN_W*$3/1000)); dy=$((WIN_H*$4/1000))
    xdotool mousemove --window "$WIN" "$sx" "$sy"; sleep 0.3
    xdotool mousedown 1; sleep 0.3
    for i in 1 2 3 4 5 6 7 8; do
        x=$((sx + (dx-sx)*i/8)); y=$((sy + (dy-sy)*i/8))
        xdotool mousemove --window "$WIN" "$x" "$y"; sleep 0.12
    done
    sleep 0.3; xdotool mouseup 1; sleep 0.8
}

activate
js 'project.create("pacing_undo")' > /dev/null || { echo "pacing_undo: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "pacing_undo: no editor space"; exit 1; }
sleep 0.3

# ============================================================ PART 1 ==========
# Frame pacing: the loop must stop submitting when no View is enabled, and must
# resume on the very next tick when one is (deep audit area 7 F8).

s=$(js 'JSON.stringify(app.frameStats())') || bad "app.frameStats is callable"
note "editor page: $s"
[ "$(num "$s" rendered)" -gt 0 ] 2>/dev/null \
    && ok "the loop renders while the editor viewport is up" \
    || bad "the loop rendered nothing with the viewport up"
[ "$(num "$s" enabledViews)" = "true" ] \
    && ok "the engine reports an enabled View on the editor page" \
    || bad "no enabled View on the editor page"

# --- idle: every View disabled -------------------------------------------
# The space switch and the reading happen in ONE script so no tick can slip
# between them: a script body runs to completion on the UI thread.
a=$(js 'app.space("desktop"); JSON.stringify(app.frameStats())')
sleep 0.6
b=$(js 'JSON.stringify(app.frameStats())')
at=$(num "$a" ticks);    bt=$(num "$b" ticks)
ar=$(num "$a" rendered); br=$(num "$b" rendered)
as=$(num "$a" skipped);  bs=$(num "$b" skipped)
ev=$(num "$b" enabledViews)
note "desktop page: ticks $at->$bt  rendered $ar->$br  skipped $as->$bs  enabledViews=$ev"

[ "$ev" = "false" ] \
    && ok "no View is enabled on the Desktop page" \
    || bad "a View is still enabled on the Desktop page (the rest of part 1 means nothing)"
[ "$bt" -gt "$at" ] \
    && ok "the driver kept ticking while idle ($((bt-at)) ticks) — the error pump still drains" \
    || bad "the driver stopped ticking entirely (the skip must be per tick, not a stop())"
[ "$br" -eq "$ar" ] \
    && ok "not one frame was submitted with zero enabled Views" \
    || bad "the loop rendered $((br-ar)) frames with nothing to draw"
[ "$bs" -gt "$as" ] \
    && ok "$((bs-as)) ticks were counted as skipped" \
    || bad "no tick was counted as skipped"

# --- wake-up: the FIRST tick after a View is enabled must render ---------
c=$(js 'app.space("editor"); JSON.stringify(app.frameStats())')
sleep 0.4
d=$(js 'JSON.stringify(app.frameStats())')
ct=$(num "$c" ticks);    dt=$(num "$d" ticks)
cr=$(num "$c" rendered); dr=$(num "$d" rendered)
dticks=$((dt-ct)); drend=$((dr-cr))
note "back on editor: $dticks ticks, $drend rendered"
if [ "$dticks" -gt 0 ]; then
    # The enable happened in the same script body that produced `c`, so every
    # tick counted here is a tick that could have rendered. At most one may not.
    [ $((dticks-drend)) -le 1 ] \
        && ok "rendering resumed within one tick ($drend of $dticks ticks rendered)" \
        || bad "the loop took $((dticks-drend)) ticks to wake up"
else
    bad "no tick happened after re-enabling — the wake-up is unproven"
fi

# ============================================================ PART 2 ==========
# Ctrl+Z routing. Owner decision: the Materials space owns the chord and drives
# the GRAPH stack there; every other space keeps the editor undo.

activate

# --- the editor page still undoes a scene edit ---------------------------
# The add and the count are ONE run_script on purpose: every MCP run is its own
# undo macro, so a separate counting call would push an EMPTY macro on top and
# Ctrl+Z would undo that instead of the cube.
e=$(js 'var b = scene.nodes().length;
        scene.addPrimitive("cube", {name:"pacing_cube"});
        JSON.stringify({before:b, after:scene.nodes().length})')
before=$(num "$e" before); mid=$(num "$e" after)
[ "$mid" -gt "$before" ] || bad "the cube did not land in the scene ($before -> $mid)"
key ctrl+z
after=$(js 'scene.nodes().length')
note "editor page: scene nodes $before -> $mid -> $after after Ctrl+Z"
[ "$after" -eq "$before" ] \
    && ok "Ctrl+Z on the editor page undid the scene edit" \
    || bad "Ctrl+Z on the editor page did not undo (nodes $mid -> $after)"

# --- the Materials page does NOT reach the editor's stack ----------------
guard=$(js 'scene.addPrimitive("cube", {name:"pacing_cube2"}); scene.nodes().length')
js 'app.space("materials")' > /dev/null || bad "the Materials space could be shown"
activate
key ctrl+z
kept=$(js 'scene.nodes().length')
note "materials page: scene nodes $guard -> $kept after Ctrl+Z"
[ "$kept" -eq "$guard" ] \
    && ok "Ctrl+Z on the Materials page left the SCENE alone" \
    || bad "Ctrl+Z on the Materials page undid a scene edit ($guard -> $kept)"

# --- and it DOES drive the graph's stack --------------------------------
u=$(js 'JSON.stringify(graph.undoState())')
note "graph undo state on the page: $u"
[ "$(num "$u" available)" = "true" ] \
    && ok "graph.undoState reaches the Materials page's stack (the delegate is wired)" \
    || bad "graph.undoState reports no page stack — the verbs are not wired to the page"

# A REAL graph edit, made the way a user makes one: drag a node tile out of the
# palette strip along the bottom of the page onto the empty canvas. That is
# GraphNodeScene::addNodeModel -> AddNodeCommand -> this stack.
#
# It has to be a gesture rather than a verb: the graph MUTATION verbs work on a
# script-local NodeGraph, NOT the page's (materials.loadGraph deserializes its
# own copy), so nothing they do can ever reach the page's stack — reported to
# the lead as an API-first hole, deliberately not fixed in this lane. Delete is
# no good either: the only node on a fresh canvas is the master, and
# deleteSelectedNodes refuses to delete that one.
#
# Per-mille coordinates: the "Time" tile in the Input tab (~57%, 86%) to an
# empty part of the canvas (~31%, 28%).
drag 569 858 310 283
u1=$(js 'graph.undoState().undoCount')
if [ "${u1:-0}" -gt 0 ] 2>/dev/null; then
    ok "a node dragged onto the canvas landed on the graph's stack (undoCount=$u1)"
    key ctrl+z
    u2=$(js 'JSON.stringify(graph.undoState())')
    n2=$(num "$u2" undoCount); r2=$(num "$u2" redoCount)
    note "after Ctrl+Z: $u2"
    { [ "$n2" -lt "$u1" ] && [ "$r2" -gt 0 ]; } \
        && ok "Ctrl+Z on the Materials page moved the GRAPH stack ($u1 -> $n2, redo $r2)" \
        || bad "Ctrl+Z on the Materials page did not touch the graph stack ($u1 -> $n2)"
    key ctrl+shift+z
    u3=$(js 'JSON.stringify(graph.undoState())')
    n3=$(num "$u3" undoCount)
    note "after Ctrl+Shift+Z: $u3"
    [ "$n3" -gt "$n2" ] \
        && ok "Ctrl+Shift+Z redid it on the GRAPH stack ($n2 -> $n3)" \
        || bad "Ctrl+Shift+Z did not redo on the graph stack ($n2 -> $n3)"

    # ...and the VERBS drive the same stack the chord just drove (the API-first
    # half: the shortcut and graph.undo/graph.redo are two callers of one
    # EffectsPage entry point, so a test that only pressed keys would leave the
    # verbs unproven).
    v1=$(js 'JSON.stringify({undid: graph.undo(), state: graph.undoState()})')
    note "after graph.undo(): $v1"
    { [ "$(printf '%s' "$v1" | jq -r '.undid')" = "true" ] \
      && [ "$(printf '%s' "$v1" | jq -r '.state.undoCount')" -lt "$n3" ]; } \
        && ok "graph.undo() undid on the same stack" \
        || bad "graph.undo() did not undo on the page's stack"
    v2=$(js 'JSON.stringify({redid: graph.redo(), state: graph.undoState()})')
    note "after graph.redo(): $v2"
    { [ "$(printf '%s' "$v2" | jq -r '.redid')" = "true" ] \
      && [ "$(printf '%s' "$v2" | jq -r '.state.undoCount')" -eq "$n3" ]; } \
        && ok "graph.redo() redid on the same stack" \
        || bad "graph.redo() did not redo on the page's stack"
    # Nothing left to undo => false, not a silent true.
    js 'graph.undo(); graph.undo()' > /dev/null
    [ "$(js 'graph.undo()')" = "false" ] \
        && ok "graph.undo() reports false on an exhausted stack" \
        || bad "graph.undo() claimed to undo an empty stack"
else
    bad "the palette drag made no graph edit (undoCount=$u1) — the graph half is unproven"
fi

# --- the chords have exactly ONE claimant each ---------------------------
# The original defect in one line of log: with two WindowShortcut claimants Qt
# dispatches QShortcutEvent(..., TRUE) — "ambiguously" — and QShortcut answers
# an ambiguous event by doing nothing at all. The trailing flag is therefore the
# whole assertion, per chord.
#
# Scoped per chord, NOT a blanket "no ambiguous line anywhere": Space is still
# ambiguous on this page (MainWindow's "tool.cycle" vs EffectsPage's own
# QShortcut — the same defect, second instance, found while building this gate
# and reported to the lead), and a blanket grep would tie this suite to that
# separate decision.
for chord in 'Ctrl+Z' 'Ctrl+Shift+Z'; do
    if grep -qF "QShortcutEvent(\"\"$chord\"\"" "$LOG"; then
        ok "the shortcut map dispatched $chord"
    else
        bad "the shortcut map never dispatched $chord — the keys never arrived"
    fi
    if grep -F "QShortcutEvent(\"\"$chord\"\"" "$LOG" | grep -q ', true)'; then
        bad "$chord was dispatched AMBIGUOUSLY — something re-claimed it, so it does nothing"
        grep -nF "QShortcutEvent(\"\"$chord\"\"" "$LOG" | grep ', true)' | head -3
    else
        ok "$chord was never dispatched ambiguously (exactly one claimant)"
    fi
done

js 'app.quit()' > /dev/null 2>&1
sleep 1
exit $fail
