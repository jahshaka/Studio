#!/usr/bin/env bash
#
# app.input_keys — EVERY INPUT-SYNTHESIS ASSERTION THE EDITOR HAS, ON ONE BOOT.
#
# This is the merge of what used to be three ctest rows — app.pacing_undo,
# app.multiselect_keys and app.navigation_keys — each of which started its own
# Xvfb, booted its own Jahshaka under --mcp-port, found the main window and
# clicked it before it could assert anything. Three identical ~10 s preambles,
# and all three were RUN_SERIAL, so the gate paid for them end to end
# (TEST_GATE_AUDIT.md §5 step 6). Nothing about the assertions needed three
# processes: they are three PARTS of one question — does a synthesised key
# reach the widget that is supposed to answer it — and they are asserted here
# in one window, in sequence, with every assertion and its wording intact.
#
# WHY REAL KEYS AT ALL, per section (unchanged, and worth keeping in front of
# whoever edits this next):
#
#   * PACING/UNDO. The render loop is a QTimer: nothing a script can call moves
#     it (editor.frame(n) deliberately bypasses the driver), so the app has to
#     be LEFT RUNNING with its event loop turning while the test watches the
#     counters. And "Ctrl+Z is ambiguous, so nothing happens" is a property of
#     Qt's shortcut MAP, not of any function — it only exists when real key
#     events reach a real window with real widget visibility.
#
#   * MULTISELECT. Two WindowShortcut claimants make Qt drop a chord entirely;
#     the only way to gate that is to press the keys in a live window in BOTH
#     spaces. And "N objects delete as ONE undo step" is unobservable from a
#     --script run, because a run is itself one open macro.
#
#   * NAVIGATION. input.fly_controls pins the fly MATHS headless. What only a
#     live window can answer is whether the key ARRIVES (the arrows are
#     WindowShortcut material all over the app) and whether the freed letter
#     now reaches the tool shortcut while the right button is held.
#
# WM-less Xvfb facts this depends on (CLAUDE.md): the window must be CLICKED
# before Qt treats it as active (Qt::WindowShortcut needs an active window),
# and letters, arrows and modified chords arrive clean (unlike F-keys, which
# arrive as Alt+F*).
#
# RUN_SERIAL stays. Key ARRIVAL is timing-sensitive — that is the
# WM-less-Xvfb arrival class in CLAUDE.md's gate facts — and one serial row of
# ~40 s is what this merge replaces three serial rows of ~60 s with.
#
# usage: input_keys.sh <jahshaka-binary> <mcp-port, 0 for ephemeral>
# cwd is a scratch run dir (ctest sets it); HOME is a scratch home.
set -u

BIN="$1"
PORT="$2"
LOG="$PWD/app.log"

fail=0
TAG="input_keys"
ok()   { echo "$TAG: ok — $1"; }
bad()  { echo "$TAG: FAIL — $1"; fail=1; }
note() { echo "$TAG: .. $1"; }

# Every section that greps the shortcut-map log scopes its grep to the lines
# ITS OWN keys produced: the three used to have a log each, and a blanket grep
# over one shared log would let one section's chords convict another's.
MARK=0
mark()    { MARK=$(wc -l < "$LOG"); }
logtail() { tail -n +$((MARK + 1)) "$LOG"; }

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
    command -v "$tool" > /dev/null 2>&1 || { echo "input_keys: $tool missing"; exit 1; }
done

