#!/bin/bash
# warm_cold_rows.sh <rows-file> <deadline> <log-dir> - THE COLD ROWS' COMMANDS, EACH RUN ONCE UNCAPPED (URGENT #119 +
# #122, HARNESS-4; scripts/warm-app-cache.sh's cold-on boot runs it under the card's lock, the driver's cache ON):
# <rows-file> is <build>/tests/test-cold-rows.txt (tests/CMakeLists.txt _jah_cold_row: one row declared COLD a line,
# <row> TAB <working dir> TAB <env: VAR=value or -VAR, joined by 0x1F> TAB <exe> [TAB <arg>]...). Such a row makes its
# own cache: an engine row's every pipeline comes from the NVIDIA driver's PER-EXECUTABLE disk cache (after a shader
# change rq_reflect's ahead build alone took 117-121 s, BUGS-8's gdb); an app row keeps a cache keyed on the shaders
# (shader.async_after_open.seed: a 64-s boot when its seed is stale). The 60-s cap kills either and nothing is stored.
# Run here to its own end - the driver stores its compiles DURING the run (measured, #102b); the seed row makes its
# seed - so the rows then run warm under the cap. Each with its row's test tier (the env field), as ctest gives it.
# ONE ROW AT A TIME, EACH ITS OWN HOLD OF THE CARD (SHIP-CUT-2 0b, URGENT #155): the exes ran AT ONCE under one hold, and
# in photon-7's tree (2026-11-03 03:52) six of them - four full app boots: both seeds re-seeding, shadercache.app.cold,
# samples.cold_open.import - shared the card: .cold's prime died VK_ERROR_OUT_OF_DEVICE_MEMORY 35 s in (2.5 GB its own),
# the stamp was written over it, and the row then met a 4.5-MB driver cache and timed out at 60.11 s in every call. Every
# GPU row takes the whole card (tests/cmake/vram_rows.cmake, #44), and so does a prime: each row's command runs alone,
# through scripts/gpu-exclusive.sh (--wait-max $JAH_COLD_WAIT, default 600; the Unreal session's holds fit between two
# rows), in its row's working directory as ctest runs it, bounded by <deadline> (the unix second the call's budget ends,
# read after ITS card wait - never the 60-s cap).
# EACH ROW JUDGED (one line each, the stamp's to write - scripts/warm-app-cache.sh): "PRIMED" when its run reached its own
# end - exit 0, or a red of its own law (its cache stored all the same, the red printed); a SEED row (`--seed`) only when
# its own `--seed-check` then says fresh. "NOT PRIMED" otherwise: killed at the call's end, a signal, or the engine's own
# crash or device-loss line in its log (`Jahshaka crashed`, VK_ERROR_OUT_OF_DEVICE_MEMORY, VK_ERROR_DEVICE_LOST).
# EXIT: 0 every row run PRIMED; 1 a prime CRASHED (a defect: the row's own, never environmental); 75 the card stayed held
# past a row's wait or the call's end came - the rows left are named and the next call primes them.
# THE STARTUP SEEDS' OWN FRESHNESS (URGENT #139): a seed row (its command `... --seed [--seed-dir <name>]`) keys its seed on
# the Studio build too (the app's global pass), so a Studio commit makes it stale while the cold-on stamp still holds:
#   warm_cold_rows.sh --stale-seeds <rows-file>  prints the seed rows whose `--seed-check` (the same command, ~0.2 s, no card)
#                                                says stale, one a line - scripts/warm-app-cache.sh's cold-on reads it;
#   JAH_COLD_ONLY="<row> ..." (the environment)  runs those rows alone (the stamp held: only the stale seeds re-seed, the
#                                                other cold rows' caches are this key's already).
set -u
each_row() {   # <rows-file> <fn>: fn row wd cmd... with the row's env applied (the env field), for every line
    local rows=$1 fn=$2 line row wd
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        readarray -t -d $'\t' f < <(printf '%s' "$line")
        row=${f[0]}; wd=${f[1]}
        unsets=(-u JAHSHAKA_SHADER_CACHE_DIR -u __GL_SHADER_DISK_CACHE); sets=()
        readarray -t -d $'\x1f' ev < <(printf '%s' "${f[2]}")
        for e in "${ev[@]}"; do
            case "$e" in "") ;; -*) unsets+=(-u "${e#-}") ;; *) sets+=("$e") ;; esac
        done
        "$fn" "$row" "$wd" "${f[@]:3}"
    done < "$rows"
}
if [ "${1:-}" = "--stale-seeds" ]; then
    stale_check() {   # row wd cmd...: the row printed when its seed is stale
        local row=$1 wd=$2; shift 2
        local cmd=() a seedrow=0
        for a in "$@"; do [ "$a" = --seed ] && { a=--seed-check; seedrow=1; }; cmd+=("$a"); done
        [ "$seedrow" = 1 ] || return 0
        ( cd "$wd" 2>/dev/null && exec env "${unsets[@]}" "${sets[@]}" "${cmd[@]}" ) < /dev/null > /dev/null 2>&1 || echo "$row"
    }
    [ -s "${2:?rows file}" ] && each_row "$2" stale_check
    exit 0
