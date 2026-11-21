#!/bin/bash
# source.display_guard - NO TOOL BOOTS ON A DISPLAY THAT IS NOT A RIG (URGENT #195, 2026-11-21): an agent's shell inherits
# DISPLAY=:0, the OWNER's, and warm-app-cache.sh booted jahlab there ("booting ... jahlab on :0"). CPU only: fakes for the
# app, jahlab and Xvfb, a private lock, a scratch tree; nothing connects to any X server and the card is never taken.
#   (1) the one definition: scripts/display-guard.sh check - :60-:99 (a screen suffix allowed) a rig, empty 1, all else 2;
#   (2) THE CENSUS: every file under scripts/ and tests/support/ that USES DISPLAY in code is in the table below with its
#       class, and the class's own check holds (guard: it sources or calls display-guard.sh; own: its own 60-99 test; unset:
#       it runs with `-u DISPLAY`); a new user of DISPLAY outside the table is red until it is classed;
#   (3) warm-app-cache.sh under DISPLAY=:0: REFUSED, exit 2, the safe line (env -u DISPLAY <abs path> ...) printed, nothing
#       booted; with --rig the tree's rig is taken (a fake Xvfb) and the boot is handed THAT display, never :0;
#   (4) gpu-exclusive.sh: a command under the lock sees DISPLAY=:0 UNSET (said on one line), a rig display kept;
#   (5) fresh_home.sh under DISPLAY=:0: REFUSED, exit 2, its command never run; with no DISPLAY the command runs.
# usage: display_guard.sh <source dir> <build dir>
set -u
SRC=${1:?source dir}; BUILD=${2:?build dir}
T=${KEEP_T:-$(mktemp -d "${TMPDIR:-/tmp}/display-guard.XXXXXX")}; [ -n "${KEEP_T:-}" ] || trap 'rm -rf "$T"' EXIT   # (KEEP_T=<dir>: a hand debug run keeps its toys)
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
G="$SRC/scripts/display-guard.sh"
# (1)
got=""; for v in :60 :99 :99.0 :61.1 "" :0 :0.0 :1 :59 :100 localhost:10.0 :fake; do bash "$G" check "$v"; got="$got$?"; done
want "$([ "$got" = 000012222222 ] && echo 1)" "(1) check: :60 :99 :99.0 :61.1 a rig (0), empty 1, :0 :0.0 :1 :59 :100 localhost:10.0 :fake refused (2) - got $got"
# (2) THE CENSUS (file: class). guard = sources/calls display-guard.sh; own = its own 60-99 test; unset = runs with -u DISPLAY;
# maker = rig.sh (it MAKES the rig display); reads = records or prints it, boots nothing; desktop = the OWNER's smoke on his
# own display by design (scripts/lab/smoke.sh, the lead's ruling pending, URGENT #195); note = a comment only
read -r -d '' TABLE <<'TAB'
scripts/display-guard.sh def
scripts/warm-app-cache.sh guard
scripts/gpu-exclusive.sh guard
scripts/row-env.sh guard
scripts/sanitize.sh guard
scripts/shadercache/shadercache-bench.sh guard
scripts/threading/texture-ab.sh guard
scripts/threading/warmup-ab.sh guard
tests/support/fresh_home.sh guard
scripts/perf-ab.py own
scripts/width_pair.py own
scripts/lab/dolly.sh own
scripts/lab/warm.sh own
scripts/regen-api-docs.sh unset
tests/support/seed_home.sh unset
scripts/rig.sh maker
scripts/gate_runlog.py reads
scripts/lab/smoke.sh desktop
tests/support/documentgraph.h note
TAB
users=$(cd "$SRC" && grep -rlE '\$\{?DISPLAY\b|\bDISPLAY=|["'"'"']DISPLAY["'"'"']|-u DISPLAY' scripts tests/support | grep -v '\.md$' | LC_ALL=C sort)
listed=$(printf '%s\n' "$TABLE" | awk 'NF{print $1}' | LC_ALL=C sort)
new=$(comm -23 <(printf '%s\n' "$users") <(printf '%s\n' "$listed")); gone=$(comm -13 <(printf '%s\n' "$users") <(printf '%s\n' "$listed"))
want "$([ -z "$new" ] && [ -z "$gone" ] && echo 1)" "(2) the census: $(printf '%s\n' "$users" | wc -l) users of DISPLAY, each classed (unclassed: [${new//$'\n'/ }]; classed but no longer a user: [${gone//$'\n'/ }])"
badc=""
while read -r f c; do
    [ -n "$f" ] || continue; p="$SRC/$f"
    case "$c" in
        guard) grep -q 'display-guard\.sh' "$p" || badc="$badc $f(no display-guard.sh)" ;;
        own) grep -qE '6\[0-9\]|\[6-9\]\[0-9\]|60 <= ' "$p" || badc="$badc $f(no 60-99 test)" ;;
        unset) grep -q -- '-u DISPLAY' "$p" || badc="$badc $f(no -u DISPLAY)" ;;
    esac
