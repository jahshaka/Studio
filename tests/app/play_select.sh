#!/usr/bin/env bash
#
# app.play_select — A REAL CLICK AND A REAL GIZMO DRAG, ON A SCENE THAT IS
# PLAYING (PLAY-SELECT-1, owner R13).
#
# `scripting.e2e.play_select` proves the ownership RULE through the verbs. What
# only a live window can answer is whether the gesture ARRIVES: the editor
# viewport used to hand every mouse and key event to the play controller and
# return, so the picker never ran and "select only through the scene graph" was
# the whole of editing during play. That routing is not a function anybody can
# call — it is QMouseEvent delivery into a real widget — so this suite presses
# the actual button on an actual X display, exactly as app.input_keys presses
# actual keys (and for the same reason).
#
# The four questions, in one window, in one boot:
#   1. a plain left click in the viewport during play SELECTS what it is over,
#      the same click that selects in edit mode;
#   2. the simulation is still advancing while that happens (a falling body,
#      sampled around the click) — a click that quietly paused the run would
#      pass claim 1 and be worthless;
#   3. a gizmo drag MOVES the selection live, mid-run;
#   4. Stop restores the document and KEEPS the selection.
# Then the EJECT key (the ShortcutRegistry entry `play.eject`): while a run has
# the keyboard, W is the Gameplay row and does not switch the tool; ejected, it
# is the translate tool again — and the latch reads back through the verb.
#
# WM-less Xvfb facts this depends on (CLAUDE.md): the window must be CLICKED
# before Qt treats it as active; letters and chords arrive clean but F-KEYS
# ARRIVE AS Alt+F*, which is why the eject section rebinds `play.eject` to a
# plain letter through a persisted registry override in jahsettings.ini (the
# documented workaround) rather than pressing F8 and hoping.
#
# THE PIXEL IS NOT GUESSED. editor.viewportState() reports where the viewport
# widget sits inside its window (windowX/Y/W/H), so every click below is the
# window pixel a named viewport pixel maps to — no dock-size arithmetic, no
# per-mille of a window whose size this suite does not control.
#
# RUN_SERIAL, like app.input_keys: synthesised input goes to whatever the X
# server considers focused, and the drag needs real motion events with real
# time between them.
#
# usage: play_select.sh <jahshaka-binary> <mcp-port, 0 for ephemeral>
# cwd is a scratch run dir (ctest sets it); HOME is a scratch home.
set -u

BIN="$1"
PORT="$2"
LOG="$PWD/app.log"

fail=0
TAG="play_select"
ok()   { echo "$TAG: ok — $1"; }
bad()  { echo "$TAG: FAIL — $1"; fail=1; }
note() { echo "$TAG: .. $1"; }

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
    command -v "$tool" > /dev/null 2>&1 || { echo "play_select: $tool missing"; exit 1; }
done

# THE EJECT KEY, REBOUND BEFORE THE APP READS ITS SETTINGS. F8 is the default
# and arrives as Alt+F8 under a WM-less Xvfb; `J` is free in the registry (no
# entry claims it) and is not bound in the InputMap either, so it reaches the
# shortcut whether or not the run has the keyboard. The settings file is this
# suite's own scratch — dropped first so a previous run's dock layout and
# window size cannot leak into this one's geometry.
SETTINGS="${JAHSHAKA_DATA_ROOT:-$PWD}/jahsettings.ini"
mkdir -p "$(dirname "$SETTINGS")"
rm -f "$SETTINGS"
printf '[shortcut]\nplay.eject=J\n' > "$SETTINGS"

