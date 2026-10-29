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
# util.CardLock too: a holder outside it is served first by the kernel's blocked-flock order); it waits (1 s polls)
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
[ "${JAH_GPU_LOCK_HELD:-}" = 1 ] && exec "${bound[@]}" "$@"
# THE DRIVER's SHADER CACHE REACHES EVERY GPU RUN (URGENT #91 + #99, 2026-10-28): /etc/profile.d is not sourced in an agent's
# shell, and a row's fresh HOME has no driver cache at all, so every run redid the driver's work. Every GPU run passes here.
export __GL_SHADER_DISK_CACHE_SIZE="${__GL_SHADER_DISK_CACHE_SIZE:-8589934592}"
# the driver appends GLCache/ itself: the path is the PARENT, so the rows share the box's own cache (#97's diagnosis)
export __GL_SHADER_DISK_CACHE_PATH="${__GL_SHADER_DISK_CACHE_PATH:-/home/jahshaka/.cache/nvidia}"
lock="${JAH_GPU_LOCK:-/tmp/jah-gpu.lock}"
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
stayed() {   # the place kept for the re-call (the header): the ticket rewritten as a reservation, the trap's removal off
    trap - EXIT
    echo "R $(( $(date +%s) + ${JAH_GPU_RESERVE_S:-120} )) $me" > "$ticket.tmp.$$" && mv -f "$ticket.tmp.$$" "$ticket"
    echo "gpu-lock: the card stayed held ${wm} s (by: $(holder)) - nothing ran (the place kept ${JAH_GPU_RESERVE_S:-120} s for a re-call labelled '$me')"
    exit 75
}
t0=$(date +%s)
waiting=""
while :; do
    # THE HEAD TRIES THE CARD, NEVER BLOCKS IN THE KERNEL: a blocked flock is served in the kernel's order, so a re-call
    # adopting an earlier ticket could not pass it - the head re-reads the queue between tries (0.2 s; the others 1 s)
    h=$(head_ticket)
    [ "$h" = "$ticket" ] && flock -n 9 && break
    [ -n "$waiting" ] || { echo "gpu-lock: waiting for the card (held by: $(holder))"; waiting=1; }
    [ -n "${wm:-}" ] && [ $(( $(date +%s) - t0 )) -ge "$wm" ] && stayed
    if [ "$h" = "$ticket" ]; then sleep 0.2; else sleep 1; fi
done
rm -f "$ticket"
[ -z "$waiting" ] || echo "gpu-lock: waited $(( $(date +%s) - t0 )) s"
echo "pid $$ ${label:-$1} since $(date '+%F %T')" > "$lock.holder"
start=$(date +%s)
JAH_GPU_LOCK_HELD=1 "${bound[@]}" "$@"
rc=$?
python3 "$here/kernel_xid.py" since "$start" 2>&1 | sed 's/^/gpu-lock: /'
exit $rc
