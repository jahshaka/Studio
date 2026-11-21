#!/usr/bin/env bash
# gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] [--short <s>] <command> [args...] — run <command> holding
# THE CARD: the one
# lock on the GPU (DOCS/PROCESS.md; TESTING-SIMPLE). An exclusive flock on /tmp/jah-gpu.lock (JAH_GPU_LOCK overrides),
# waited for with one `gpu-lock: waiting …` line and one `gpu-lock: waited <s> s` line; the command runs with
# JAH_GPU_LOCK_HELD=1, so a row that takes the lock itself under a holder that has it (spec.sh test / merge and
# sprint.sh ship hold it around their ctest) runs at once. Inside one ctest the width per class is ctest's own
# (tests/cmake/vram_rows.cmake: the card's 8 slots). Every app or GPU binary run by hand outside ctest, and every
# measurement, runs through this. --run-timeout: the command's own bound, counted after the wait (timeout(1)).
# --wait-max <s>: wait for the card at most <s> seconds, else exit 75 with one `gpu-lock: the card stayed held …` line and
# nothing run (URGENT FIX LIST #26: a caller with a foreground limit bounds its whole call; it is still the lock's one wait).
# FIRST COME, FIRST SERVED (URGENT #113, 2026-10-29: 8B's --part 4/4 exited 75 three times in a row, 16 min lost, three
# later holds taken ahead of it). Every waiter takes a TICKET first: a numbered file under <lock>.q/ (the number from
# <lock>.q/counter, taken and the ticket written under that directory's own one-line flock) holding
# "<pid> <its start time, /proc/<pid>/stat field 22> <label>" - THE PROTOCOL every holder of the card speaks (the lead's
# util.CardLock too: a holder outside it is served first by the kernel's blocked-flock order); it waits (0.5 s polls)
# until the lowest ticket whose pid is alive is its own, THEN tries the main flock (0.2 s polls, the queue re-read between
# tries, within what is left of --wait-max), and removes its ticket once it holds the card (and on any exit: the trap). A ticket whose pid is dead is skipped and removed
# by whoever sees it. The main flock stays the mutual exclusion; a holder that bypasses the queue (an older copy of this
# script, a bare flock) still excludes, it only is not ordered.
# A BOUNDED WAITER KEEPS ITS PLACE (measured 2026-10-29: blocked flock waiters were served in arrival order 4 runs of 4 -
# the loss was the exit 75 itself: each call that ran out of --wait-max left the line, and its re-call queued behind
# every hold that arrived meanwhile): on exit 75 the ticket stays RESERVED for JAH_GPU_RESERVE_S seconds (default 120: an
# agent's turn between the 75 and its re-call) under the caller's label, and the next call with the SAME label adopts it -
# its place ahead of every later arrival, not a new number. A reservation never idles the card: the queue passes over it
# while its caller is away, and removes it once expired.
# AN UNTICKETED HOLDER IS NAMED (URGENT #141, 2026-11-02: the Unreal session's holds ran the MAIN clone's older copy of this
# script, which takes no ticket, and so jumped every ticketed waiter - a te139 wait ran out at 600 s behind them). A holder
# that took its ticket writes its holder line ending in ` ticketed` (util.CardLock likewise); a waiter that finds a LIVE
# holder whose line lacks it prints ONE loud line naming it - "gpu-lock: AN UNTICKETED HOLDER: pid <pid> <label> - an old
# gpu-exclusive.sh copy or a bare flock ..." - and appends the same line, stamped, to <lock>.log (the lead's to read).
# It never kills, never waits less: the flock stays the mutual exclusion.
# THE HOLD's PROCESSES DIE WITH THE HOLD (URGENT #144, 2026-11-02: the Unreal session's hold ue-6c2-c1v hit its 540-s bound,
# timeout(1) killed its shell, and its UnrealEditor take - another process group, reparented to the user manager - KEPT
# RUNNING ON THE CARD after the lock was released: a bound or a process group never held a hold's descendants). The held
# command runs in ITS OWN TRANSIENT CGROUP, `systemd-run --user --scope --unit=jahgpu-<ticket>` (the user manager: no
# password, ~15 ms; the kernel puts every descendant there, setsid or reparented alike, and no process can leave it but by
# a cgroup write of its own; --expand-environment=no: systemd-run otherwise rewrites `$$` and `${VAR}` in the command's
# own arguments - the lead's lock_test caught `pid=$$` arriving as `pid=$`). At the command's end - its normal exit or its bound - every process still in the cgroup is
# sent TERM, KILL 10 s later (JAH_GPU_SWEEP_GRACE), and the lock is released only once the cgroup is EMPTY, with one line
# naming what was swept. A cgroup that does not empty after the KILL keeps the card: ONE LOUD LINE names each surviving pid
# (here and in <lock>.log) and the hold waits until they are gone - the card is never handed on with a live process of the
# hold. A nested call (JAH_GPU_LOCK_HELD=1: spec.sh's util.CardLock holds the card, its ctests and boots run through here)
# takes its own scope and sweeps it the same way before it returns, so the call releases the card after them; a call
# already inside a hold's scope (JAH_GPU_HOLD set: a row's own lock call inside ctest) runs as before - its hold sweeps it.
# THE HOLD's MEMORY IS CAPPED (2026-11-20: one app under GPU-assisted validation grew to 26 GB of a 32-GB box and the kernel's global
# OOM killer fired): the scope carries MemoryMax (JAH_GPU_MEM_MAX, default 16G; a normal app is ~3 GB) and MemorySwapMax, so a
# runaway is killed ALONE, inside its own cgroup, rc 137, and the box and the other lanes keep their memory.
# No user manager (no systemd-run or no bus socket): the hold is marked instead, JAH_GPU_HOLD=<unit> in the command's
# environment, and the sweep finds its processes by that mark in /proc/<pid>/environ (a process that clears its
# environment escapes it - the scope does not have that hole).
# THE HOLD LOG (TEST-STREAMLINE-1 1d, 2026-11-03: the test audit could not count card hours by holder - only anomalies were
# logged): every hold of the card appends to <lock>.holds, one line when the card is TAKEN and one when it is RELEASED,
# paired by the ticket (a SIGKILLed holder leaves its HOLD line alone - a kill cannot write its end; the reader closes it at
# the next line of that pid, or reads it open):
#   HOLD <ticket> <pid> <start, ISO seconds> <the wait, s> <label> <class: long | short:<declared s>>
#   DONE <ticket> <pid> <end, ISO seconds> <rc> <held, s>
#   OVERRUN <ticket> <pid> <declared s> <held s> <label>          (a short hold past its declared time: SHORT HOLDS PASS below)
# tab-separated, each line one write (O_APPEND: whole lines from concurrent holders). A nested call (a holder's own row)
# and an exit 75 (never held) write none. The lead's util.CardLock writes the same lines (its rc "-": it does not run the
# command). Two lines a hold, not one at the release: a killed holder's hold would otherwise leave no trace at all.
# THE LOG IS ON DISK (WORKFLOW-STREAMLINE-1 2.5, 2026-11-20: it lived beside the lock in /tmp - RAM - and every hold of the
# sprint before the 11-16 reboot was lost to the audit). WHERE (`gpu-exclusive.sh --holds-log` prints it): for THE CARD's lock
# (JAH_GPU_LOCK unset or /tmp/jah-gpu.lock) JAH_GPU_HOLD_LOG when set, else <the box-local root>/lock/holds.log
# (JAH_LOCAL_ROOT, else <the user's passwd home>/Developer/local - BOX-LOCAL-1: box data, never in a repo; the passwd home, so a
# row's scratch HOME never moves it); a PRIVATE lock (a suite's) ALWAYS <lock>.holds beside it, whatever JAH_GPU_HOLD_LOG says
# (CARD-QUEUE-1, 2026-11-21: the box's env.sh exports JAH_GPU_HOLD_LOG to every tool a lead tool starts, so 162 toy holds of
# the lock suites - gpu_lock_fifo's E/H/W1-5, card.sh's C11, display-guard - were written into the card's record in one day
# and counted as card holds: a toy hold never enters the card's record). Its directory is made.
# THE REPORT: `gpu-exclusive.sh --report [since]` (since: anything `date -d` reads - 2026-11-16, '7 days ago') - per side
# (ours / the Unreal session's `ue-*` labels): holds, total held, share of all holds, share of the window (busy), the
# holds over 600 s, waits; then every hold over 600 s listed, and every open hold (a HOLD with no DONE: a killed holder,
# closed at the next hold's start - the flock makes that its latest possible end - or one running now). The ~10-min hold
# is a TARGET (CLAUDE.md "The box and the card"): an overrun is REPORTED here, a running call is never cut.
# The kernel's GPU faults while the command ran are printed after it (scripts/kernel_xid.py): an Xid is never
# environmental. The desktop route by default: JAHSHAKA_VR=0 unless the caller set JAHSHAKA_VR (`--vr` runs set it).
# SHORT HOLDS PASS (CARD-QUEUE-1, CODING_RULES § 7; measured 2026-11-21: 465 of 568 holds were under 90 s, held 2.43 h and
# WAITED 11.87 h - mostly behind long calls). `--short <s>` (1-90 s, else 64) DECLARES a hold short: one row (`ctest -R`), a one-shot
# trial, a warm-up boot whose measured wall fits - never a whole check (spec.sh test, merge, ship: the lead's util.CardLock) and
# never the six. Its ticket is named <n>.s<s> (a long one <n>, as ever; a stale copy of this script globs it as an ordinary
# ticket in its order and degrades to first come, first served). THE HEAD: the lowest live ticket - unless that is LONG and the
# FIRST waiting short has CREDIT on every long ticket below it: then that short. THE FAIRNESS LAW: a long ticket is passed while
# passed + s <= 90 s + 15 % of its owner's own held seconds (<lock>.q/.credit/<pid>-<start>: "held passed count"), so a long call's
# wall grows by at most 90 s + 15 % of its own hold (+ any logged overrun); the credits are written ONLY by the card's holder (a
# short at its grant charges its declared time to each long ticket it passed, corrected to its actual time at its release; the
# long caller its held time) - the flock itself serialises them, and a short that wins the flock reads the head again under it.
# A long call BETWEEN ITS ROWS (util.CardLock in spec.sh's gate, at a batch boundary) passes the card to a waiting short within
# credit and keeps its place: it re-enters as <its n>.y<k> (after n, before n+1). A running row is never cut. OVERRUN: a short
# hold held past its declared time writes an OVERRUN line, says so, and its CALLER (JAH_GPU_CALLER, else the parent pid; rig.sh
# passes its own caller, warm-app-cache.sh its own pid) loses the class: its next --short holds long (<lock>.q/.lost/). Never
# killed. Under a holder (JAH_GPU_LOCK_HELD) --short means nothing: the call goes at once. With no short ticket in the queue the
# grant order is exactly the first-come order above (tooling.gpu_lock_fifo's cases).
# A CALL HOLDS ONE PLACE (CARD-QUEUE-1, the lead's add; measured 2026-11-21: 13 feature-end calls ended 75 IN THEIR WARM-UP, no row
# reached - 5.42 h of wall, >= 4.27 h of it waiting - because each warm-up boot took a fresh ticket and its call, re-called,
# queued at the BACK). JAH_GPU_PLACE=<key> (spec.sh test sets it for its warm-up's boots; util.CardLock's holds read it too): a
# hold of the call adopts the call's reservation `R <expiry> <key>` - its NUMBER, the call's place in the line - and at its end
# (or an exit 75) writes it back at that number, JAH_GPU_PLACE_S (600 s) ahead; while the call is away between its holds the
# reservation is passed over (it never idles the card). A hold with a place is long (--short is dropped, said).
# THE CALL's IDENTITY STOPS AT THE HOLD (CARD-QUEUE-1 M1, the second engineer's review: a call's place in a row's environment
# turned the lock's own rows red): JAH_GPU_PLACE and JAH_GPU_CALLER are read here, for this hold, and REMOVED before the command
# runs (a nested call included) - the command and every row under it never take the call's place or its caller. The one list
# of what a hold's command (and so a row) may see of the lock - tooling.row_lock_env reads the next line:
# A HOLD's COMMAND SEES: JAH_GPU_LOCK_HELD JAH_GPU_HOLD JAH_GPU_LOCK JAH_GPU_HOLD_LOG JAH_GPU_RESERVE_S JAH_GPU_PLACE_S JAH_GPU_MEM_MAX JAH_GPU_SWAP_MAX JAH_GPU_NO_SCOPE JAH_GPU_SWEEP_GRACE
export JAHSHAKA_VR="${JAHSHAKA_VR-0}"
here="$(cd "$(dirname "$0")" && pwd)"
card_lock=/tmp/jah-gpu.lock
holds_log() {   # the hold log's path (the header's THE LOG IS ON DISK): only the card's lock writes the card's log
    local h
    if [ "${JAH_GPU_LOCK:-$card_lock}" != "$card_lock" ]; then printf '%s\n' "$JAH_GPU_LOCK.holds"
    elif [ -n "${JAH_GPU_HOLD_LOG:-}" ]; then printf '%s\n' "$JAH_GPU_HOLD_LOG"
    else h=$(getent passwd "$(id -u)" | cut -d: -f6); printf '%s\n' "${JAH_LOCAL_ROOT:-${h:-$HOME}/Developer/local}/lock/holds.log"; fi
}
if [ "${1:-}" = --holds-log ]; then holds_log; exit 0; fi
if [ "${1:-}" = --report ]; then   # (the header's THE REPORT; reads the log, never takes the lock)
    s0=0
    if [ -n "${2:-}" ]; then
        s0=$(date -d "$2" +%s 2> /dev/null) \
            || { echo "gpu-exclusive.sh --report: '$2' is not a date (date -d reads it: 2026-11-16, '7 days ago')" >&2; exit 64; }
    fi
    exec python3 -I - "$(holds_log)" "$s0" <<'PY'
