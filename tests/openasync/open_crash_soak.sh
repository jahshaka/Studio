#!/usr/bin/env sh
# open.crash_soak — the async-open teardown crash, as a repeatable measurement
# (lane OPEN-FRAMES-1, 2026-09-15).
#
# WHAT IT RUNS: the minimal repro from the diagnosis
# (~/Developer/spikes/async-open-crash/phase2) — import one sample, open it
# through the threaded path, and poll with a verb that renders NOTHING — N
# times, each in a fresh process with a fresh data root, under glibc's own heap
# checks (MALLOC_CHECK_=3 aborts on a corrupt chunk header at the next
# malloc/free; MALLOC_PERTURB_ fills freed memory so a use-after-free reads
# poison instead of plausible data).
#
# WHY IT IS NIGHTLY AND NOT A MERGE GATE: on the BASE it is probabilistic — 9
# failures in 12 runs when the install ran with no frames, and the same code
# passed 3 runs in 12. A suite that fails one run in four is not a gate. Its
# value is the OTHER number: 0 in 12 with the fix, repeatedly, and any
# regression that takes the boundary drive back out returns the rate to
# roughly three quarters. Run it when the open path, the scene teardown or the
# engine's resource handling changes.
#
# $1 = the binary, $2 = the scenes directory, $3 = the scratch root, $4 = N.
set -u
BIN="$1"; SCENES="$2"; ROOT="$3"; N="${4:-12}"
fail=0
mkdir -p "$ROOT"
cat > "$ROOT/soak.js" <<JS
var a = project.importArchive("$SCENES/Matcaps.zip");
project.openAsync(a.guid);
var t = 0;
while (project.openState() === "opening") {
    try { editor.properties({ tab: "world" }); } catch (e) {}
    if (++t > 40000) break;
}
console.log("[soak] the open finished in " + t + " turns");
for (var k = 0; k < 200; ++k) { try { editor.properties({ tab: "world" }); } catch (e) {} }
console.log("[soak] done");
JS
i=1
while [ "$i" -le "$N" ]; do
    home="$ROOT/run-$i"
    rm -rf "$home"; mkdir -p "$home/run" "$home/dr"
    ( cd "$home/run" && HOME="$home" MALLOC_CHECK_=3 MALLOC_PERTURB_=165 \
      "$BIN" --data-root "$home/dr" --script "$ROOT/soak.js" > "$ROOT/run-$i.log" 2>&1 )
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL: run $i exited $rc"
        grep -aE "double free|free\(\): |malloc\(\): |corrupted|munmap_chunk|Segmentation" "$ROOT/run-$i.log" | head -2
        fail=$((fail+1))
    elif ! grep -aq "\[soak\] done" "$ROOT/run-$i.log"; then
        echo "FAIL: run $i never reached the end of its script"
        fail=$((fail+1))
    else
        echo "ok:   run $i clean"
    fi
    rm -rf "$home"
    i=$((i+1))
done
echo "open.crash_soak: $fail failure(s) in $N runs"
[ "$fail" -eq 0 ]
