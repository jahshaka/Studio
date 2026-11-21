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
echo "tooling.row_lock_env: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"
exit $((fails > 0))