import os, sys, time
from datetime import datetime
log, s0 = sys.argv[1], int(sys.argv[2])
LONG = 600   # the ~10-min hold (CLAUDE.md "The box and the card"): a target, reported, never enforced
now = time.time()
try:
    lines = open(log, errors="replace").read().splitlines()
except OSError as e:
    print(f"gpu-lock report: no hold log at {log} ({e.strerror})"); sys.exit(1)
ts = lambda s: datetime.strptime(s, "%Y-%m-%dT%H:%M:%S%z").timestamp()
day = lambda t: datetime.fromtimestamp(t).strftime("%F %T")
holds, byk, skipped = [], {}, 0
for ln in lines:
    f = ln.split("\t")
    try:
        if len(f) in (6, 7) and f[0] == "HOLD":   # (the 7th field, CARD-QUEUE-1: the class, long | short:<s>)
            h = {"t": f[1], "pid": int(f[2]), "start": ts(f[3]), "wait": float(f[4]), "label": f[5], "end": None, "rc": "?", "how": "",
                 "cls": f[6] if len(f) == 7 else "", "over": None}
            holds.append(h); byk[(f[1], f[2])] = h
        elif len(f) == 6 and f[0] == "DONE" and (f[1], f[2]) in byk:
            h = byk[(f[1], f[2])]; h["end"], h["rc"] = h["start"] + int(f[5]), f[4]
        elif len(f) == 6 and f[0] == "OVERRUN" and (f[1], f[2]) in byk:   # <ticket> <pid> <declared> <held> <label>
            byk[(f[1], f[2])]["over"] = (int(f[3]), int(f[4]))
        else:
            skipped += 1
    except ValueError:
        skipped += 1
