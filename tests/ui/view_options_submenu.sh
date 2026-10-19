#!/usr/bin/env bash
#
# ui.view_options_submenu — THE VIEW OPTIONS SUBMENUS POP EVERY TIME (ATOM-MENU-POPUP-1).
#
# The owner's smoke, 2026-10-19: after one use of View Options > Atom View the submenu stopped
# popping until another submenu was hovered. The cause (gdb trace, SPECS/ATOM-MENU-POPUP-1.md):
# Qt's menu fade effect, which Qlementine switches on app-wide, FAKES a menu shown and shows it
# only when its animation ends. A click on a submenu row before it opened pops the submenu from
# the press and again from the 20 ms timer the press starts; the second popup kills the running
# effect and its own show() is a no-op on a menu that already reads "not hidden" — the submenu is
# stranded, never on screen, and every later hover of its row asks for nothing. The theme turns
# Qt's menu effects off (JahQlementineStyle::polish(QApplication*)).
#
# WHAT IT DRIVES, for Atom View (Triangles, Off) and Photon View (Voxels, Off), with REAL pointer
# input (XTEST through xdotool) at coordinates the app reports (app.widget, app.menus):
#   1. open View Options, HOVER the submenu's row: the submenu is on screen; click the mode; the
#      view is that mode;
#   2. open View Options, CLICK the submenu's row the moment the pointer reaches it (a person's
#      click, before the popup delay): the submenu is on screen — the stranding this row guards;
#      click Off; the view is off;
#   3. open View Options, HOVER the row once, no other submenu touched: the submenu is on screen.
# "On screen" is app.menus() listing the menu — Qt's own visibility of the popup.
#
# WAITS ARE EVENT-LOOP TURNS, NEVER A CLOCK'S VERDICT: a wait polls app.menus() (each poll is one
# request the app's event loop answers) until the menu is listed, bounded by POLLS. Qt's popup
# delay is the style's 300 ms timer; a stranded submenu never appears however long the poll runs.
#
# ITS OWN DISPLAY (as app.input_keys): it synthesises pointer input, which the X server delivers
# to whatever window is under the pointer — a private Xvfb is the only safe place to do that.
#
# usage: view_options_submenu.sh <jahshaka-binary>     (cwd: the scratch run dir)
set -u

BIN="$1"
LOG="$PWD/app.log"
TAG="view_options_submenu"
POLLS=60
fail=0
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

for tool in xdotool curl jq Xvfb xdpyinfo; do
    command -v "$tool" > /dev/null 2>&1 || { echo "$TAG: $tool missing"; exit 1; }
done

# ------------------------------------------------------------- its own display -----
DISP=""
for n in $(seq 100 199); do
    [ -e "/tmp/.X11-unix/X${n}" ] || [ -e "/tmp/.X${n}-lock" ] && continue
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
[ -n "$DISP" ] || { echo "$TAG: could not start an Xvfb of my own"; exit 1; }
export DISPLAY="$DISP"
export QT_QPA_PLATFORM=xcb
note "own display $DISP (pid $XVFB_PID)"

# ----------------------------------------------------------------------- boot -------
"$BIN" --mcp-port=0 > "$LOG" 2>&1 &
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
    echo "$TAG: the app never published an MCP token and port"; tail -40 "$LOG"; exit 1
fi
URL="http://127.0.0.1:${BOUND}/mcp"

# run_script over MCP; echoes the script's value (a JSON.stringify comes back as JSON text).
js() {
    local payload response inner
    payload=$(jq -n --arg s "$1" --arg l "$TAG" \
        '{jsonrpc:"2.0",id:1,method:"tools/call",params:{name:"run_script",arguments:{script:$s,label:$l}}}')
    response=$(curl -s --max-time 60 -X POST "$URL" \
        -H "Authorization: Bearer $TOKEN" -H "Content-Type: application/json" -d "$payload")
    inner=$(printf '%s' "$response" | jq -r '.result.content[0].text // empty' 2>/dev/null)
    [ -n "$inner" ] || { echo "$TAG: (transport) ${response:-<no response>}" >&2; return 1; }
    if [ "$(printf '%s' "$inner" | jq -r '.ok')" != "true" ]; then
        echo "$TAG: (script) $(printf '%s' "$inner" | jq -r '.error')" >&2; return 1
    fi
    printf '%s' "$inner" | jq -r '.result'
}

js 'project.create("View Options " + Date.now()); editor.frame(2); true' > /dev/null \
    || { echo "$TAG: could not open a scene"; tail -20 "$LOG"; exit 1; }
ok "app is up on port $BOUND, a new scene open"