fi
ROWS=${1:?rows file}; DEADLINE=${2:?deadline}; LOGS=${3:?log dir}
LOCK="$(cd "$(dirname "$0")/../.." && pwd)/scripts/gpu-exclusive.sh"; WAITMAX=${JAH_COLD_WAIT:-600}
mkdir -p "$LOGS"
[ -s "$ROWS" ] || { echo "warm_cold_rows: no cold rows ($ROWS)"; exit 0; }
crashed=0; again=0
while IFS= read -r -u 3 line; do
    [ -n "$line" ] || continue
    readarray -t -d $'\t' f < <(printf '%s' "$line")   # empty fields kept (a tab is IFS whitespace: read would drop them)
    row=${f[0]}; wd=${f[1]}; cmd=("${f[@]:3}")
    if [ -n "${JAH_COLD_ONLY:-}" ] && ! printf '%s\n' $JAH_COLD_ONLY | grep -qxF "$row"; then
        echo "warm_cold_rows: $row: skipped (primed at this key)"; continue
    fi
    if [ "$again" = 1 ]; then echo "warm_cold_rows: $row: NOT RUN - left for the next call"; continue; fi
    left=$(( DEADLINE - $(date +%s) ))
    if [ "$left" -lt "${JAH_COLD_MIN_LEFT:-30}" ]; then   # (the knob: tooling.warm_app_cache's toy bound, seconds)
        echo "warm_cold_rows: $row: NOT RUN - the call's end (${left} s left): the next call primes it"; again=1; continue
    fi
    unsets=(-u JAHSHAKA_SHADER_CACHE_DIR -u __GL_SHADER_DISK_CACHE); sets=()   # env takes its -u options BEFORE any VAR=value
    readarray -t -d $'\x1f' ev < <(printf '%s' "${f[2]}")
    for e in "${ev[@]}"; do
        case "$e" in "") ;; -*) unsets+=(-u "${e#-}") ;; *) sets+=("$e") ;; esac
    done
    w=$(( left - ${JAH_COLD_MIN_LEFT:-30} )); [ "$w" -ge 1 ] || w=1; [ "$w" -le "$WAITMAX" ] || w=$WAITMAX
    log="$LOGS/$row.log"; t0=$(date +%s)
    ( cd "$wd" && exec env "${unsets[@]}" "${sets[@]}" "$LOCK" --wait-max "$w" --label "warm-app-cache (cold-on: $row)" \
          bash -c 'b=$(( $1 - $(date +%s) )); [ "$b" -ge 1 ] || b=1; shift; exec timeout --kill-after=10 "$b" "$@"' \
          cold-bound "$DEADLINE" "${cmd[@]}" ) < /dev/null > "$log" 2>&1
    rc=$?
    wall=$(( $(date +%s) - t0 ))
    waited=$(sed -n 's/^gpu-lock: waited \([0-9]*\)\(\.[0-9]*\)\{0,1\} s$/\1/p' "$log" | head -1); waited=${waited:-0}
    if [ $rc = 75 ] && grep -q '^gpu-lock: the card stayed held' "$log"; then
        echo "warm_cold_rows: $row: NOT RUN - $(grep -m1 '^gpu-lock: the card stayed held' "$log" | sed 's/^gpu-lock: //')"
        again=1; continue
    fi
    ran=$(( wall - waited ))
    bad=$(grep -m1 -oE 'Jahshaka crashed|VK_ERROR_OUT_OF_DEVICE_MEMORY|VK_ERROR_DEVICE_LOST' "$log")
    seedrow=0; chk=(); for a in "${cmd[@]}"; do [ "$a" = --seed ] && { seedrow=1; a=--seed-check; }; chk+=("$a"); done
    if [ $rc = 124 ] || [ $rc = 137 ]; then
        echo "warm_cold_rows: $row: NOT PRIMED - killed at the call's end after ${ran} s (exit $rc): the next call primes it"
        again=1
    elif [ -n "$bad" ] || [ $rc -ge 128 ]; then
        echo "warm_cold_rows: $row: NOT PRIMED - its run CRASHED (exit $rc${bad:+, $bad}) after ${ran} s - its log $log"
        crashed=1
    elif [ "$seedrow" = 1 ] && ! ( cd "$wd" && exec env "${unsets[@]}" "${sets[@]}" "${chk[@]}" ) < /dev/null > /dev/null 2>&1; then
        echo "warm_cold_rows: $row: NOT PRIMED - the seed is not fresh after its run (exit $rc, ${ran} s) - its log $log"
        crashed=1
    else
        red=$( [ $rc = 0 ] || grep -m1 -E '^FAIL|RESULT: [0-9]+ FAILURE' "$log" )
        echo "warm_cold_rows: $row: PRIMED - its run ${ran} s (exit $rc; the card's wait ${waited} s)${red:+; its own red, printed: $red}"
    fi
done 3< "$ROWS"
[ "$crashed" = 1 ] && exit 1
[ "$again" = 1 ] && exit 75
exit 0