holds.sort(key=lambda h: h["start"])
for i, h in enumerate(holds):   # an open hold: closed at the next hold's start (the flock: its latest end), or running
    if h["end"] is not None:
        continue
    if i + 1 < len(holds):
        h["end"], h["how"] = holds[i + 1]["start"], "killed (no DONE): closed at the next hold's start"
    elif os.path.exists(f"/proc/{h['pid']}"):
        h["end"], h["how"] = now, "RUNNING now"
    else:
        h["how"] = "open (no DONE, no later hold): its end unknown, not counted"
sel = [h for h in holds if h["start"] >= s0]
t0 = s0 if s0 else (sel[0]["start"] if sel else now)
span = max(now - t0, 1.0)
side = lambda h: "ue-*" if h["label"].startswith("ue-") else "ours"
held = lambda h: (h["end"] - h["start"]) if h["end"] is not None else 0.0
total = sum(held(h) for h in sel)
print(f"gpu-lock report: {log}")
frm = "(since)" if s0 else "(the log's first hold)"
print(f"  {len(sel)} holds from {day(t0)} {frm} to {day(now)}: {span / 3600:.1f} h"
      + (f"; {skipped} line(s) not read" if skipped else ""))
print(f"  {'side':<5} {'holds':>6} {'held s':>9} {'held h':>7} {'share':>6} {'busy':>5} {'>600s':>6} {'max s':>6}"
      f" {'waited':>7} {'wait s':>8} {'max wait':>9}")
