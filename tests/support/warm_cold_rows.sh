#!/bin/bash
# warm_cold_rows.sh <rows-file> <deadline> <log-dir> - THE COLD ENGINE ROWS' EXES, EACH RUN ONCE UNCAPPED (URGENT #119,
# HARNESS-4; scripts/warm-app-cache.sh's cold-on boot runs it under the card's lock, the driver's cache ON):
# <rows-file> is <build>/tests/test-cold-engine-rows.txt (tests/CMakeLists.txt _jah_cold_engine_row: one CLASS engine row
# declared COLD a line, <row> TAB <working dir> TAB <exe> [TAB <arg>]...). Such a row boots on no shared pipeline cache,
# so every pipeline it makes is the NVIDIA driver's, served only from the driver's PER-EXECUTABLE disk cache; after a
# shader change its first run compiles them cold (rq_reflect's ahead build alone 117-121 s, BUGS-8's gdb), the 60-s cap
# kills it and nothing is stored. Run here to its own end - the driver stores its compiles DURING the run (measured,
# #102b) - so the rows then run warm under the cap.
# The exes run AT ONCE (the driver compiles on its own thread per process; one card, the lock held around all of them),
# each in its row's working directory as ctest runs it, bounded by <deadline> (the unix second the call's budget ends,
# read HERE, after the card's wait - never the 60-s cap: the first proof's bound, fixed before a 35-s wait, cut two cold
# exes at 251 s of their run).
# A row's own red is printed, not judged here (its cache was stored all the same); an exe the bound killed = exit 1.
set -u
ROWS=${1:?rows file}; DEADLINE=${2:?deadline}; LOGS=${3:?log dir}
BOUND=$(( DEADLINE - $(date +%s) )); [ "$BOUND" -ge 1 ] || BOUND=1
mkdir -p "$LOGS"
[ -s "$ROWS" ] || { echo "warm_cold_rows: no cold engine rows ($ROWS)"; exit 0; }
pids=(); names=(); t0=$(date +%s)
while IFS= read -r line; do
    [ -n "$line" ] || continue
    IFS=$'\t' read -r -a f <<< "$line"
    row=${f[0]}; wd=${f[1]}; cmd=("${f[@]:2}")
    ( cd "$wd" && exec env -u JAHSHAKA_SHADER_CACHE_DIR -u __GL_SHADER_DISK_CACHE \
          timeout --kill-after=10 "$BOUND" "${cmd[@]}" ) > "$LOGS/$row.log" 2>&1 &
    pids+=($!); names+=("$row")
done < "$ROWS"
killed=0
for i in "${!pids[@]}"; do
    wait "${pids[$i]}"; rc=$?
    echo "warm_cold_rows: ${names[$i]}: exit $rc, $(( $(date +%s) - t0 )) s$( [ $rc = 124 ] && echo " - KILLED at the bound (${BOUND} s), its driver cache incomplete")"
    [ $rc = 124 ] || [ $rc = 137 ] && killed=1
done
exit $killed