done <<< "$TABLE"
want "$([ -z "$badc" ] && echo 1)" "(2) each class's check holds (guard: display-guard.sh named; own: a 60-99 test; unset: -u DISPLAY)${badc:+ - RED:$badc}"
# (3) warm-app-cache.sh: fakes that write the DISPLAY they were handed; a scratch tree whose scripts are the real ones
mkdir -p "$T/rt/scripts" "$T/x" "$T/bin" "$T/tests"; ln -s "$SRC"/scripts/* "$T/rt/scripts/"; ln -s "$SRC/tests" "$T/rt/tests"
printf '#!/bin/sh\necho "DISPLAY=${DISPLAY:-none}" >> %s/seen\nexit 0\n#lab\n' "$T" > "$T/lab"; cp "$T/lab" "$T/app"; chmod +x "$T/lab" "$T/app"
: > "$T/cold-none"
printf '%s\n' '#!/bin/bash' 'n=${1#:}; d=${JAH_RIG_XDIR:?}' \
    '[ -e "$d/.X$n-lock" ] && { echo "Server is already active for display $n" >&2; exit 1; }' \
    'printf "%10d\n" $$ > "$d/.X$n-lock"; mkdir -p "$d/.X11-unix"; : > "$d/.X11-unix/X$n"' \
    'trap '"'"'rm -f "$d/.X$n-lock" "$d/.X11-unix/X$n"; exit 0'"'"' TERM' 'for i in $(seq 300); do sleep 0.2; done' > "$T/bin/Xvfb"
chmod +x "$T/bin/Xvfb"
W=(--tests-dir "$T/tests" --app "$T/app" --lab "$T/lab" --cold-rows "$T/cold-none" --only engine-off)
out=$(DISPLAY=:0 JAH_GPU_LOCK_HELD=1 HOME="$T" "$T/rt/scripts/warm-app-cache.sh" "$BUILD" "${W[@]}" 2>&1); rc=$?
want "$([ $rc = 2 ] && [ ! -e "$T/seen" ] && grep -q "REFUSED (exit 2): DISPLAY=:0 is not a rig display" <<< "$out" \
        && grep -q "env -u DISPLAY $T/rt/scripts/warm-app-cache.sh $BUILD --tests-dir" <<< "$out" && echo 1)" \
     "(3) warm-app-cache.sh under DISPLAY=:0: exit $rc, nothing booted, the safe line printed ($(grep -m1 REFUSED <<< "$out" | cut -c1-120))"
out=$(DISPLAY=:0 JAH_GPU_LOCK_HELD=1 HOME="$T" PATH="$T/bin:$PATH" JAH_RIG_XDIR="$T/x" JAH_CPUS=0 \
      "$T/rt/scripts/warm-app-cache.sh" "$BUILD" "${W[@]}" --rig 2>&1); rc=$?
seen=$(cat "$T/seen" 2>/dev/null | tr '\n' ' ')
want "$(grep -qE '^DISPLAY=:(6[0-9]|[7-9][0-9]) $' <<< "$seen" && grep -q -- '--rig: the tree.s rig is used instead' <<< "$out" \
        && [ ! -e "$T/rt/rig-xvfb.pid" ] && echo 1)" \
     "(3) --rig under DISPLAY=:0: the boot handed the tree's rig [$seen] (exit $rc: the fake lab grows no cache), the rig down after"
# (4) gpu-exclusive.sh on a private lock: what its command sees
pd() { "$SRC/scripts/gpu-exclusive.sh" --label display-guard sh -c 'echo "${DISPLAY:-none}"' 2> "$T/g.err"; }
a=$(DISPLAY=:0 HOME="$T" JAH_GPU_LOCK="$T/lock" pd); b=$(DISPLAY=:61 HOME="$T" JAH_GPU_LOCK="$T/lock" pd)
want "$([ "$a" = none ] && [ "$b" = :61 ] && echo 1)" "(4) under the lock DISPLAY=:0 reaches the command as [$a] (unset), :61 as [$b]"
DISPLAY=:0 HOME="$T" JAH_GPU_LOCK="$T/lock" pd > /dev/null
want "$(grep -q 'gpu-lock: DISPLAY=:0 is not a rig display (60-99; :0 is the OWNER.s) - unset for this command' "$T/g.err" && echo 1)" \
     "(4) ...and the lock says so on one line"
# (5) fresh_home.sh: a home four deep, no build above it (no shared cache)
H="$T/h/o/m/e"; mkdir -p "$H"
DISPLAY=:0 sh "$SRC/tests/support/fresh_home.sh" "$H" -- sh -c "touch '$T/ran0'" > "$T/f.out" 2>&1; rc=$?
want "$([ $rc = 2 ] && [ ! -e "$T/ran0" ] && grep -q 'fresh_home.sh: REFUSED (exit 2): DISPLAY=:0 is not a rig display' "$T/f.out" && echo 1)" \
     "(5) fresh_home.sh under DISPLAY=:0: exit $rc, its command not run"
env -u DISPLAY sh "$SRC/tests/support/fresh_home.sh" "$H" -- sh -c "touch '$T/ran1'" > "$T/f1.out" 2>&1; rc=$?
want "$([ $rc = 0 ] && [ -e "$T/ran1" ] && echo 1)" "(5) ...with no DISPLAY its command runs (exit $rc)"
[ "$fails" = 0 ] && { echo "source.display_guard: PASSED"; exit 0; }
echo "source.display_guard: $fails FAILED"; exit 1