for name in ("ours", "ue-*", "all"):
    g = [h for h in sel if name == "all" or side(h) == name]
    hs = sum(held(h) for h in g)
    w = [h["wait"] for h in g if h["wait"] >= 1.0]
    print(f"  {name:<5} {len(g):>6} {hs:>9.0f} {hs / 3600:>7.2f} {(100 * hs / total if total else 0):>5.0f}% {100 * hs / span:>4.0f}%"
          f" {sum(1 for h in g if held(h) > LONG):>6} {max([held(h) for h in g] or [0]):>6.0f}"
          f" {len(w):>7} {sum(w):>8.0f} {max(w or [0]):>9.0f}")
over = [h for h in sel if held(h) > LONG]
print(f"HOLDS OVER {LONG} s - {len(over)} (the ~10-min hold is a target: reported, a running call is never cut):")
for h in over:
    print(f"  {day(h['start'])}  {side(h):<4} held {held(h):>5.0f} s  waited {h['wait']:>5.0f} s  rc {h['rc']:<2} pid {h['pid']:<8}"
          f" {h['label']}" + (f"  [{h['how']}]" if h["how"] else ""))
sh = [h for h in sel if h["cls"].startswith("short:")]
yl = [h for h in sel if ".y" in h["t"]]
ov = [h for h in sel if h["over"]]
if sh or yl or ov:   # SHORT HOLDS PASS (CARD-QUEUE-1): the class's holds, the long calls' yields to them, the overruns
    print(f"SHORT HOLDS - {len(sh)}: held {sum(held(h) for h in sh):.0f} s, waited {sum(h['wait'] for h in sh):.0f} s; "
          f"a long call passed the card to them at a row boundary {len(yl)} time(s); OVERRUNS (a fault of the caller) - {len(ov)}:")
    for h in ov:
        print(f"  {day(h['start'])}  declared {h['over'][0]} s, held {h['over'][1]} s  pid {h['pid']:<8} {h['label']}")
op = [h for h in sel if h["how"]]
if op:
    print(f"OPEN HOLDS (a HOLD line with no DONE) - {len(op)}:")
    for h in op:
        print(f"  {day(h['start'])}  {side(h):<4} pid {h['pid']:<8} {h['label']}  [{h['how']}]")
PY
fi
rt="" label="" short=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run-timeout) rt="${2:?--run-timeout needs seconds}"; shift 2 ;;
        --wait-max) wm="${2:?--wait-max needs seconds}"; shift 2 ;;
        --label) label="${2:?--label needs text}"; shift 2 ;;
        --short) short="${2:-}"; shift 2 2> /dev/null || shift
                 case "$short" in [1-9]|[1-8][0-9]|90) ;; *)
                     echo "gpu-exclusive.sh: --short takes the hold's declared seconds, 1 to 90 (a short hold: CODING_RULES § 7) - got '$short'" >&2
                     exit 64 ;; esac ;;
        *) break ;;
    esac
done
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] [--short <s>] <command> [args...]" >&2; exit 64; }
# (the header's THE CALL's IDENTITY STOPS AT THE HOLD: this hold's call and caller, read once, never passed to the command)
place=${JAH_GPU_PLACE:-} caller_env=${JAH_GPU_CALLER:-}
unset JAH_GPU_PLACE JAH_GPU_CALLER
# NOTHING UNDER THE LOCK SEES A DISPLAY THAT IS NOT A RIG (URGENT #195, 2026-11-21): every GPU run passes through here, and an
# agent's shell inherits DISPLAY=:0 - the OWNER's. A DISPLAY outside :60-:99 is UNSET for the command, said on one line: a tool
# under it then takes its tree's rig (warm-app-cache.sh, dolly, perf-ab) and a bare app finds no display and stops - never :0.
# A caller that names its rig (rig.sh run, the lead tools' rows) is untouched. (scripts/display-guard.sh: the one definition.)
. "$here/display-guard.sh"
if [ -n "${DISPLAY:-}" ] && ! jah_rig_display "$DISPLAY"; then
    echo "gpu-lock: DISPLAY=$DISPLAY is not a rig display (60-99; :0 is the OWNER's) - unset for this command (a rig: $here/rig.sh up, then $here/rig.sh run <label> -- <command>)" >&2
    unset DISPLAY