# ------------------------------------------------------- its own display -----
# Like app.input_keys: this suite synthesises pointer and key events, which go
# to whatever window the server considers focused, so it must never honour
# JAH_TEST_DISPLAY (which may name a developer's live session).
DISP=""
for n in $(seq 171 240); do
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
[ -n "$DISP" ] || { echo "play_select: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

# ---------------------------------------------------------------- boot -------
"$BIN" --mcp-port="$PORT" > "$LOG" 2>&1 &
APP_PID=$!

TOKEN=""; BOUND=""
for _ in $(seq 1 240); do
    kill -0 "$APP_PID" 2>/dev/null || break
    TOKEN=$(grep -m1 '^MCP: token ' "$LOG" 2>/dev/null | sed 's/^MCP: token //')
    BOUND=$(grep -m1 '^MCP: port '  "$LOG" 2>/dev/null | sed 's/^MCP: port //')
    [ -n "$TOKEN" ] && [ -n "$BOUND" ] && break
    sleep 0.5
done
if [ -z "$TOKEN" ] || [ -z "$BOUND" ]; then
    echo "play_select: the app never published an MCP token and port"
    tail -40 "$LOG"
    exit 1
fi
URL="http://127.0.0.1:${BOUND}/mcp"
ok "app is up on port $BOUND"

# run_script over MCP (the transport wraps twice; this unwraps both).
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
num() { printf '%s' "$1" | jq -r "if has(\"$2\") then .$2 else \"?\" end" 2>/dev/null || echo "?"; }
# jq floats: note the -r (a quoted "yes" takes the failing branch silently).
gt() { jq -rn --argjson a "$1" --argjson b "$2" 'if $a > $b then "yes" else "no" end'; }

# The app's MAIN window — the largest of the process's windows, like input_keys.
WIN=""; WIN_W=0; WIN_H=0
for _ in $(seq 1 40); do
    best=""; bestarea=0
    for w in $(xdotool search --pid "$APP_PID" 2>/dev/null); do
        g=$(xdotool getwindowgeometry --shell "$w" 2>/dev/null) || continue
        ww=$(printf '%s' "$g" | sed -n 's/^WIDTH=//p'); hh=$(printf '%s' "$g" | sed -n 's/^HEIGHT=//p')
        [ -n "${ww:-}" ] && [ -n "${hh:-}" ] || continue
        area=$((ww*hh))
        if [ "$area" -gt "$bestarea" ]; then best=$w; bestarea=$area; WIN_W=$ww; WIN_H=$hh; fi
    done
    if [ -n "$best" ] && [ "$bestarea" -gt 200000 ]; then WIN=$best; break; fi
    sleep 0.25
done
[ -n "$WIN" ] || { echo "play_select: no main window for pid $APP_PID"; tail -20 "$LOG"; exit 1; }
note "window $WIN (${WIN_W}x${WIN_H})"

# WM-less Xvfb: a CLICK is what makes Qt treat the window as active.
activate() {
    xdotool windowactivate --sync "$WIN" 2>/dev/null
    xdotool mousemove --window "$WIN" 40 400 click 1 2>/dev/null
    sleep 0.25
}
click() {   # $1,$2 = WINDOW pixels
    xdotool mousemove --window "$WIN" "$1" "$2"; sleep 0.25
    xdotool click 1; sleep 0.55
}
key() { xdotool key --clearmodifiers "$1"; sleep 0.45; }
drag() {   # $1..$4 = from-x, from-y, to-x, to-y, WINDOW pixels
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

activate
js 'project.create("play_select")' > /dev/null || { echo "$TAG: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "$TAG: no editor space"; exit 1; }
sleep 0.5

# ---- the scene, and the pixel the cube is under -----------------------------
#
# A cube at the origin, framed — so it sits at the middle of the VIEWPORT — and
# a sphere off to one side as the rigid body whose fall proves the run is
# really running. The click pixel is the viewport's own middle, converted to
# window pixels through editor.viewportState()'s windowX/windowY.
IDS=$(js 'var c = scene.addPrimitive("cube", {position:{x:0,y:1,z:0}});
          var s = scene.addPrimitive("sphere", {position:{x:4,y:8,z:0}});
          node.physics(s, {type:"rigidbody", shape:"sphere", mass:1});
          editor.select(c); editor.focusSelection(); editor.selectNone();
          JSON.stringify({cube:c, ball:s})') \
    || { echo "$TAG: could not build the scene"; exit 1; }
CUBE=$(num "$IDS" cube); BALL=$(num "$IDS" ball)
note "cube $CUBE  ball $BALL"

VP=$(js 'JSON.stringify(editor.viewportState())') || bad "editor.viewportState is callable"
VPX=$(num "$VP" windowX); VPY=$(num "$VP" windowY)
VPW=$(num "$VP" windowW); VPH=$(num "$VP" windowH)
note "viewport at ${VPX},${VPY} ${VPW}x${VPH} in the window"
if [ "$VPW" -lt 100 ] || [ "$VPH" -lt 100 ]; then
    echo "$TAG: the viewport reports no usable rect ($VP)"; exit 1
fi
# The viewport's middle, in window pixels.
CX=$((VPX + VPW/2)); CY=$((VPY + VPH/2))
note "click pixel ${CX},${CY}"

# The same pixel, asked of the app: whatever the click is about to select.
UNDER=$(js "JSON.stringify(editor.dropTargetAt($((VPW/2)), $((VPH/2)))||{})")
[ "$(num "$UNDER" id)" = "$CUBE" ] \
    && ok "the cube is what sits under that pixel (editor.dropTargetAt agrees)" \
    || bad "the framed cube is NOT under the click pixel ($UNDER) — the rest is meaningless"

# ---- the control: the same click in EDIT mode -------------------------------
click "$CX" "$CY"
SEL=$(js 'editor.selection() || ""')
[ "$SEL" = "$CUBE" ] \
    && ok "CONTROL: the click selects the cube while EDITING" \
    || bad "the click did not select in edit mode (got '$SEL') — the rig is wrong, not the code"
js 'editor.selectNone()' > /dev/null

# ##########################################################################
# SECTION 1 — A CLICK DURING PLAY SELECTS (the owner's request)
# ##########################################################################
PUSHES0=$(js 'JSON.stringify(editor.undoState())' | jq -r .pushes)
js 'editor.play()' > /dev/null || bad "editor.play()"
sleep 0.6
[ "$(js 'editor.playing()')" = "true" ] && ok "the scene is playing" || bad "the scene is not playing"
[ "$(js 'editor.playInputOwner()')" = "editor" ] \
    && ok "with nobody possessed the EDITOR owns the click" \
    || bad "the run claims the click with nobody possessed"

Y0=$(js "JSON.stringify(node.info('$BALL').position)" | jq -r .y)
click "$CX" "$CY"
SEL=$(js 'editor.selection() || ""')
Y1=$(js "JSON.stringify(node.info('$BALL').position)" | jq -r .y)
[ "$SEL" = "$CUBE" ] \
    && ok "A PLAIN LEFT CLICK DURING PLAY SELECTED THE CUBE (owner R13)" \
    || bad "the click during play selected '$SEL', not the cube"
[ "$(gt "$Y0" "$Y1")" = "yes" ] \
    && ok "and the simulation kept advancing across the click (the ball fell $Y0 -> $Y1)" \
    || bad "the ball did not fall across the click ($Y0 -> $Y1): the run is not running"
[ "$(js 'editor.playing()')" = "true" ] \
    && ok "the click did not stop the run" || bad "the run stopped on a click"

# ##########################################################################
# SECTION 2 — A GIZMO DRAG MOVES THE SELECTION LIVE
# ##########################################################################
#
# The translate gizmo is on the cube (it is selected). editor.gizmoHitTest
# answers what a pixel hits, in VIEWPORT coordinates, so the handle is FOUND
# rather than assumed: walk right from the cube's pixel until the X arrow
# answers, then drag from there.
js 'editor.setGizmoMode("translate")' > /dev/null
HANDLE_DX=""
for dx in 30 40 50 60 70 80 90 100 110 120 130 140; do
    H=$(js "JSON.stringify(editor.gizmoHitTest($((VPW/2 + dx)), $((VPH/2))))")
    [ "$(num "$H" handle)" = "x" ] && { HANDLE_DX=$dx; break; }
done
if [ -z "$HANDLE_DX" ]; then
    bad "no translate X handle answered anywhere along the cube's right — cannot drag"
else
    note "the X handle answers at +${HANDLE_DX} px"
    X0=$(js "JSON.stringify(node.info('$CUBE').position)" | jq -r .x)
    drag $((CX + HANDLE_DX)) "$CY" $((CX + HANDLE_DX + 150)) "$CY"
    X1=$(js "JSON.stringify(node.info('$CUBE').position)" | jq -r .x)
    note "the cube's x: $X0 -> $X1"
    [ "$(gt "$X1" "$X0")" = "yes" ] \
        && ok "THE GIZMO MOVED THE CUBE WHILE THE SCENE PLAYED" \
        || bad "the drag moved nothing during play ($X0 -> $X1)"
    [ "$(js 'editor.playing()')" = "true" ] \
        && ok "and the run is still running after the drag" || bad "the drag stopped the run"
    [ "$(js 'editor.selection()')" = "$CUBE" ] \
        && ok "the drag kept the selection" || bad "the drag lost the selection"
fi

# ##########################################################################
# SECTION 3 — THE EJECT KEY (ShortcutRegistry `play.eject`)
# ##########################################################################
#
# The KEYS are the run's until the user ejects: W is the Gameplay Move row
# while the run has the keyboard, and the translate tool once it does not. This
# is the observable half of the eject on a machine with no character to possess.
js 'editor.setGizmoMode("rotate")' > /dev/null
key w
MODE=$(js 'editor.gizmoMode()')
[ "$MODE" = "rotate" ] \
    && ok "W did NOT switch the tool while the run held the keyboard (it is the Move row)" \
    || bad "W reached the tool shortcut during play (mode '$MODE')"

key j                                   # the rebound play.eject
EJ=$(js 'editor.playEject()')
[ "$EJ" = "true" ] && ok "the eject key ejected (editor.playEject reads true)" \
                   || bad "the eject key did nothing (playEject '$EJ')"
[ "$(js 'editor.playing()')" = "true" ] \
    && ok "ejecting did NOT stop the run" || bad "ejecting stopped the run"

key w
MODE=$(js 'editor.gizmoMode()')
[ "$MODE" = "translate" ] \
    && ok "EJECTED: W is the translate tool again while the scene keeps playing" \
    || bad "W still did not reach the tool shortcut after ejecting (mode '$MODE')"

key j
EJ=$(js 'editor.playEject()')
[ "$EJ" = "false" ] && ok "the key toggles back (the run has the keyboard again)" \
                    || bad "the eject key did not toggle back (playEject '$EJ')"
js 'editor.setGizmoMode("rotate")' > /dev/null
key w
MODE=$(js 'editor.gizmoMode()')
[ "$MODE" = "rotate" ] \
    && ok "...and W is the Move row once more" \
    || bad "W still reaches the tool after un-ejecting (mode '$MODE')"

# ##########################################################################
# SECTION 4 — STOP RESTORES THE DOCUMENT AND KEEPS THE SELECTION
# ##########################################################################
js 'editor.stop()' > /dev/null
sleep 0.4
XEND=$(js "JSON.stringify(node.info('$CUBE').position)" | jq -r .x)
SEL=$(js 'editor.selection() || ""')
YEND=$(js "JSON.stringify(node.info('$BALL').position)" | jq -r .y)
note "after stop: cube x=$XEND  ball y=$YEND  selection=$SEL"
[ "$(jq -rn --argjson a "$XEND" 'if ($a|fabs) < 0.001 then "yes" else "no" end')" = "yes" ] \
    && ok "the cube is back at the origin — the run's edits were thrown away" \
    || bad "the cube stayed at x=$XEND after Stop"
[ "$(jq -rn --argjson a "$YEND" 'if ($a > 7.99 and $a < 8.01) then "yes" else "no" end')" = "yes" ] \
    && ok "the ball is back where Play found it" \
    || bad "the ball did not go back (y=$YEND)"
[ "$SEL" = "$CUBE" ] \
    && ok "THE SELECTION SURVIVED THE RUN" || bad "the selection was lost at Stop (got '$SEL')"
[ "$(js 'editor.playEject()')" = "false" ] \
    && ok "Stop cleared the eject latch" || bad "the eject latch survived Stop"
[ "$(js 'editor.playInputOwner()')" = "editor" ] \
    && ok "and the editor owns the pointer with nothing playing" \
    || bad "playInputOwner is wrong outside play"

# AN UNDO STEP FOR A PLAY-TIME DRAG WOULD REWIND THE CUBE TO A POSE THE RESTORE
# HAS ALREADY THROWN AWAY, so the run must have pushed none. `pushes` is the
# process's lifetime count of commands that reached the undo sink, so this is
# the whole run — the drag included — measured against the moment before Play.
UNDO=$(js 'JSON.stringify(editor.undoState())')
PUSHES1=$(printf '%s' "$UNDO" | jq -r .pushes)
note "undo after the run: $UNDO"
[ "$PUSHES0" = "$PUSHES1" ] \
    && ok "the run pushed NO undo command ($PUSHES0 before Play, $PUSHES1 after Stop)" \
    || bad "the run left $((PUSHES1 - PUSHES0)) undo command(s) behind for edits Stop discarded"

echo
if [ "$fail" -eq 0 ]; then echo "$TAG: ALL SECTIONS PASSED"; else echo "$TAG: FAILURES ABOVE"; fi
exit "$fail"
