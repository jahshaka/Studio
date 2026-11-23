#!/bin/bash
# tooling.row_lock_env - A ROW AND A HOLD's COMMAND CARRY NO CALL's IDENTITY OF THE LOCK (CARD-QUEUE-1 M1, the second engineer's
# review: the lead's spec.sh test once put its call's place, JAH_GPU_PLACE, into every row's environment and the lock's own rows
# went RED - their toys' holds took the call's place). The JAH_GPU_* variables a hold's command and a row may see are the ones
# scripts/gpu-exclusive.sh's header names on its line "A HOLD's COMMAND SEES:" (the one list); any other - the call's place
# JAH_GPU_PLACE, the caller JAH_GPU_CALLER, a toy's allowance - is a leak:
#   (1) this row's own environment (run by ctest under whatever tool runs it: a lead tool's gate, rig.sh run, a hand ctest);
#   (2) the command of a hold on a PRIVATE lock started with JAH_GPU_PLACE and JAH_GPU_CALLER set: it sees neither (the lock
#       reads them for its own hold and drops them before its command).
# usage: row_lock_env.sh <the tree's scripts/gpu-exclusive.sh>
set -u
LOCKSH=${1:?gpu-exclusive.sh}
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
allowed=$(sed -n "s/^# A HOLD's COMMAND SEES: //p" "$LOCKSH" | tr -d ',;.')
leaks() {   # the JAH_GPU_* names of an `env` listing on stdin that the list does not name
    local v l=""
    for v in $(sed -n 's/^\(JAH_GPU_[A-Z_]*\)=.*/\1/p'); do case " $allowed " in *" $v "*) ;; *) l="$l $v" ;; esac; done
    echo "${l# }"
}
want "$([ -n "$allowed" ] && echo 1)" "the lock names what a hold's command sees: '$allowed'"
own=$(env | leaks)
want "$([ -z "$own" ] && echo 1)" "(1) this row's environment carries no other JAH_GPU_* variable (${own:-none})"
T=$(mktemp -d "${TMPDIR:-/tmp}/row-lock-env.XXXXXX"); trap 'rm -rf "$T"' EXIT
sleep 300 & SP=$!   # (a live process for the toy's place key: a dead one's place is removed at once)
st=$(sed 's/.*) //' "/proc/$SP/stat" | cut -d' ' -f20)
cmd=$(env -u JAH_GPU_LOCK_HELD -u DISPLAY JAH_GPU_LOCK="$T/lock" JAH_GPU_PLACE="place:$SP-$st" JAH_GPU_CALLER=$$ "$LOCKSH" --label E env | leaks)
kill "$SP" 2> /dev/null
want "$([ -z "$cmd" ] && echo 1)" "(2) a hold started with the call's place and caller: its command sees neither (${cmd:-none})"
# (3) THE TWO DOORS GIVE ONE ENVIRONMENT (COLD-BOOT-1's feature end, 2026-11-22: under spec.sh test every row ran with NO shared
# driver cache - the rows run under util.CardLock's hold, JAH_GPU_LOCK_HELD=1, and the lock returned on its nested paths before it
# exported __GL_SHADER_DISK_CACHE_PATH; shadercache.seed_boot.twin REFUSED "no warm driver cache"): a hold's command sees the same
# driver cache path and size, and the same VR default, through the lock's own door (rig.sh run), under a holder (the CardLock
# door: the scope's path) and nested inside a hold (JAH_GPU_HOLD set: the exec path)
seen() { env -u __GL_SHADER_DISK_CACHE_PATH -u __GL_SHADER_DISK_CACHE_SIZE -u JAHSHAKA_VR -u DISPLAY "$@" "$LOCKSH" --label E \
             sh -c 'echo "${__GL_SHADER_DISK_CACHE_PATH:-unset} ${__GL_SHADER_DISK_CACHE_SIZE:-unset} ${JAHSHAKA_VR:-unset}"'; }
own=$(seen -u JAH_GPU_LOCK_HELD JAH_GPU_LOCK="$T/lock" JAH_GPU_NO_SCOPE=1)
held=$(seen JAH_GPU_LOCK="$T/lock" JAH_GPU_LOCK_HELD=1 JAH_GPU_NO_SCOPE=1)
nest=$(seen JAH_GPU_LOCK="$T/lock" JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=jahgpu-toy)
want "$(case "$own" in *unset*) ;; *) [ "$own" = "$held" ] && [ "$own" = "$nest" ] && echo 1 ;; esac)" \
     "(3) the two doors give a hold's command one environment: its own door '$own', under a holder '$held', nested '$nest'"
# (4) NO HOLD's COMMAND REACHES A DEBUGINFOD SERVER (RIG-DEBUGINFOD-1, URGENT #233: the box's login shells set DEBUGINFOD_URLS and
# nsys under a card hold fetched 41 symbol files through it): started with the variable set, a hold's command sees it UNSET through
# all three doors (rig.sh run's own `env -u` needs a live display this row has not; it passes through this same lock)
dbg() { env "$@" DEBUGINFOD_URLS=https://debuginfod.invalid "$LOCKSH" --label E sh -c 'echo "${DEBUGINFOD_URLS-unset}"'; }
d_own=$(dbg -u JAH_GPU_LOCK_HELD -u DISPLAY JAH_GPU_LOCK="$T/lock" JAH_GPU_NO_SCOPE=1)
d_held=$(dbg -u DISPLAY JAH_GPU_LOCK="$T/lock" JAH_GPU_LOCK_HELD=1 JAH_GPU_NO_SCOPE=1)
d_nest=$(dbg -u DISPLAY JAH_GPU_LOCK="$T/lock" JAH_GPU_LOCK_HELD=1 JAH_GPU_HOLD=jahgpu-toy)
want "$([ "$d_own $d_held $d_nest" = "unset unset unset" ] && echo 1)" \
     "(4) a hold started with DEBUGINFOD_URLS set: its command sees it unset through each door ('$d_own' '$d_held' '$d_nest')"
echo "tooling.row_lock_env: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
exit $((fails > 0))
