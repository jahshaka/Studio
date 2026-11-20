#!/bin/bash
# rig.sh — ONE COMMAND FOR CARD WORK BY HAND (WORKFLOW-STREAMLINE-1 5.8): the rig of CODING_RULES §7 as a tool, not a recipe -
# a display of the tree's own, its REAL pid recorded, the data root beside the tree, the card's lock, nice, our CPUs, VR off.
#
#   rig.sh up [NN] [--env]   an Xvfb at 1920x1080x24 on a free display in 60-99 (NN: that one, refused if taken; a lock file
#                            /tmp/.X<NN>-lock or a socket /tmp/.X11-unix/X<NN> = taken). The REAL Xvfb pid is the one the
#                            server writes in its own lock file - never `$!` of a wrapper (GATE_AND_RIG "Xvfb ... $!") -
#                            checked to be an Xvfb process, and recorded with its start time and display in the tree's
#                            gitignored rig-xvfb.pid. The data root <tree>.data (the SIBLING, never inside the tree) and its
#                            home/ are made. Prints DISPLAY, the data root and the CPUs; --env prints them as shell
#                            assignments for `eval` (DISPLAY, JAH_RIG_DATA, JAH_CPUS, RIG_STARTED=1|0). A rig already up in
#                            the tree is reused, said, RIG_STARTED=0 (a script brings down only a rig it started).
#   rig.sh run <label> [--run-timeout S] [--wait-max S] -- <cmd...>
#                            <cmd> on the tree's rig, holding the card: this tree's scripts/gpu-exclusive.sh --label <label>
#                            (the ticket queue, the hold log, the hold's cgroup), then `taskset -c <our CPUs> nice -n 10` with
#                            DISPLAY=:NN, HOME=<data root>/home, JAHSHAKA_VR=0 (VR runs go through tests/vr/run_vr_app.sh),
#                            JAH_CPUS and JAH_RIG_DATA; a Jahshaka or jahlab binary given no --data-root gets
#                            `--data-root <data root>`. Refused (2) with no live rig of the tree - :0 is never used.
#   rig.sh down              THE STARTER's end: kills EXACTLY the recorded pid, once /proc/<pid>/comm is Xvfb and its start time
#                            is the recorded one (a reused number or pid is another process: never killed), then removes the pid
#                            file - ONLY WHEN NO OTHER LIVE USER is recorded; else it is KEPT ("down requested") and the last
#                            user's release takes it down. A stale pid file (the process gone - a reboot - or no longer ours) is
#                            removed and said.
#   rig.sh release           A REUSER's end (it got RIG_STARTED=0): unrecords its caller; the last user of a rig whose starter
#                            asked it down takes it down. THE USERS (the branch audit's W1): `up` records its caller - JAH_RIG_USER,
#                            else its parent pid - with its start time in <pid file>.users; a user whose pid is gone or whose
#                            start time differs is pruned; `run` is a user for its own run.
#   rig.sh status            the rig, the data root, our CPUs, the card's holder and queue.
#   rig.sh display           the live rig's DISPLAY (:NN), exit 1 if none - for scripts.
#   rig.sh tree              the tree's ONE spelling (~/Developer/..., CODING_RULES §4) - what a script cds into before it
#                            configures (a first configure under /mnt/work/... compiles uncached and re-spells every command).
#   rig.sh cpus              OUR CPU SET, the one setting: $JAH_CPUS, else the first line of <lead repo>/testing/CPUS
#                            (JAH_LEAD_ROOT, else ~/Developer), else 10-19 (the box split with the Unreal session, CLAUDE.md
#                            "The box and the card"). `nproc` honours the affinity, so a build run under
#                            `taskset -c $(rig.sh cpus)` also narrows build-ogre.sh's job count.
# The tree is the one this script sits in; its spelling the canonical ~/Developer one (CODING_RULES §4).
# exit: run passes its command's code (75: the card stayed held); 2 refused; 64 usage.
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
uhome=$(getent passwd "$(id -u)" | cut -d: -f6); uhome=${uhome:-$HOME}
canon() {   # the ~/Developer spelling of a path under the link's real target (scripts/lead/lib/canon.sh's rule)
    local p=$1 hd="$uhome/Developer" rd
    case $p in "$hd"|"$hd"/*) printf '%s\n' "$p"; return ;; esac
    rd=$(readlink -f "$hd" 2> /dev/null) || rd=""
    if [ -n "$rd" ] && [ "$rd" != "$hd" ]; then case $p in "$rd"|"$rd"/*) printf '%s\n' "$hd${p#"$rd"}"; return ;; esac; fi
    printf '%s\n' "$p"
}
tree=$(canon "$(cd "$here/.." && pwd)")
pidf="$tree/rig-xvfb.pid"
users="$pidf.users"   # "<pid> <start time>" a line: the rig's live users (W1)
data="$tree.data"
xdir=${JAH_RIG_XDIR:-/tmp}   # where X servers keep their lock files and sockets (a suite's fake server: its own)
die() { echo "rig.sh: $*" >&2; exit 2; }
usage() { sed -n '4,35p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' >&2; exit 64; }

cpus() {
    local c=${JAH_CPUS:-} f="${JAH_LEAD_ROOT:-$uhome/Developer}/testing/CPUS"
    [ -n "$c" ] || { [ -f "$f" ] && read -r c _ < "$f"; }
    c=${c:-10-19}
    [[ $c =~ ^[0-9]+(-[0-9]+)?(,[0-9]+(-[0-9]+)?)*$ ]] || die "our CPU set '$c' is not a CPU list (JAH_CPUS / testing/CPUS)"
    printf '%s\n' "$c"
}
starttime() { sed 's/.*) //' "/proc/$1/stat" 2> /dev/null | cut -d' ' -f20; }   # /proc/<pid>/stat field 22
comm() { cat "/proc/$1/comm" 2> /dev/null; }
# the recorded rig: "<pid> :<NN> <start time>"; live() = the recorded pid is still that Xvfb
rpid="" rdisp="" rstart=""
[ -f "$pidf" ] && read -r rpid rdisp rstart _ < "$pidf" 2> /dev/null
live() { [ -n "$rpid" ] && [ "$(comm "$rpid")" = Xvfb ] && [ -n "$rstart" ] && [ "$(starttime "$rpid")" = "$rstart" ]; }
me() { local p=${JAH_RIG_USER:-$PPID}; echo "$p $(starttime "$p")"; }
others() {   # the live users other than the caller ($1: its line), pruning the dead from the file
    local p s keep=""
    [ -f "$users" ] || return 0
    while read -r p s; do
        [ -n "$p" ] && [ -n "$s" ] && [ "$(starttime "$p")" = "$s" ] && [ "$p $s" != "$1" ] && keep="$keep$p $s"$'\n'
    done < "$users"
    printf '%s' "$keep" > "$users.tmp.$$" && mv -f "$users.tmp.$$" "$users"
    printf '%s' "$keep"
}
enroll() { local m; m=$(me); others "$m" > /dev/null; echo "$m" >> "$users"; rm -f "$pidf.down"; }
stale_note() {   # a pid file that names no live rig of ours: removed, said
    local why="the process is gone"
    [ -d "/proc/$rpid" ] && why="pid $rpid is now '$(comm "$rpid")' started at another time - not ours"
    rm -f "$pidf"
    echo "rig.sh: a stale pid file (pid ${rpid:-?} ${rdisp:-?}: $why) - removed, nothing killed"
    rpid="" rdisp="" rstart=""
}
say_rig() {   # <started 0|1> [--env]
    local c; c=$(cpus) || exit 2
    if [ "${2:-}" = --env ]; then
        printf 'DISPLAY=%q; JAH_RIG_DATA=%q; JAH_CPUS=%q; RIG_STARTED=%s\n' "$rdisp" "$data" "$c" "$1"
    else
        echo "rig $([ "$1" = 1 ] && echo up || echo "already up"): DISPLAY=$rdisp (Xvfb pid $rpid, $pidf)"
        echo "  data root: $data (HOME for runs: $data/home)   our CPUs: $c"
        echo "  run:  $here/rig.sh run <label> -- <cmd...>     down: $here/rig.sh down"
    fi
}

cmd_up() {
    local want="" env="" n p log
    while [ "$#" -gt 0 ]; do
        case "$1" in --env) env=--env ;; [0-9]*) want=$1 ;; *) usage ;; esac; shift
    done
    if [ -n "$want" ]; then
        [[ $want =~ ^[0-9]+$ ]] && [ "$want" -ge 60 ] && [ "$want" -le 99 ] \
            || die "display :$want refused - a rig is a display in 60-99 (never :0, never another session's range)"
    fi
    mkdir -p "$data/home" || die "cannot make the data root $data"
    exec 7> "$data/.rig.lock" && flock -x 7   # one up at a time in a tree
    [ -f "$pidf" ] && read -r rpid rdisp rstart _ < "$pidf" 2> /dev/null
    if [ -f "$pidf" ] && live; then
        [ -z "$want" ] || [ "$rdisp" = ":$want" ] || die "this tree's rig is up at $rdisp (pid $rpid) - not :$want; rig.sh down first"
        [ -z "$env" ] || echo "rig.sh: this tree's rig is already up at $rdisp (pid $rpid) - reused" >&2
        enroll; say_rig 0 $env; return 0
    fi
    [ -f "$pidf" ] && stale_note >&2
    for n in ${want:-$(seq 60 99)}; do
        if [ -e "$xdir/.X$n-lock" ] || [ -e "$xdir/.X11-unix/X$n" ]; then
            [ -n "$want" ] && die "display :$n is taken ($xdir/.X$n-lock or its socket exists)"
            continue
        fi
        log="$data/xvfb-$n.log"
        # its own session (a caller's process group ending never takes it down); `exec` all the way, so $! IS the server
        # (setsid forks only for a group leader, which a background subshell is not); the rig's lock fd not inherited
        ( exec setsid Xvfb ":$n" -screen 0 1920x1080x24 -nolisten tcp > "$log" 2>&1 < /dev/null 7>&- ) &
        local w=$! i pp
        p=""
        for i in $(seq 100); do   # the server's own lock file names its pid; its socket says it listens (<= 10 s)
            { read -r p _ < "$xdir/.X$n-lock"; } 2> /dev/null
            if [ -n "$p" ] && [ "$(comm "$p")" = Xvfb ] && [ -e "$xdir/.X11-unix/X$n" ]; then
                pp=$(awk '/^PPid:/ { print $2 }' "/proc/$p/status" 2> /dev/null)
                [ "$p" = "$w" ] || [ "$pp" = "$w" ] || p=""   # ANOTHER server won the number between the check and the start
                break
            fi
            p=""; kill -0 "$w" 2> /dev/null || break
            sleep 0.1
        done
        if [ -n "$p" ]; then
            rpid=$p rdisp=":$n" rstart=$(starttime "$p")
            echo "$rpid $rdisp $rstart $tree" > "$pidf.tmp.$$" && mv -f "$pidf.tmp.$$" "$pidf"; rm -f "$users" "$pidf.down"; enroll
            [ -z "$env" ] || echo "rig.sh: Xvfb up at $rdisp (pid $rpid, $pidf)" >&2
            say_rig 1 $env; return 0
        fi
        [ "$(comm "$w")" = Xvfb ] && kill "$w" 2> /dev/null   # ours, half-started
        [ -n "$want" ] && die "Xvfb :$n did not come up ($log)"
    done
    die "no free display in 60-99"
}

cmd_down() {   # <down|release>
    local o
    if [ ! -f "$pidf" ]; then echo "rig.sh: no rig up in $tree (no $pidf)"; return 0; fi
    mkdir -p "$data" && exec 7> "$data/.rig.lock" && flock -x 7   # (the users file: one writer at a time)
    live || { stale_note; rm -f "$users" "$pidf.down"; return 0; }
    o=$(others "$(me)")
    if [ -n "$o" ]; then
        [ "$1" = down ] && : > "$pidf.down"
        echo "rig $1: $rdisp kept: in use by $(printf '%s' "$o" | grep -c .) other user(s) (pid $(printf '%s' "$o" | cut -d' ' -f1 | tr '\n' ' ')) -" \
             "$([ -f "$pidf.down" ] && echo "down requested: the last user's release takes it down" || echo "its starter takes it down")"
        return 0
    fi
    if [ "$1" = release ] && [ ! -f "$pidf.down" ]; then echo "rig release: $rdisp left up (its starter takes it down)"; return 0; fi
    kill "$rpid" 2> /dev/null
    local i; for i in $(seq 50); do [ "$(starttime "$rpid")" = "$rstart" ] || break; sleep 0.1; done
    if [ "$(starttime "$rpid")" = "$rstart" ]; then kill -KILL "$rpid" 2> /dev/null; sleep 0.2; fi
    rm -f "$pidf" "$users" "$pidf.down"
    echo "rig $1: Xvfb pid $rpid ($rdisp) killed, $pidf removed (the data root $data stays)"
}

cmd_run() {
    local label=${1:-}; [ -n "$label" ] && [ "$label" != -- ] || usage; shift
    local lockopts=()
    while [ "$#" -gt 0 ]; do
        case "$1" in
            --run-timeout|--wait-max) lockopts+=("$1" "${2:?$1 needs seconds}"); shift 2 ;;
            --) shift; break ;;
            *) break ;;
        esac
    done
    [ "$#" -gt 0 ] || usage
    live || die "no live rig in $tree - \`$here/rig.sh up\` first${rpid:+ (its pid file is stale: rig.sh up cleans it)}"
    [[ ${rdisp#:} =~ ^[0-9]+$ ]] && [ "${rdisp#:}" -ge 60 ] && [ "${rdisp#:}" -le 99 ] \
        || die "the recorded display '$rdisp' is not a rig in 60-99 - refused (never :0)"
    local c; c=$(cpus) || exit 2
    taskset -c "$c" true 2> /dev/null || die "taskset -c $c fails on this box (JAH_CPUS / testing/CPUS)"
    local cmd=("$@") a dr=""
    case "$(basename -- "$1")" in
        Jahshaka|jahlab) for a in "$@"; do case "$a" in --data-root|--data-root=*) dr=1 ;; esac; done
                         [ -n "$dr" ] || cmd+=(--data-root "$data") ;;
    esac
    mkdir -p "$data/home"
    # THE RUN IS A USER of the rig for its own life (W1): a starter's down meanwhile keeps the rig; its end releases it
    local rc; ( exec 7> "$data/.rig.lock"; flock -x 7; JAH_RIG_USER=$$ enroll )
    "$here/gpu-exclusive.sh" "${lockopts[@]}" --label "$label" \
        env DISPLAY="$rdisp" HOME="$data/home" JAHSHAKA_VR=0 JAH_CPUS="$c" JAH_RIG_DATA="$data" \
        taskset -c "$c" nice -n 10 "${cmd[@]}"
    rc=$?
    JAH_RIG_USER=$$ cmd_down release > /dev/null
    return $rc
}

cmd_status() {
    local lock=${JAH_GPU_LOCK:-/tmp/jah-gpu.lock} q
    echo "tree: $tree"
    if [ ! -f "$pidf" ]; then echo "rig: none (rig.sh up)"
    elif live; then echo "rig: UP at $rdisp - Xvfb pid $rpid, started $(ps -o lstart= -p "$rpid" 2> /dev/null)"
    else echo "rig: STALE pid file ($pidf: pid ${rpid:-?} ${rdisp:-?}) - rig.sh down or up removes it"; fi
    echo "data root: $data$([ -d "$data" ] && echo " ($(du -sh "$data" 2> /dev/null | cut -f1))" || echo " (not made yet)")"
    echo "our CPUs: $(cpus)"
    q=$(ls "$lock.q" 2> /dev/null | grep -c '^[0-9]')
    echo "the card: $(cat "$lock.holder" 2> /dev/null || echo 'no holder line') - $q ticket(s) queued ($lock)"
}

case "${1:-}" in
    up) shift; cmd_up "$@" ;;
    down) cmd_down down ;;
    release) cmd_down release ;;
    run) shift; cmd_run "$@" ;;
    status) cmd_status ;;
    display) live && echo "$rdisp" ;;
    tree) printf '%s\n' "$tree" ;;
    cpus) cpus ;;
    *) usage ;;
esac
