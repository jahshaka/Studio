#!/bin/bash
# warm_app_boot.sh <app> <png> <sets-file> <dump-dir> — THE APP CACHE's -off BOOT, ITS PROCESSES (HARNESS-1;
# scripts/warm-app-cache.sh runs it inside fresh_home.sh --warm, the driver's cache off, under the card's lock):
#   1. `<app> --engine-selftest <png>` - the startup gate's born World at Epic with every feature, and the self-test's six
#      poses; its Hlms permutations dumped into <dump-dir> (JAHSHAKA_HLMS_DEBUG_DIR: source.shaders_spirv_valid's input);
#   2. then ONE `--script tests/support/warm_tier.js` boot per line of <sets-file> (<tier>|<needs>: every test tier the
#      app and VR rows declare, tests/CMakeLists.txt) - a row's tier draws its own permutations of the born World, which
#      the all-features self-test never builds (log.engine: 3 pbs permutations, 256,658 bytes of pipelines a run).
# All in ONE home, in order: each process loads the cache the one before it saved; the boot's promotion is fresh_home's.
# Exit: the first failing process's code.
set -u
APP=${1:?app}; PNG=${2:?png}; SETS=${3:?sets file}; DUMP=${4:?dump dir}
HERE=$(cd "$(dirname "$0")" && pwd)
JAHSHAKA_HLMS_DEBUG_DIR="$DUMP/" "$APP" --engine-selftest "$PNG" || exit $?
[ -f "$SETS" ] || { echo "warm_app_boot: no test-boot sets ($SETS) - the self-test alone"; exit 0; }
n=0
while IFS='|' read -r tier needs; do
    [ -n "$tier" ] || continue
    n=$((n + 1))
    t0=$(date +%s)
    JAHSHAKA_TEST_TIER="$tier" JAHSHAKA_TEST_NEEDS="$needs" "$APP" --script "$HERE/warm_tier.js" > "$DUMP/../warm-tier-$n.log" 2>&1
    rc=$?
    echo "warm_app_boot: test tier $tier, needs '$needs': exit $rc, $(( $(date +%s) - t0 )) s"
    [ $rc = 0 ] || { tail -20 "$DUMP/../warm-tier-$n.log"; exit $rc; }
done < "$SETS"
exit 0
