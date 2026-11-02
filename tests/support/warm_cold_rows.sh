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
# The exes run AT ONCE (the driver compiles on its own thread per process; one card, the lock held around all of them),
# each in its row's working directory as ctest runs it, bounded by <deadline> (the unix second the call's budget ends,
# read HERE, after the card's wait - never the 60-s cap: the first proof's bound, fixed before a 35-s wait, cut two cold
# exes at 251 s of their run).
# A row's own red is printed, not judged here (its cache was stored all the same); an exe the bound killed = exit 1.
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
BOUND=$(( DEADLINE - $(date +%s) )); [ "$BOUND" -ge 1 ] || BOUND=1
mkdir -p "$LOGS"
[ -s "$ROWS" ] || { echo "warm_cold_rows: no cold rows ($ROWS)"; exit 0; }
pids=(); names=(); t0=$(date +%s)
while IFS= read -r line; do
    [ -n "$line" ] || continue
    readarray -t -d $'\t' f < <(printf '%s' "$line")   # empty fields kept (a tab is IFS whitespace: read would drop them)
    row=${f[0]}; wd=${f[1]}; cmd=("${f[@]:3}")
    if [ -n "${JAH_COLD_ONLY:-}" ] && ! printf '%s\n' $JAH_COLD_ONLY | grep -qxF "$row"; then
        echo "warm_cold_rows: $row: skipped (its cache is this key's; only the stale seeds run: $JAH_COLD_ONLY)"; continue
    fi
    unsets=(-u JAHSHAKA_SHADER_CACHE_DIR -u __GL_SHADER_DISK_CACHE); sets=()   # env takes its -u options BEFORE any VAR=value
    readarray -t -d $'\x1f' ev < <(printf '%s' "${f[2]}")
    for e in "${ev[@]}"; do
        case "$e" in "") ;; -*) unsets+=(-u "${e#-}") ;; *) sets+=("$e") ;; esac
    done
    ( cd "$wd" && exec env "${unsets[@]}" "${sets[@]}" timeout --kill-after=10 "$BOUND" "${cmd[@]}" ) < /dev/null > "$LOGS/$row.log" 2>&1 &
    pids+=($!); names+=("$row")
done < "$ROWS"
killed=0
for i in "${!pids[@]}"; do
    wait "${pids[$i]}"; rc=$?
    echo "warm_cold_rows: ${names[$i]}: exit $rc, $(( $(date +%s) - t0 )) s$( [ $rc = 124 ] && echo " - KILLED at the bound (${BOUND} s), its cache incomplete")"
    [ $rc = 124 ] || [ $rc = 137 ] && killed=1
done
exit $killed