# ------------------------------------------------------- its own display -----
# THIS SUITE OWNS ITS DISPLAY, and deliberately ignores JAH_TEST_DISPLAY.
#
# Every other display-using suite only RENDERS; this one synthesises mouse
# clicks and key chords through XTEST, which go to whatever window the server
# considers focused. JAH_TEST_DISPLAY may name a developer's live session, so
# honouring it would fire Ctrl+Z and a click-drag into whatever the person at
# the keyboard happens to have open. A private Xvfb is the only safe
# arrangement, and it also makes the window geometry the drag coordinates are
# computed from predictable.
DISP=""
for n in $(seq 90 170); do
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
[ -n "$DISP" ] || { echo "input_keys: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

# ---------------------------------------------------------------- boot -------
# The shortcut map's own debug output is the evidence for every ambiguity
# assertion below.
export QT_LOGGING_RULES="qt.gui.shortcutmap.debug=true"
"$BIN" --mcp-port="$PORT" > "$LOG" 2>&1 &
APP_PID=$!

# THE PORT IS READ BACK, NOT ASSUMED (TEST_GATE_AUDIT.md §4.1). ctest passes 0,
# the app binds an ephemeral port and prints it beside the token; two suites
# that both hard-coded 8751 is exactly what made -j4 unsafe.
TOKEN=""; BOUND=""
for _ in $(seq 1 240); do
    kill -0 "$APP_PID" 2>/dev/null || break
    TOKEN=$(grep -m1 '^MCP: token ' "$LOG" 2>/dev/null | sed 's/^MCP: token //')
    BOUND=$(grep -m1 '^MCP: port '  "$LOG" 2>/dev/null | sed 's/^MCP: port //')
    [ -n "$TOKEN" ] && [ -n "$BOUND" ] && break
    sleep 0.5
done
if [ -z "$TOKEN" ] || [ -z "$BOUND" ]; then
    echo "input_keys: the app never published an MCP token and port"
    tail -40 "$LOG"
    exit 1
fi
URL="http://127.0.0.1:${BOUND}/mcp"
ok "app is up on port $BOUND"

# run_script over MCP. The transport wraps twice (JSON-RPC result ->
# content[0].text -> {ok,result}); this unwraps both and echoes the verb's
# return value. Scripts that return a JSON.stringify get the raw object text.
js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" --arg l "$TAG" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",
          params:{name:"run_script",arguments:{script:$s,label:$l}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
        -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    if [ -z "$inner" ]; then
        echo "$TAG: (transport) ${response:-<no response>}" >&2
        return 1
    fi
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "$TAG: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2
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
[ -n "$WIN" ] || { echo "input_keys: no main window for pid $APP_PID"; tail -20 "$LOG"; exit 1; }
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
# A press-drag-release in window-relative PIXELS. Qt's drag loop needs real
# motion events, which XTEST provides; the intermediate steps are not optional
# (one jump never starts a QDrag).
#
# PIXELS, NOT PER-MILLE OF THE WINDOW (hygiene lane, 2026-09-09). This used to
# take fractions of the window measured once, from a 1612x1060 window. When the
# suite became hermetic (JAHSHAKA_DATA_ROOT + an empty settings file) there was
# no stored geometry to restore, so the window opened at the .ui's authored
# 1612x1530 — and 858 per mille of 1530 is 55 pixels lower than of 1060: it
# landed on the palette's TAB BAR, which flipped the tab to "Utility" and
# dragged nothing. The gesture is still real; only the guessing is gone
# (graph.paletteTile answers where the tile actually is).
drag() {   # $1..$4 = from-x, from-y, to-x, to-y, window pixels
    local sx sy dx dy i x y
    sx=$1; sy=$2; dx=$3; dy=$4
    xdotool mousemove --window "$WIN" "$sx" "$sy"; sleep 0.3
    xdotool mousedown 1; sleep 0.3
    for i in 1 2 3 4 5 6 7 8; do
        x=$((sx + (dx-sx)*i/8)); y=$((sy + (dy-sy)*i/8))
        xdotool mousemove --window "$WIN" "$x" "$y"; sleep 0.12
    done
    sleep 0.3; xdotool mouseup 1; sleep 0.8
}

# The viewport's middle. The hierarchy dock owns the left edge and the
# properties dock the right, so the horizontal centre is viewport in every
# layout this window opens with; vertically the toolbar is at the top and the
# timeline at the bottom, so 45% is comfortably inside.
VX=$((WIN_W/2)); VY=$((WIN_H*45/100))

# A HELD fly: right button down over the viewport, key down, wait, key up,
# button up. The fly is a per-frame integration of held keys, so the hold has
# to last real wall-clock time — a tap would move a fraction of a unit and be
# indistinguishable from noise.
flyhold() {   # $1 = key name, $2 = seconds
    xdotool mousemove --window "$WIN" "$VX" "$VY"; sleep 0.2
    xdotool mousedown 3; sleep 0.3
    xdotool keydown "$1"; sleep "$2"; xdotool keyup "$1"; sleep 0.2
    xdotool mouseup 3; sleep 0.3
}

# Float maths via jq (bc is not everywhere and jq already is). NOTE THE -r ON
# gt: jq prints a string RESULT quoted, so without it the answer is "yes" with
# the quotes and every comparison below silently takes the failing branch —
# which is exactly how the navigation suite first reported four failures against
# a run whose own trace showed the camera moving 7.8 units (2026-09-09).
dist() { jq -n --argjson a "$1" --argjson b "$2" '(($a.x-$b.x)*($a.x-$b.x)+($a.y-$b.y)*($a.y-$b.y)+($a.z-$b.z)*($a.z-$b.z)) | sqrt'; }
gt()   { jq -rn --argjson a "$1" --argjson b "$2" 'if $a > $b then "yes" else "no" end'; }

# ##########################################################################
# SECTION 1 — FRAME PACING AND CTRL+Z ROUTING (was app.pacing_undo)
# ##########################################################################
#
# Runs FIRST, and deliberately: its Part 1 measures the render loop with
# nothing else having perturbed it, and its shortcut-map greps want the
# quietest log they can get.
#
# Note on the first space switch: --mcp-port boots through
# beginEngineSelftest, which shows the editor PAGE by setting the stack
# index directly while `currentSpace` stays DESKTOP. Part 1 therefore
# switches to the editor space properly first, so that leaving it runs the
# real transition.
TAG=pacing_undo
mark
activate
js 'project.create("pacing_undo")' > /dev/null || { echo "$TAG: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "$TAG: no editor space"; exit 1; }
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
# WHERE the tile is, though, is a question the API answers: graph.paletteTile
# selects the tab that owns it, scrolls it into view and reports its rect (and
# the canvas rect) in window pixels. The suite drags the tile's CENTRE — no
# window fractions anywhere.
TILE=$(js 'JSON.stringify(graph.paletteTile("Time"))') \
    || bad "graph.paletteTile is callable on the Materials page"
note "Time tile: $TILE"
TX=$(printf '%s' "$TILE" | jq -r '(.x + .w/2) | floor' 2>/dev/null)
TY=$(printf '%s' "$TILE" | jq -r '(.y + .h/2) | floor' 2>/dev/null)
TTAB=$(printf '%s' "$TILE" | jq -r '.tab' 2>/dev/null)
CLICKABLE=$(printf '%s' "$TILE" | jq -r '.clickable' 2>/dev/null)
# A quarter into the canvas: away from the master node in the middle and well
# clear of every dock edge.
CX=$(printf '%s' "$TILE" | jq -r '(.canvas.x + .canvas.w/4) | floor' 2>/dev/null)
CY=$(printf '%s' "$TILE" | jq -r '(.canvas.y + .canvas.h/4) | floor' 2>/dev/null)
[ "$TTAB" = "Input" ] \
    && ok "the Time tile lives in the Input tab, and the verb selected it" \
    || bad "graph.paletteTile put the Time tile in tab '$TTAB'"
[ "$CLICKABLE" = "true" ] \
    && ok "the tile is inside the window and clickable at ${TX},${TY}" \
    || bad "the tile at ${TX},${TY} is not reachable in a ${WIN_W}x${WIN_H} window"
# ...and both sides mean the SAME window. The Materials page is a QMainWindow of
# its own, so its coordinates are the app window's only while it is parented
# into it; a mismatch here would send the drag somewhere else entirely.
VW=$(printf '%s' "$TILE" | jq -r '.window.w' 2>/dev/null)
VH=$(printf '%s' "$TILE" | jq -r '.window.h' 2>/dev/null)
{ [ "$VW" = "$WIN_W" ] && [ "$VH" = "$WIN_H" ]; } \
    && ok "the verb measured the window this suite is clicking (${VW}x${VH})" \
    || bad "the verb measured a ${VW}x${VH} window; xdotool sees ${WIN_W}x${WIN_H}"
note "drag ${TX},${TY} -> ${CX},${CY} (canvas $(printf '%s' "$TILE" | jq -c '.canvas'))"
drag "$TX" "$TY" "$CX" "$CY"
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
    if logtail | grep -qF "QShortcutEvent(\"\"$chord\"\""; then
        ok "the shortcut map dispatched $chord"
    else
        bad "the shortcut map never dispatched $chord — the keys never arrived"
    fi
    if logtail | grep -F "QShortcutEvent(\"\"$chord\"\"" | grep -q ', true)'; then
        bad "$chord was dispatched AMBIGUOUSLY — something re-claimed it, so it does nothing"
        logtail | grep -nF "QShortcutEvent(\"\"$chord\"\"" | grep ', true)' | head -3
    else
        ok "$chord was never dispatched ambiguously (exactly one claimant)"
    fi
done


# ##########################################################################
# SECTION 2 — THE EDIT CHORDS IN BOTH SPACES (was app.multiselect_keys)
# ##########################################################################
#
# Its own project, so nothing section 1 left in the scene can be mistaken
# for one of these edits, and its own log mark, so its ambiguity grep
# convicts only chords IT pressed.
TAG=multiselect_keys
mark
activate
js 'project.create("multiselect_keys")' > /dev/null || { echo "$TAG: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "$TAG: no editor space"; exit 1; }
sleep 0.5
activate

# ============================================================ PART 1 ==========
# Delete on a SET, with real keys, and ONE Ctrl+Z that brings all of it back.
#
# NOTHING MAY RUN BETWEEN THE TWO KEYS. Every run_script — a QUERY included —
# opens and closes an undo macro (scriptengine.cpp:182/202, unconditionally), so
# an empty query run still lands an EMPTY entry on the stack and the next Ctrl+Z
# spends itself undoing that instead of the delete. Measured here on 2026-09-09
# and reported as a defect of the scripting host, not worked around anywhere but
# in the shape of this suite: the delete-then-check pass and the
# delete-then-undo pass use DIFFERENT objects, and the second one presses the
# two keys back to back.

# ---- 1a: the Delete KEY deletes the whole selection -------------------------
IDS=$(js 'var a = scene.addPrimitive("cube"); var b = scene.addPrimitive("sphere");
          editor.select([a, b]); JSON.stringify({a:a, b:b, n:editor.selectionSet().length})') \
    || bad "could not build the two-object selection"
note "selection: $IDS"
[ "$(printf '%s' "$IDS" | jq -r '.n')" = "2" ] \
    && ok "two objects are selected" || bad "the set did not take"
A=$(printf '%s' "$IDS" | jq -r '.a'); B=$(printf '%s' "$IDS" | jq -r '.b')

key Delete
# node.info() RAISES for a node that is gone, which would abort the whole run —
# ask the scene for its node list instead.
GONE=$(js "var ids = scene.nodes().map(function (n) { return n.id; });
           JSON.stringify({a: ids.indexOf('$A') >= 0, b: ids.indexOf('$B') >= 0,
                           sel: editor.selectionSet().length})")
note "after Delete: $GONE"
[ "$(printf '%s' "$GONE" | jq -r '.a')" = "false" ] && [ "$(printf '%s' "$GONE" | jq -r '.b')" = "false" ] \
    && ok "the Delete KEY deleted the whole selection (the registry entry reached the editor)" \
    || bad "Delete did not remove both objects"
[ "$(printf '%s' "$GONE" | jq -r '.sel')" = "0" ] \
    && ok "and left nothing selected" || bad "the selection survived its own delete"

# ---- 1b: ONE Ctrl+Z restores every member -----------------------------------
IDS=$(js 'var c = scene.addPrimitive("cone"); var d = scene.addPrimitive("torus");
          editor.select([c, d]); JSON.stringify({c:c, d:d})') \
    || bad "could not build the second two-object selection"
C=$(printf '%s' "$IDS" | jq -r '.c'); D=$(printf '%s' "$IDS" | jq -r '.d')
key Delete
key ctrl+z                     # BACK TO BACK — see the note above
BACK=$(js "var ids = scene.nodes().map(function (n) { return n.id; });
           JSON.stringify({c: ids.indexOf('$C') >= 0, d: ids.indexOf('$D') >= 0})")
note "delete + one undo: $BACK"
[ "$(printf '%s' "$BACK" | jq -r '.c')" = "true" ] && [ "$(printf '%s' "$BACK" | jq -r '.d')" = "true" ] \
    && ok "ONE Ctrl+Z restored BOTH objects — the multi-delete is one undo step" \
    || bad "one undo did not restore both objects"

# ============================================================ PART 2 ==========
# Ctrl+C / Ctrl+V and Ctrl+D on the editor selection, as real keys. These push
# nothing between the key and its observation, so a query run in between is
# harmless.

js "editor.select(['$C','$D'])" > /dev/null
BEFORE=$(js 'scene.nodes().length')
key ctrl+c
CLIP=$(js 'editor.clipboard().length')
[ "$CLIP" = "2" ] && ok "the Ctrl+C KEY filled the editor clipboard ($CLIP fragments)" \
                  || bad "Ctrl+C did not reach the editor (clipboard = $CLIP)"
key ctrl+v
AFTER=$(js 'scene.nodes().length')
note "nodes: $BEFORE -> $AFTER"
[ "$((AFTER - BEFORE))" = "2" ] 2>/dev/null \
    && ok "the Ctrl+V KEY pasted both clipboard entries into the scene" \
    || bad "Ctrl+V did not paste the clipboard ($BEFORE -> $AFTER)"

js "editor.select(['$C','$D'])" > /dev/null
BEFORE=$(js 'scene.nodes().length')
key ctrl+d
AFTER=$(js 'scene.nodes().length')
[ "$((AFTER - BEFORE))" = "2" ] \
    && ok "the Ctrl+D KEY duplicated both selected objects" \
    || bad "Ctrl+D did not duplicate the set ($BEFORE -> $AFTER)"

# ============================================================ PART 3 ==========
# THE AMBIGUITY GATE: the SAME chords on the Materials space must reach the
# GRAPH and must NOT touch the scene selection.

js 'app.space("effects")' > /dev/null || bad "could not switch to the Materials space"
sleep 0.6
activate
BEFORE=$(js 'scene.nodes().length')
key ctrl+v
key Delete
AFTER=$(js 'scene.nodes().length')
[ "$AFTER" = "$BEFORE" ] \
    && ok "on the Materials space the chords do NOT edit the scene selection ($AFTER nodes)" \
    || bad "a Materials-space chord changed the scene ($BEFORE -> $AFTER)"
if logtail | grep -q "activated ambiguously"; then
    bad "Qt reported an AMBIGUOUS shortcut — a second claimant is back"
else
    ok "no ambiguous-shortcut warning in the log (one claimant per chord)"
fi

# ============================================================ PART 4 ==========
# A text field keeps its own Ctrl+C/Ctrl+V: the registry's WindowShortcut must
# not fire while a QLineEdit has focus (Qt's ShortcutOverride contract).
js 'app.space("editor")' > /dev/null
sleep 0.5
activate
js "editor.select('$C')" > /dev/null
BEFORE=$(js 'scene.nodes().length')
# Ctrl+` opens the script console dock AND puts the keyboard in its input line
# (mainwindow.cpp, 2026-09-09 — it used to open a console you had to click
# before it would take a character, which is also why this probe used to guess
# at a pixel and miss). NOTE-ONLY still: the Qt contract being probed (a text
# widget accepts the ShortcutOverride for Ctrl+C/V) is not something this suite
# implements, so a miss is recorded rather than gated.
key ctrl+grave
sleep 0.6
key ctrl+v
AFTER=$(js 'scene.nodes().length')
[ "$AFTER" = "$BEFORE" ] \
    && ok "Ctrl+V with a text field focused did not paste into the scene" \
    || note "Ctrl+V pasted while a text field was focused ($BEFORE -> $AFTER) — recorded, not gated"

# ============================================================ PART 5 ==========
# CTRL+A (EDITOR_MULTISELECT_SPEC §8.7). Two claims, and the second one is the
# whole reason the chord needed a decision:
#
#   5a  with the OUTLINER focused, Ctrl+A selects every node but the World root;
#   5b  with the CONSOLE INPUT focused, Ctrl+A belongs to the text — it selects
#       the text and leaves the scene selection exactly where it was.
#
# 5b is probed twice because the two halves are separable: a handler that
# declined the chord in a text field would pass "the scene is untouched" while
# leaving Ctrl+A doing nothing at all in the field. So the second probe types,
# selects, and types over — and the scene is asked what actually happened.

# ---- 5a: the outliner ------------------------------------------------------
activate                      # clicks a hierarchy row: focus is in the tree
DOC=$(js 'JSON.stringify({nodes: scene.nodes().length})')
key ctrl+a
SEL=$(js 'JSON.stringify({sel: editor.selectionSet().length,
                          nodes: scene.nodes().length,
                          hasRoot: editor.selectionSet().indexOf(scene.root()) >= 0})')
note "Ctrl+A with the tree focused: $SEL (document was $DOC)"
n_sel=$(printf '%s' "$SEL" | jq -r '.sel'); n_all=$(printf '%s' "$SEL" | jq -r '.nodes')
[ "$n_sel" = "$((n_all - 1))" ]     && ok "the Ctrl+A KEY selected every node but one ($n_sel of $n_all)"     || bad "Ctrl+A did not select all ($n_sel selected of $n_all nodes)"
[ "$(printf '%s' "$SEL" | jq -r '.hasRoot')" = "false" ]     && ok "…and the one left out is the World root (D6)"     || bad "Ctrl+A put the World root in the selection"

# ---- 5b(i): a focused text field keeps the chord ---------------------------
# PART 5a just clicked the outliner, so the focus is in the TREE. Toggle the
# console dock off and on: showing it focuses its input line (see PART 4), so
# the focus is in a QPlainTextEdit by construction rather than by pixel-guess.
key ctrl+grave
sleep 0.4
key ctrl+grave
sleep 0.6
BEFORE=$(js 'JSON.stringify(editor.selectionSet())')
key ctrl+a
AFTER=$(js 'JSON.stringify(editor.selectionSet())')
[ "$AFTER" = "$BEFORE" ]     && ok "Ctrl+A with the console input focused left the scene selection alone"     || bad "Ctrl+A changed the scene selection while a text field had focus"

# ---- 5b(ii): …and it selected the TEXT -------------------------------------
# Type one statement, Ctrl+A, type a DIFFERENT one, Enter. If the chord reached
# the text the second statement REPLACED the first, so exactly one Empty is
# added and no cube; if it did nothing the input holds both concatenated, which
# is a syntax error and adds nothing at all. Either failure is visible in the
# scene, which is the only thing this rig can read.
BEFORE=$(js 'JSON.stringify({n: scene.nodes().length,
                             empties: scene.nodes().filter(function (x) { return x.type === "empty"; }).length})')
xdotool type --delay 30 'scene.addPrimitive("cube")'
sleep 0.3
key ctrl+a
xdotool type --delay 30 'scene.addEmpty()'
sleep 0.3
xdotool key --clearmodifiers Return
sleep 1.2
AFTER=$(js 'JSON.stringify({n: scene.nodes().length,
                            empties: scene.nodes().filter(function (x) { return x.type === "empty"; }).length})')
note "console type/Ctrl+A/type-over: $BEFORE -> $AFTER"
d_n=$(( $(printf '%s' "$AFTER" | jq -r '.n') - $(printf '%s' "$BEFORE" | jq -r '.n') ))
d_e=$(( $(printf '%s' "$AFTER" | jq -r '.empties') - $(printf '%s' "$BEFORE" | jq -r '.empties') ))
if [ "$d_n" = "1" ] && [ "$d_e" = "1" ]; then
    ok "Ctrl+A selected the console text: the second statement REPLACED the first"
elif [ "$d_n" = "0" ]; then
    bad "Ctrl+A did nothing in the console input (the two statements concatenated)"
else
    bad "the console ran something unexpected (+$d_n nodes, +$d_e empties)"
fi


# The console dock is OPEN and focused when section 2 ends (its 5b probes
# leave it that way). Close it before the next section flies the camera: an
# open dock moves the viewport's centre, which is where the fly gesture puts
# the pointer.
key ctrl+grave
sleep 0.4

# ##########################################################################
# SECTION 3 — THE ARROWS FLY AND THE LETTERS ARE FREE (was app.navigation_keys)
# ##########################################################################
#
# Last, because it is the only section that leaves the camera somewhere
# arbitrary, and nothing after it cares.
TAG=navigation_keys
mark
activate
js 'project.create("navigation_keys")' > /dev/null || { echo "$TAG: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "$TAG: no editor space"; exit 1; }
sleep 0.5
activate

# The camera has to be free (not orbiting) for the fly keys to mean anything,
# and parked somewhere known.
js 'editor.setCameraMode("free"); editor.setCamera({position: [0, 5, 20], lookAt: [0, 5, 0]}); true' \
    > /dev/null || bad "could not park the editor camera"
sleep 0.3

# ============================================================ PART 1 ==========
# The ARROWS fly — i.e. the key REACHES the viewport past every WindowShortcut
# and focused widget in the app.
before=$(js 'JSON.stringify(editor.camera().position)') || bad "editor.camera() readable"
flyhold Up 1.0
after=$(js 'JSON.stringify(editor.camera().position)')
moved=$(dist "$before" "$after")
note "Up held 1s: $before -> $after (moved $moved)"
[ "$(gt "$moved" 1.0)" = "yes" ] \
    && ok "RMB + Up FLIES the editor camera (moved $moved units)" \
    || bad "RMB + Up did not move the camera (moved $moved)"

before=$(js 'JSON.stringify(editor.camera().position)')
flyhold Prior 1.0                 # X11 spells PageUp "Prior"
after=$(js 'JSON.stringify(editor.camera().position)')
rise=$(jq -n --argjson a "$before" --argjson b "$after" '$b.y - $a.y')
note "PageUp held 1s: y $(printf '%s' "$before" | jq -r .y) -> $(printf '%s' "$after" | jq -r .y)"
[ "$(gt "$rise" 1.0)" = "yes" ] \
    && ok "RMB + PageUp lifts the camera (rose $rise units)" \
    || bad "RMB + PageUp did not lift the camera (rose $rise)"

# ============================================================ PART 2 ==========
# W NO LONGER FLIES — and, the other half of the same claim, W now REACHES the
# shortcut system while the right button is held, because the viewport stopped
# claiming it. Both are asserted from ONE gesture: hold RMB, press W, and look
# at the camera (must not have moved) and the gizmo mode (must have become
# translate).
js 'editor.setGizmoMode("scale")' > /dev/null || bad "could not park the gizmo on scale"
before=$(js 'JSON.stringify(editor.camera().position)')
flyhold w 1.0
after=$(js 'JSON.stringify(editor.camera().position)')
moved=$(dist "$before" "$after")
gizmo=$(js 'editor.gizmoMode()')
note "W held 1s under RMB: moved $moved, gizmo now $gizmo"
[ "$(gt "$moved" 0.001)" = "no" ] \
    && ok "RMB + W does NOT fly the camera any more (moved $moved)" \
    || bad "W still flies the editor camera (moved $moved)"
[ "$gizmo" = "translate" ] \
    && ok "…and W reached the TOOL shortcut instead — the letter is genuinely free" \
    || bad "W did not reach tool.translate while RMB was held (gizmo: $gizmo)"

# Q and E likewise: no vertical movement.
before=$(js 'JSON.stringify(editor.camera().position)')
flyhold e 1.0
after=$(js 'JSON.stringify(editor.camera().position)')
moved=$(dist "$before" "$after")
[ "$(gt "$moved" 0.001)" = "no" ] \
    && ok "RMB + E does not lift the camera any more (moved $moved)" \
    || bad "E still flies the editor camera (moved $moved)"

# ============================================================ PART 3 ==========
# The PLAYER keeps BOTH spellings. Its gameplay Move action is the one place
# the two key sets have to coexist, and it is document state — so this is a
# cheap read, not a second gesture rig.
binds=$(js 'JSON.stringify(input.bindings()[0].keys)')
note "gameplay Move binds: $binds"
for k in W S A D Up Down Left Right; do
    printf '%s' "$binds" | jq -e --arg k "$k" 'index($k) != null' > /dev/null \
        && ok "player Move still answers to $k" \
        || bad "player Move lost $k"
done

# ---------------------------------------------------------------------------
TAG=input_keys
js 'app.quit()' > /dev/null 2>&1
sleep 1
if [ "$fail" -eq 0 ]; then echo "input_keys: ALL PASS"; else echo "input_keys: FAILURES (fail=$fail)"; fi
exit "$fail"
