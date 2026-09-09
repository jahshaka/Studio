#!/usr/bin/env bash
#
# app.multiselect_keys — THE EDIT CHORDS, AS REAL KEYS, IN BOTH SPACES
# (EDITOR_MULTISELECT_SPEC §2.6 / §5).
#
# Why this shape, and why nothing smaller will do:
#
#   * SHORTCUT AMBIGUITY IS A PROPERTY OF QT'S SHORTCUT MAP, not of any
#     function. Delete / Ctrl+D / Ctrl+C / Ctrl+V used to be bare
#     WindowShortcuts inside the Materials graph view; the editor needs the same
#     four chords for the selection SET, and TWO WindowShortcut claimants make
#     Qt drop the chord entirely (that is how Ctrl+Z came to do nothing on the
#     Materials page — deep audit 2026-09 area 1). The fix is one registry
#     claimant routed by active space, and the only way to gate it is to press
#     the keys in a live window and look at what happened in BOTH spaces.
#
#   * ONE UNDO STEP FOR N OBJECTS is likewise unobservable from a --script run:
#     a run is itself one open macro and QUndoStack refuses to undo into one
#     (scripting.e2e.multiselect_edit's header says so). Pressing Delete and
#     then Ctrl+Z as REAL KEYS has no script macro anywhere, so "both objects
#     come back on ONE undo" is a real assertion here and only here.
#
#   * The tree's inline RENAME EDITOR is the case where a WindowShortcut must
#     NOT fire: a QLineEdit accepts the ShortcutOverride for Ctrl+C/Ctrl+V.
#     That is a Qt contract we are relying on, so it is probed rather than
#     assumed (EDITOR_MULTISELECT_SPEC §7).
#
# WM-less Xvfb facts this depends on (CLAUDE.md): the window must be CLICKED
# before Qt treats it as active (Qt::WindowShortcut needs an active window), and
# letters and modified chords arrive clean (unlike F-keys, which arrive as Alt+F*).
#
# usage: multiselect_keys.sh <jahshaka-binary> <port>
# cwd is a scratch run dir (ctest sets it); HOME is a scratch home.
set -u

BIN="$1"
PORT="$2"
URL="http://127.0.0.1:${PORT}/mcp"
LOG="$PWD/app.log"

fail=0
ok()   { echo "multiselect_keys: ok — $1"; }
bad()  { echo "multiselect_keys: FAIL — $1"; fail=1; }
note() { echo "multiselect_keys: .. $1"; }

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
    command -v "$tool" > /dev/null 2>&1 || { echo "multiselect_keys: $tool missing"; exit 1; }
done

# ------------------------------------------------------- its own display -----
# Same rule as app.pacing_undo: this suite synthesises input, so it must never
# honour JAH_TEST_DISPLAY (which defaults to a developer's live session).
DISP=""
for n in $(seq 131 170); do
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
[ -n "$DISP" ] || { echo "multiselect_keys: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

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
    echo "multiselect_keys: the app never published an MCP token"
    tail -40 "$LOG"
    exit 1
fi
ok "app is up on port $PORT"

js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",
          params:{name:"run_script",arguments:{script:$s,label:"multiselect_keys"}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" \
        -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    if [ -z "$inner" ]; then
        echo "multiselect_keys: (transport) ${response:-<no response>}" >&2
        return 1
    fi
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "multiselect_keys: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2
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
[ -n "$WIN" ] || { echo "multiselect_keys: no main window for pid $APP_PID"; tail -20 "$LOG"; exit 1; }
note "window $WIN (${WIN_W}x${WIN_H})"

activate() {
    xdotool windowactivate --sync "$WIN" 2>/dev/null
    xdotool mousemove --window "$WIN" 40 400 click 1 2>/dev/null
    sleep 0.25
}
key() { xdotool key --clearmodifiers "$1"; sleep 0.45; }

activate
js 'project.create("multiselect_keys")' > /dev/null || { echo "multiselect_keys: no project"; exit 1; }
js 'app.space("editor")' > /dev/null || { echo "multiselect_keys: no editor space"; exit 1; }
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
if grep -q "activated ambiguously" "$LOG"; then
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
# Ctrl+` opens the script console dock, whose input line is a text field; a
# click lands the focus in it. NOTE-ONLY: the panel layout decides where that
# line sits, so a miss must not fail the suite — the finding is recorded and
# the Qt contract (a QLineEdit accepts the ShortcutOverride for Ctrl+C/V) is
# what is being probed, not something this lane implements.
key ctrl+grave
sleep 0.5
xdotool mousemove --window "$WIN" $((WIN_W*55/100)) $((WIN_H*95/100)) click 1 2>/dev/null
sleep 0.4
key ctrl+v
AFTER=$(js 'scene.nodes().length')
[ "$AFTER" = "$BEFORE" ] \
    && ok "Ctrl+V with a text field focused did not paste into the scene" \
    || note "Ctrl+V pasted while a text field was focused ($BEFORE -> $AFTER) — recorded, not gated"

echo "multiselect_keys: done (fail=$fail)"
exit "$fail"