fi
bound=(); [ -n "$rt" ] && bound=(timeout -k 10 "$rt")
[ "${JAH_GPU_LOCK_HELD:-}" = 1 ] && [ -n "${JAH_GPU_HOLD:-}" ] && exec "${bound[@]}" "$@"   # inside a hold: its sweep is ours
lock="${JAH_GPU_LOCK:-$card_lock}"
# THE TREE's CHECK MARK (URGENT #153's mark, TEST-STREAMLINE-1 block D's collision 2026-11-03): a hand `gpu-exclusive.sh ...
# ctest --test-dir <build>` set none, so a builder found its tree free and relinked bin/Jahshaka 4 min into a 6-min re-price -
# every row after it ran a dirty build, the call void. When the command is a ctest (the first word named ctest, after any
# nice/env/timeout/VAR= words) whose test dir (--test-dir, else the cwd) is inside a git tree, this script writes that tree's
# <gitdir>/jah-check.pid ("<pid> <time>", spec.py CheckMark's format: the pre-commit hook and the builders read it) from
# before its wait to its exit, and removes it then - only its own: a mark a live pid already holds (an outer check) is kept.
# ONE CHECK A TREE, TAKEN ATOMICALLY (the landing audit's LOW, 2026-11-04: check-then-write let two marked ctests starting
# together both write it, and the first one's release removed it under the second): the take runs under a flock of
# <gitdir>/jah-check.lock and writes the mark by rename (a reader never sees it half-written); a live mark of an ANCESTOR
# (spec.sh test's, width_pair's: the check this call runs inside) is kept and the call runs; a live mark of any OTHER process
# is a second check in the tree - REFUSED, exit 2, before any wait, the mark untouched.
mark=""
is_ancestor() {   # <pid>: is it this script or one of its parents (/proc/<p>/status PPid, no fork)
    local p=$$ k v
    while [ -n "$p" ] && [ "$p" -gt 1 ]; do
        [ "$p" = "$1" ] && return 0
        v=""; while read -r k v _; do [ "$k" = PPid: ] && break; v=""; done < "/proc/$p/status" 2> /dev/null
        p=$v
    done
    return 1
}
mark_take() {
    local a d="" seen="" g
    for a in "$@"; do
        if [ -z "$seen" ]; then [ "$(basename -- "$a")" = ctest ] && seen=1; continue; fi
        case "$d" in next) d=$a; continue ;; esac
        case "$a" in --test-dir) d=next ;; --test-dir=*) d=${a#--test-dir=} ;; esac
    done
    [ -n "$seen" ] || return 0
    [ -n "$d" ] && [ "$d" != next ] || d=$PWD
    g=$(git -C "$d" rev-parse --absolute-git-dir 2> /dev/null) || return 0
    local f="$g/jah-check.pid" r
    r=$(exec 7> "$g/jah-check.lock" && flock -x 7 || exit 0
        if { read -r p _ < "$f"; } 2> /dev/null && [ -n "$p" ] && kill -0 "$p" 2> /dev/null; then
            is_ancestor "$p" && echo outer || echo "other $(cat "$f" 2> /dev/null)"; exit 0
        fi
        echo "$$ $(date '+%FT%T%z')" > "$f.$$" && mv -f "$f.$$" "$f" && echo taken)
    case "$r" in
        taken) mark=$f ;;
        other*) echo "gpu-lock: REFUSED (exit 2): another check is running in this tree - $f holds '${r#other }' (one check" \
                     "a tree, URGENT #153; wait for it or run in another tree)" >&2
                rm -f "$ticket"; exit 2 ;;
    esac
}
mark_release() {
    [ -n "$mark" ] || return 0
    { read -r p _ < "$mark"; } 2> /dev/null && [ "$p" = "$$" ] && rm -f "$mark"
    mark=""
}
# THE HOLD's CGROUP (URGENT #144, the header): hold_run <unit> <command...> - the command in its scope (or marked), its rc
# kept, every process it left swept before it returns
hold_run() {
    local unit=$1; shift
    if [ -z "${JAH_GPU_NO_SCOPE:-}" ] && command -v systemd-run > /dev/null 2>&1 \
       && [ -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/bus" ]; then
        hold_how=scope
        JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=$unit systemd-run --user --scope --quiet --collect --expand-environment=no --unit="$unit" -p MemoryMax="${JAH_GPU_MEM_MAX:-16G}" -p MemorySwapMax="${JAH_GPU_SWAP_MAX:-4G}" -- "$@"
    else
        hold_how=mark
        JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=$unit "$@"
    fi
    local rc=$?
    hold_sweep "$unit"
    return $rc
}
hold_cg=""
hold_pids() {   # the live pids of the hold <unit>: its scope's cgroup.procs, or the processes carrying its mark
    if [ "$hold_how" = scope ]; then
        [ -n "$hold_cg" ] || hold_cg=$(systemctl --user show -p ControlGroup --value "$1.scope" 2> /dev/null)
        [ -n "$hold_cg" ] && cat "/sys/fs/cgroup$hold_cg/cgroup.procs" 2> /dev/null
    else
        grep -lsza "^JAH_GPU_HOLD=$1\$" /proc/[0-9]*/environ 2> /dev/null | cut -d/ -f3
    fi
    return 0
}
hold_signal() {   # <unit> <TERM|KILL> <pids>
    if [ "$hold_how" = scope ]; then systemctl --user kill --signal="$2" "$1.scope" 2> /dev/null
    else local sig=$2; shift 2; [ "$#" -gt 0 ] && kill -"$sig" "$@" 2> /dev/null; fi
    return 0
}
hold_empty_within() {   # <unit> <s>: 0 once the hold has no live process, 1 if it still has one after <s>
    local i=0 n=$(( $2 * 10 ))
    while [ -n "$(hold_pids "$1")" ]; do
        [ "$i" -ge "$n" ] && return 1
        sleep 0.1; i=$((i + 1))
    done
}
hold_sweep() {   # <unit>: TERM, KILL after the grace, and the hold kept until its cgroup is empty
    local left pids grace=${JAH_GPU_SWEEP_GRACE:-10} how=TERM
    left=$(hold_pids "$1" | tr '\n' ' '); [ -n "$left" ] || return 0
    hold_signal "$1" TERM $left
    if ! hold_empty_within "$1" "$grace"; then
        how="TERM, KILL after ${grace} s"
        hold_signal "$1" KILL $(hold_pids "$1")
        if ! hold_empty_within "$1" "$grace"; then
            pids=""; for p in $(hold_pids "$1"); do pids="$pids $p ($(cat /proc/$p/comm 2> /dev/null))"; done
            local msg="gpu-lock: THE HOLD's PROCESSES OUTLIVE THEIR KILL:$pids - '${label:-$1}' ($1) keeps the card until they are gone, never handing it on with a live process of the hold (URGENT #144)"
            echo "$msg"; echo "$(date '+%F %T') $msg" >> "$lock.log" 2> /dev/null || true
            while [ -n "$(hold_pids "$1")" ]; do sleep 1; done
        fi
    fi
    echo "gpu-lock: swept the hold's leftover process(es): ${left% } ($how) - the card released after them"
}
if [ "${JAH_GPU_LOCK_HELD:-}" = 1 ]; then   # a holder holds the card (util.CardLock): the scope and its sweep only
    hold_run "jahgpu-n$$-$(date +%s%N)" "${bound[@]}" "$@"
    exit $?
fi
# THE DRIVER's SHADER CACHE REACHES EVERY GPU RUN (URGENT #91 + #99, 2026-10-28): /etc/profile.d is not sourced in an agent's
# shell, and a row's fresh HOME has no driver cache at all, so every run redid the driver's work. Every GPU run passes here.
export __GL_SHADER_DISK_CACHE_SIZE="${__GL_SHADER_DISK_CACHE_SIZE:-8589934592}"
# the driver appends GLCache/ itself: the path is the PARENT, so the rows share the box's own cache (#97's diagnosis)
# - the login user's passwd home, never a spelled one (BOX-LOCAL-1: no box's path in a script)
_ph=$(getent passwd "$(id -u)" | cut -d: -f6)
export __GL_SHADER_DISK_CACHE_PATH="${__GL_SHADER_DISK_CACHE_PATH:-${_ph:-$HOME}/.cache/nvidia}"
exec 9>>"$lock"
# THE TICKET (URGENT #113): the number and the ticket file under the queue's own lock, so a lower ticket always exists
# before a higher one is seen
q="$lock.q"
mkdir -p "$q"
exec 8>>"$q/.counter.lock"
flock 8
me="${label:-$1}"
starttime() { sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f20; }   # /proc/<pid>/stat field 22
# THE CLASS LOST (the header's SHORT HOLDS PASS): a caller one of whose short holds overran its declared time holds long from then on
caller=${caller_env:-$PPID}; lostf="$q/.lost/$caller-$(starttime "$caller")"
if [ -n "$short" ] && [ -e "$lostf" ]; then
    echo "gpu-lock: --short $short NOT HONOURED: an earlier short hold of this caller (pid $caller) overran its declared time - this hold is long"
    short=""
fi
ticket=""
# A CALL's ONE PLACE (CARD-QUEUE-1, the lead's add; the header's ONE PLACE): JAH_GPU_PLACE names the call this hold is part of;
# its reservation (R <expiry> <place>) is adopted whatever this hold's label, and the hold is long (the place is the call's)
# ($place: read before the option-less nested path, above)
if [ -n "$place" ] && [ -n "$short" ]; then echo "gpu-lock: --short $short: this hold takes its call's place ($place) - long, at that place"; short=""; fi
for f in "$q"/[0-9]*; do   # a reservation under this label (a hold with a place: under its place only): adopted (the re-call after an exit 75) - of its own class
    case "$f" in *.tmp.*) continue ;; esac
    [ -e "$f" ] || continue   # (an empty queue: the glob unmatched - its redirect would print before 2>/dev/null applies)
    case "${f##*/}" in *.s[0-9]*) [ -n "$short" ] || continue ;; *) [ -z "$short" ] || continue ;; esac
    read -r p1 p2 p3 < "$f" 2>/dev/null || continue
    [ "$p1" = R ] && [ "${p2:-0}" -gt "$(date +%s)" ] || continue
    # A HOLD WITH A PLACE ADOPTS ITS PLACE ONLY, NEVER A LABEL (note 6, the second engineer's review: every lane's warm-up boot has
    # the same label, and a placed boot took ANOTHER lane's bare exit-75 reservation by it - lock_test 14f); a bare hold, its label
    if [ -n "$place" ]; then [ "$p3" = "$place" ]; else [ "$p3" = "$me" ]; fi && { ticket=$f; break; }
