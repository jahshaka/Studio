#!/usr/bin/env bash
#
# app.navigation_keys — THE EDITOR FLIES ON THE ARROW CLUSTER, AND THE LETTERS
# ARE FREE (owner decision 2026-09-09).
#
# input.fly_controls already pins the MOVEMENT MATHS of both free cameras
# headless, to the last float, and it is the stronger test of that half. What
# it cannot see is the half that only exists in a live Qt window:
#
#   * WHETHER THE KEY ARRIVES. The arrow keys are Qt::WindowShortcut material
#     all over the app (list navigation, the hierarchy tree), so the fly only
#     gets them because EngineSceneViewport accepts their ShortcutOverride
#     while the right button is held. A fly that moves correctly in a unit test
#     and never receives a key in the app is exactly the bug this suite exists
#     to catch.
#
#   * WHETHER THE LETTER IS ACTUALLY FREE. Freeing W/A/S/D/Q/E was the POINT of
#     the change, and "free" means the window-wide tool shortcut on W fires
#     while the right mouse button is down — the case that used to be swallowed
#     by the fly's own ShortcutOverride claim. Nothing but a real window has a
#     shortcut map to ask.
#
# WM-less Xvfb facts this depends on (CLAUDE.md): the window must be CLICKED
# before Qt treats it as active (Qt::WindowShortcut needs an active window),
# and letters and arrows arrive clean (unlike F-keys, which arrive as Alt+F*).
#
# usage: navigation_keys.sh <jahshaka-binary> <mcp-port, 0 for ephemeral>
# cwd is a scratch run dir (ctest sets it); HOME is a scratch home.
set -u

BIN="$1"
PORT="$2"
LOG="$PWD/app.log"

fail=0
ok()   { echo "navigation_keys: ok — $1"; }
bad()  { echo "navigation_keys: FAIL — $1"; fail=1; }
note() { echo "navigation_keys: .. $1"; }

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
    command -v "$tool" > /dev/null 2>&1 || { echo "navigation_keys: $tool missing"; exit 1; }
done

# ------------------------------------------------------- its own display -----
# Same rule as app.pacing_undo and app.multiselect_keys: this suite synthesises
# input, so it must never honour JAH_TEST_DISPLAY (which defaults to a
# developer's live session).
DISP=""
for n in $(seq 171 210); do
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
[ -n "$DISP" ] || { echo "navigation_keys: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

"$BIN" --mcp-port="$PORT" > "$LOG" 2>&1 &
APP_PID=$!

# THE PORT IS READ BACK, NOT ASSUMED (TEST_GATE_AUDIT.md §4.1). ctest passes
# 0, the app binds an ephemeral port and prints it beside the token; two suites
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
    echo "navigation_keys: the app never published an MCP token and port"
    tail -40 "$LOG"
    exit 1
fi
URL="http://127.0.0.1:${BOUND}/mcp"
ok "app is up on port $BOUND"

js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",
          params:{name:"run_script",arguments:{script:$s,label:"navigation_keys"}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
        -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    if [ -z "$inner" ]; then
        echo "navigation_keys: (transport) ${response:-<no response>}" >&2
        return 1
    fi
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "navigation_keys: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2
        return 1
    fi
    printf '%s' "$inner" | jq -r '.result'
}

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
    if [ -n "$best" ] && [ "$bestarea" -gt 200000 ]; then WIN=$best; break; fi
    sleep 0.25
done
[ -n "$WIN" ] || { echo "navigation_keys: no main window for pid $APP_PID"; tail -20 "$LOG"; exit 1; }
note "window $WIN (${WIN_W}x${WIN_H})"

activate() {
    xdotool windowactivate --sync "$WIN" 2>/dev/null
    xdotool mousemove --window "$WIN" 40 400 click 1 2>/dev/null
    sleep 0.25
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
# which is exactly how this suite first reported four failures against a run
# whose own trace showed the camera moving 7.8 units (2026-09-09).
dist() { jq -n --argjson a "$1" --argjson b "$2" '(($a.x-$b.x)*($a.x-$b.x)+($a.y-$b.y)*($a.y-$b.y)+($a.z-$b.z)*($a.z-$b.z)) | sqrt'; }
gt()   { jq -rn --argjson a "$1" --argjson b "$2" 'if $a > $b then "yes" else "no" end'; }

activate
js 'project.create("navigation_keys")' > /dev/null || { echo "navigation_keys: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "navigation_keys: no editor space"; exit 1; }
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

if [ "$fail" -eq 0 ]; then echo "navigation_keys: ALL PASS"; else echo "navigation_keys: FAILURES"; fi
exit "$fail"