# ---------------------------------------------------------------- the gestures ------
# The centre of the row labelled $2 in the menu titled $1 ('' = View Options' own menu), as
# "x y" in screen pixels; empty when no such menu is on screen.
row_at() {
    js "(function () {
          var ms = app.menus();
          for (var i = 0; i < ms.length; ++i) {
            if (ms[i].title !== '$1') continue;
            for (var j = 0; j < ms[i].rows.length; ++j) {
              var r = ms[i].rows[j];
              if (r.text === '$2' || r.submenu === '$2')
                return Math.round(r.x + r.width / 2) + ' ' + Math.round(r.y + r.height / 2);
            }
          }
          return '';
        })()"
}
# Polls until the menu titled $1 is on screen (0) or POLLS requests found it nowhere (1).
wait_menu() {
    local i
    for i in $(seq 1 "$POLLS"); do
        [ "$(js "String(app.menus().some(function (m) { return m.title === '$1'; }))")" = "true" ] && return 0
        sleep 0.05
    done
    return 1
}
# Polls until no menu is on screen.
wait_closed() {
    local i
    for i in $(seq 1 "$POLLS"); do
        [ "$(js 'String(app.menus().length)')" = "0" ] && return 0
        sleep 0.05
    done
    return 1
}
close_all() { xdotool key Escape; sleep 0.1; xdotool key Escape; wait_closed; }

# Opens View Options with a click on its button and returns the row of submenu $1, "x y".
open_view_options() {
    local b bx by
    b=$(js "(function () { var w = app.widget('viewOptionsButton');
              return w.visible ? Math.round(w.x + w.width / 2) + ' ' + Math.round(w.y + w.height / 2) : ''; })()")
    [ -n "$b" ] || { bad "the View Options button is not on screen"; return 1; }
    read -r bx by <<< "$b"
    xdotool mousemove "$bx" "$by" click 1
    wait_menu '' || { bad "View Options did not open"; return 1; }
    OPEN_X=$bx; OPEN_Y=$by
}

# A pointer that TRAVELS from (x1,y1) to (x2,y2) in 12-pixel steps — the motion a hand makes,
# every step its own MotionNotify.
travel() {
    local x1=$1 y1=$2 x2=$3 y2=$4 n i
    n=$(( ( (x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1) ) ))
    n=$(awk -v d="$n" 'BEGIN { s = int(sqrt(d) / 12); print (s < 1 ? 1 : s) }')
    for i in $(seq 1 "$n"); do
        xdotool mousemove $(( x1 + (x2 - x1) * i / n )) $(( y1 + (y2 - y1) * i / n ))
    done
}

# Triggers mode $3 of submenu $1 from its row at ($4,$5) and asserts world.$2() reads $6.
pick() {
    local sub="$1" verb="$2" label="$3" sx="$4" sy="$5" want="$6" r rx ry
    r=$(row_at "$sub" "$label")
    [ -n "$r" ] || { bad "$sub: no '$label' row on screen"; close_all; return 1; }
    read -r rx ry <<< "$r"
    travel "$sx" "$sy" "$rx" "$sy"           # along the row into the submenu, then to the mode
    travel "$rx" "$sy" "$rx" "$ry"
    xdotool click 1
    wait_closed || bad "$sub > $label: the menu did not close on the trigger"
    local got; got=$(js "world.$verb()")
    [ "$got" = "$want" ] && ok "$sub > $label: world.$verb() is '$got'" \
        || bad "$sub > $label: world.$verb() is '$got', not '$want'"
}

# One submenu's three gestures.
drive() {
    local sub="$1" verb="$2" mode="$3" modeView="$4" r sx sy
    # 1. hover, then the mode
    open_view_options || return
    r=$(row_at '' "$sub"); read -r sx sy <<< "$r"
    travel "$OPEN_X" "$OPEN_Y" "$sx" "$sy"
    if wait_menu "$sub"; then ok "$sub: the submenu pops on a hover"
    else bad "$sub: the submenu did not pop on a hover"; close_all; return; fi
    pick "$sub" "$verb" "$mode" "$sx" "$sy" "$modeView"

    # 2. a CLICK on the submenu's row the moment the pointer reaches it: the pointer goes down
    #    OUTSIDE the menu (left of it) to the row's height, then one xdotool request moves it
    #    onto the row and presses — the press arrives inside the 300 ms popup delay
    open_view_options || return
    local mx; mx=$(js "String(app.menus().filter(function (m) { return m.title === ''; })[0].x - 40)")
    travel "$OPEN_X" "$OPEN_Y" "$mx" "$OPEN_Y"
    travel "$mx" "$OPEN_Y" "$mx" "$sy"
    xdotool mousemove "$sx" "$sy" click 1
    if wait_menu "$sub"; then ok "$sub: the submenu pops when its row is clicked"
    else bad "$sub: the submenu did not pop when its row was clicked (stranded)"; close_all; return; fi
    pick "$sub" "$verb" "Off" "$sx" "$sy" "off"

    # 3. after both uses, the FIRST hover of the row, no other submenu touched
    open_view_options || return
    travel "$OPEN_X" "$OPEN_Y" "$sx" "$sy"
    if wait_menu "$sub"; then ok "$sub: after its use the submenu pops on the first hover"
    else bad "$sub: after its use the submenu did not pop on the first hover"; fi
    close_all
}

drive "Atom View" atomView "Triangles" "triangles"
drive "Photon View" photonView "Voxels" "voxels"

note "frames rendered: $(js 'String(app.frameStats().rendered)')"
js 'app.quit()' > /dev/null 2>&1   # its reply may race the window's close
for _ in $(seq 1 100); do kill -0 "$APP_PID" 2>/dev/null || break; sleep 0.1; done
[ "$fail" -eq 0 ] && echo "$TAG: PASS" || { echo "$TAG: FAILED"; grep -E 'warning|error' "$LOG" | tail -10; }
exit $fail