done
if [ -z "$ticket" ]; then
    n=$(( $(cat "$q/counter" 2>/dev/null || echo 0) + 1 ))
    echo "$n" > "$q/counter"
    ticket="$q/$(printf '%012d' "$n")${short:+.s$short}"
fi
case "${ticket##*/}" in *.s[0-9]*) short=${ticket##*.s} ;; esac   # (an adopted short reservation keeps its declared time)
echo "$$ $(starttime $$) ${place:+$place }$me" > "$ticket.tmp.$$" && mv -f "$ticket.tmp.$$" "$ticket"   # (a call's hold: its place, M2)
flock -u 8
exec 8>&-
mark_take "$@"
keep_place() {   # the call's place kept at its number for the call's next hold: R <expiry> <place> (passed over while the call is away)
    local pt="$q/$(basename "$ticket" | cut -c1-12)"
    echo "R $(( $(date +%s) + ${JAH_GPU_PLACE_S:-600} )) $place" > "$pt.tmp.$$" && mv -f "$pt.tmp.$$" "$pt"
}
trap 'rm -f "$ticket"; [ -z "$place" ] || keep_place; mark_release' EXIT   # (no TERM/INT trap: bash would defer it past a blocking flock; a killed waiter's ticket is a dead pid's)
alive() {   # a ticket's pid and start time: the same live process (a recycled pid is another start time: dead)
    [ -n "$1" ] && [ -n "$2" ] && [ "$(starttime "$1")" = "$2" ]
}
# THE FAIRNESS LAW (CODING_RULES § 7; util.py PASS_ALLOW_S / PASS_SHARE hold the same two numbers - lock_test holds them equal):
# a long ticket is passed by shorts while passed + s <= 90 s + 15 % of its owner's own held seconds
# THE CREDIT IS THE CALL's (CARD-QUEUE-1 M2, the second engineer's review: keyed by the ticket's process, each warm-up boot of a
# feature end got a fresh 90 s and the call's end line read 0 - lock_test 14d): a hold that is part of a call writes its ticket
# "<pid> <start> place:<pid>-<start> <label>", and its credit is the PLACE's <pid>-<start> - one credit for every hold of the call
# (its boots here, its util.CardLock holds and yields); a hold with no place keys by its own <pid>-<start>
key_of() { case "$3" in place:*-*) echo "${3#place:}" ;; *) echo "$1-$2" ;; esac; }   # <pid> <start> <the ticket's 3rd word>
PASS_ALLOW_S=${JAH_GPU_PASS_ALLOW_S:-90} PASS_SHARE_PCT=15   # (JAH_GPU_PASS_ALLOW_S: the suites' toy allowance, never set by a tool)
short_of() { case "${1##*/}" in *.s[0-9]*) echo "${1##*.s}" ;; esac; }   # a short ticket's declared seconds; empty: a long one
credit_ok() {   # <key> <s>: may the call (or the lone hold) of this long ticket be passed by <s> more seconds of shorts
    local held=0 passed=0
    { read -r held passed _ < "$q/.credit/$1"; } 2> /dev/null   # (braced: a missing file's error is the redirect's own, N)
    [ $(( 100 * (${passed:-0} + $2) )) -le $(( 100 * PASS_ALLOW_S + PASS_SHARE_PCT * ${held:-0} )) ]
}
head_ticket() {   # THE HEAD: the lowest ticket whose pid is alive - unless it is LONG and the first waiting SHORT ticket has credit
                  # on every long ticket below it (then that short); the dead ones removed; a reservation is passed over (an
                  # absent caller never idles the card) and kept until it expires. With no short ticket: the lowest live, as ever.
    local f pid exp k first="" longs=() s l ok
    for f in "$q"/[0-9]*; do
        case "$f" in *.tmp.*) continue ;; esac
        [ -e "$f" ] || continue
        { read -r pid exp k _ < "$f"; } 2>/dev/null || pid=""
        if [ "$pid" = R ]; then   # (passed over; removed once expired - a call's place at once when its caller died: ONE PLACE)
            case "$k" in place:*-*) k=${k#place:}; alive "${k%-*}" "${k##*-}" || exp=0 ;; esac
            [ "${exp:-0}" -gt "$(date +%s)" ] || rm -f "$f"; continue
        fi
        alive "$pid" "$exp" || { rm -f "$f"; continue; }
        s=$(short_of "$f")
        if [ -z "$s" ]; then [ -n "$first" ] || first=$f; longs+=("$(key_of "$pid" "$exp" "$k")"); continue; fi
        [ -n "$first" ] || { echo "$f"; return; }                 # a short at the front: first come, first served
        ok=1; for l in "${longs[@]}"; do credit_ok "$l" "$s" || { ok=""; break; }; done
        [ -n "$ok" ] && { echo "$f"; return; }
        break                                                     # the FIRST waiting short only: shorts keep their order
    done
    echo "$first"
}
charge() {   # <seconds, may be negative>: added to `passed` of every long ticket this short hold passed (charged[]) - written
             # only by the card's holder (this hold), by rename: the flock itself serialises every credit write
    local k held passed n
    for k in "${charged[@]}"; do
        held=0 passed=0 n=0; { read -r held passed n < "$q/.credit/$k"; } 2> /dev/null
        echo "${held:-0} $(( ${passed:-0} + $1 )) $(( ${n:-0} + ${2:-0} ))" > "$q/.credit/$k.tmp.$$" && mv -f "$q/.credit/$k.tmp.$$" "$q/.credit/$k"
    done
}
holder() { cat "$lock.holder" 2>/dev/null || echo '?'; }
named=""
unticketed() {   # a live holder whose line lacks the ticketed mark: named once per holder pid, here and in <lock>.log
    local line hp
    line=$(cat "$lock.holder" 2>/dev/null) || return 0
    case "$line" in *" ticketed") return 0 ;; pid\ *) ;; *) return 0 ;; esac
    hp=$(printf '%s' "$line" | cut -d' ' -f2)
    [ -n "$hp" ] && [ "$hp" != "$named" ] && kill -0 "$hp" 2>/dev/null || return 0
    named=$hp
    local msg="gpu-lock: AN UNTICKETED HOLDER: ${line% since *} - an old gpu-exclusive.sh copy or a bare flock: it took the card without a ticket, ahead of every ticketed waiter (URGENT #141); its owner runs the tree's scripts/gpu-exclusive.sh"
    echo "$msg"
    echo "$(date '+%F %T') waiter '$me' (pid $$): $msg" >> "$lock.log" 2>/dev/null || true
}
stayed() {   # the place kept for the re-call (the header): the ticket rewritten as a reservation, the trap's removal off
    trap - EXIT
    mark_release
    if [ -n "$place" ]; then rm -f "$ticket"; keep_place
    else echo "R $(( $(date +%s) + ${JAH_GPU_RESERVE_S:-120} )) $me" > "$ticket.tmp.$$" && mv -f "$ticket.tmp.$$" "$ticket"; fi
    echo "gpu-lock: the card stayed held ${wm} s (by: $(holder)) - nothing ran (the place kept ${JAH_GPU_RESERVE_S:-120} s for a re-call labelled '$me')"
    exit 75
}
t0=$(date +%s) t0f=$(date +%s.%N)
waiting=""
while :; do
    # THE HEAD TRIES THE CARD, NEVER BLOCKS IN THE KERNEL: a blocked flock is served in the kernel's order, so a re-call
    # adopting an earlier ticket could not pass it - the head re-reads the queue between tries (0.2 s; the others 0.5 s: a
    # new head notices within it, the card's idle at a hand-off)
    h=$(head_ticket)
    if [ "$h" = "$ticket" ] && flock -n 9; then
        # the credits move only under the flock: read again now it is held - a short whose credit was spent meanwhile waits on
        [ "$(head_ticket)" = "$ticket" ] && break
        flock -u 9
    fi
    [ -n "$waiting" ] || { echo "gpu-lock: waiting for the card (held by: $(holder))"; waiting=1; }
    [ "$h" = "$ticket" ] && unticketed   # (the head alone: it is the one the flock refused)
    [ -n "${wm:-}" ] && [ $(( $(date +%s) - t0 )) -ge "$wm" ] && stayed
    if [ "$h" = "$ticket" ]; then sleep 0.2; else sleep 0.5; fi
