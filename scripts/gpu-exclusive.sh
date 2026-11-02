#!/usr/bin/env bash
# gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] <command> [args...] — run <command> holding THE CARD: the one
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
# No user manager (no systemd-run or no bus socket): the hold is marked instead, JAH_GPU_HOLD=<unit> in the command's
# environment, and the sweep finds its processes by that mark in /proc/<pid>/environ (a process that clears its
# environment escapes it - the scope does not have that hole).
# The kernel's GPU faults while the command ran are printed after it (scripts/kernel_xid.py): an Xid is never
# environmental. The desktop route by default: JAHSHAKA_VR=0 unless the caller set JAHSHAKA_VR (`--vr` runs set it).
export JAHSHAKA_VR="${JAHSHAKA_VR-0}"
here="$(cd "$(dirname "$0")" && pwd)"
rt="" label=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --run-timeout) rt="${2:?--run-timeout needs seconds}"; shift 2 ;;
        --wait-max) wm="${2:?--wait-max needs seconds}"; shift 2 ;;
        --label) label="${2:?--label needs text}"; shift 2 ;;
        *) break ;;
    esac
done
[ "$#" -gt 0 ] || { echo "usage: gpu-exclusive.sh [--run-timeout <s>] [--wait-max <s>] [--label <text>] <command> [args...]" >&2; exit 64; }
bound=(); [ -n "$rt" ] && bound=(timeout -k 10 "$rt")
[ "${JAH_GPU_LOCK_HELD:-}" = 1 ] && [ -n "${JAH_GPU_HOLD:-}" ] && exec "${bound[@]}" "$@"   # inside a hold: its sweep is ours
lock="${JAH_GPU_LOCK:-/tmp/jah-gpu.lock}"
# THE HOLD's CGROUP (URGENT #144, the header): hold_run <unit> <command...> - the command in its scope (or marked), its rc
# kept, every process it left swept before it returns
hold_run() {
    local unit=$1; shift
    if [ -z "${JAH_GPU_NO_SCOPE:-}" ] && command -v systemd-run > /dev/null 2>&1 \
       && [ -S "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/bus" ]; then
        hold_how=scope
        JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=$unit systemd-run --user --scope --quiet --collect --expand-environment=no --unit="$unit" -- "$@"
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
export __GL_SHADER_DISK_CACHE_PATH="${__GL_SHADER_DISK_CACHE_PATH:-/home/jahshaka/.cache/nvidia}"
exec 9>>"$lock"
# THE TICKET (URGENT #113): the number and the ticket file under the queue's own lock, so a lower ticket always exists
# before a higher one is seen
q="$lock.q"
mkdir -p "$q"
exec 8>>"$q/.counter.lock"
flock 8
me="${label:-$1}"
starttime() { sed 's/.*) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f20; }   # /proc/<pid>/stat field 22
ticket=""
for f in "$q"/[0-9]*; do   # a reservation under this label: its place adopted (the re-call after an exit 75)
    case "$f" in *.tmp.*) continue ;; esac
    [ -e "$f" ] || continue   # (an empty queue: the glob unmatched - its redirect would print before 2>/dev/null applies)
    read -r p1 p2 p3 < "$f" 2>/dev/null || continue
    [ "$p1" = R ] && [ "${p2:-0}" -gt "$(date +%s)" ] && [ "$p3" = "$me" ] && { ticket=$f; break; }
done
if [ -z "$ticket" ]; then
    n=$(( $(cat "$q/counter" 2>/dev/null || echo 0) + 1 ))
    echo "$n" > "$q/counter"
    ticket="$q/$(printf '%012d' "$n")"
fi
echo "$$ $(starttime $$) $me" > "$ticket.tmp.$$" && mv -f "$ticket.tmp.$$" "$ticket"
flock -u 8
exec 8>&-
trap 'rm -f "$ticket"' EXIT   # (no TERM/INT trap: bash would defer it past a blocking flock; a killed waiter's ticket is a dead pid's)
alive() {   # a ticket's pid and start time: the same live process (a recycled pid is another start time: dead)
    [ -n "$1" ] && [ -n "$2" ] && [ "$(starttime "$1")" = "$2" ]
}
head_ticket() {   # the lowest ticket whose pid is alive; the dead ones ahead of it removed; a reservation is passed
                  # over (an absent caller never idles the card) and kept until it expires
    local f pid exp
    for f in "$q"/[0-9]*; do
        case "$f" in *.tmp.*) continue ;; esac
        [ -e "$f" ] || continue
        read -r pid exp _ < "$f" 2>/dev/null || pid=""
        if [ "$pid" = R ]; then [ "${exp:-0}" -gt "$(date +%s)" ] || rm -f "$f"; continue; fi
        if alive "$pid" "$exp"; then echo "$f"; return; fi
        rm -f "$f"
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
    echo "R $(( $(date +%s) + ${JAH_GPU_RESERVE_S:-120} )) $me" > "$ticket.tmp.$$" && mv -f "$ticket.tmp.$$" "$ticket"
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
    [ "$h" = "$ticket" ] && flock -n 9 && break
    [ -n "$waiting" ] || { echo "gpu-lock: waiting for the card (held by: $(holder))"; waiting=1; }
    [ "$h" = "$ticket" ] && unticketed   # (the head alone: it is the one the flock refused)
    [ -n "${wm:-}" ] && [ $(( $(date +%s) - t0 )) -ge "$wm" ] && stayed
    if [ "$h" = "$ticket" ]; then sleep 0.2; else sleep 0.5; fi
done
rm -f "$ticket"
[ -z "$waiting" ] || echo "gpu-lock: waited $(awk -v a="$t0f" -v b="$(date +%s.%N)" 'BEGIN{printf "%.1f", b-a}') s"   # tenths (#95)
echo "pid $$ ${label:-$1} since $(date '+%F %T') ticketed" > "$lock.holder"   # (#141: the mark a waiter reads)
start=$(date +%s)
hold_run "jahgpu-$(basename "$ticket")-$$" "${bound[@]}" "$@"   # (#144: the hold's cgroup, swept before the release)
rc=$?
python3 "$here/kernel_xid.py" since "$start" 2>&1 | sed 's/^/gpu-lock: /'
exit $rc