done
charged=()
if [ -n "$short" ]; then   # A SHORT HOLD's GRANT: it passes every live long ticket below its own - each charged its declared time
    mkdir -p "$q/.credit" "$q/.lost"
    for f in "$q"/[0-9]*; do
        case "$f" in *.tmp.*) continue ;; esac
        [ -e "$f" ] && [[ ${f##*/} < ${ticket##*/} ]] && [ -z "$(short_of "$f")" ] || continue
        { read -r pid exp k _ < "$f"; } 2> /dev/null; [ "$pid" != R ] && alive "$pid" "$exp" && charged+=("$(key_of "$pid" "$exp" "$k")")
    done
    charge "$short" 1
    for f in "$q/.credit"/* "$q/.lost"/*; do   # the credit and the lost mark of a process gone: removed by the card's holder
        [ -e "$f" ] || continue; case "$f" in *.tmp.*) continue ;; esac
        k=${f##*/}; alive "${k%-*}" "${k##*-}" || rm -f "$f"
    done
fi
rm -f "$ticket"
waited=$(awk -v a="$t0f" -v b="$(date +%s.%N)" 'BEGIN{printf "%.1f", b-a}')
[ -z "$waiting" ] || echo "gpu-lock: waited $waited s"   # tenths (#95)
echo "pid $$ ${label:-$1} since $(date '+%F %T') ticketed" > "$lock.holder"   # (#141: the mark a waiter reads)
start=$(date +%s)
holds=$(holds_log); mkdir -p "$(dirname "$holds")" 2> /dev/null; tno=$(basename "$ticket")
printf 'HOLD\t%s\t%s\t%s\t%s\t%s\t%s\n' "$tno" "$$" "$(date -d "@$start" '+%FT%T%z')" "$waited" "$me" \
    "$([ -n "$short" ] && echo "short:$short" || echo long)" >> "$holds" 2> /dev/null || true
hold_run "jahgpu-${tno//./_}-$$" "${bound[@]}" "$@"   # (#144: the hold's cgroup, swept before the release)
rc=$?
mark_release
held=$(( $(date +%s) - start ))
if [ -n "$short" ]; then   # (still under the flock) the passed longs charged the hold's ACTUAL time; an overrun is its caller's fault
    charge $(( held - short )) 0
    if [ "$held" -gt "$short" ]; then
        printf 'OVERRUN\t%s\t%s\t%s\t%s\t%s\n' "$tno" "$$" "$short" "$held" "$me" >> "$holds" 2> /dev/null || true
        echo "gpu-lock: OVERRUN - '$me' declared a short hold of $short s and held $held s: logged; the short class is lost for its caller (pid $caller) - its next --short holds long (CODING_RULES § 7: a running call is never cut)"
        : > "$lostf" 2> /dev/null || true
    fi
fi
printf 'DONE\t%s\t%s\t%s\t%s\t%s\n' "$tno" "$$" "$(date '+%FT%T%z')" "$rc" "$held" >> "$holds" 2> /dev/null || true
python3 "$here/kernel_xid.py" since "$start" 2>&1 | sed 's/^/gpu-lock: /'
exit $rc
