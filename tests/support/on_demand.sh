#!/bin/sh
# on_demand.sh <build dir> <target> <binary> [<argument>...] — A ROW WHOSE BINARY IS BUILT ON DEMAND RUNS IT ONLY WHEN THE BUILD's
# STAMP CLAIMS IT, OR FAILS BY NAME (ENGINE-HEADERS-1 stage 0). The sanitizer twin of the engine and the `*_asan` executables
# that link it are out of the default build (EXCLUDE_FROM_ALL: 53 of the 376 objects a Types.h edit recompiled); the lead tools
# build the targets a run's rows name (the row property JAH_BUILD_TARGETS) before the rows run. A row run any other way - a
# ctest by hand after a default build - would run a binary that is ABSENT (a skip, or a confusing exit 127) or STALE (a green
# from yesterday's engine): both are a FAILED row here, with the one command that builds it.
# THE PROOF OF "CURRENT": <build>/BUILT_FROM's `targets=` line (cmake/BuiltFrom.cmake) names the targets current at the
# stamp's state - a `--target <target>` pass of the lead tools (util.build) followed by its stamp names it; any later build at
# another state (a commit, an edit: the content id) starts the set again without it, so a binary built from older bytes reads
# stale.
# Exit: the binary's own, or 1 FAILED (not built / not claimed / no stamp), 64 usage.
[ $# -ge 3 ] || { echo "on_demand.sh: usage: on_demand.sh <build dir> <target> <binary> [<argument>...]" >&2; exit 64; }
b=$1; t=$2; shift 2
exe=$1
how="cmake --build $b --target $t, then its stamp: JAH_BUILT_REACH=$t cmake --build $b --target jah_built_reach   (the lead tools do both before the row: spec.sh test, sprint.sh ship)"
if [ ! -f "$b/BUILT_FROM" ]; then
    echo "ON DEMAND: FAILED - $b/BUILT_FROM is missing: the build never finished, or its last build failed; build it, then: $how"
    exit 1
fi
if [ ! -x "$exe" ]; then
    echo "ON DEMAND: FAILED - $exe is NOT BUILT (its target is out of the default build): $how"
    exit 1
fi
# A STALE PASS (stale= / stale_fork= not 0: a file ninja could not see was touched, the binary built from its old bytes -
# cmake/BuiltFrom.cmake) claims nothing a row may run: the stamp's own next pass rebuilds it
st=$(sed -n 's/^stale=//p' "$b/BUILT_FROM"); sf=$(sed -n 's/^stale_fork=//p' "$b/BUILT_FROM")
if [ "${st:-0}" != 0 ] || [ "${sf:-0}" != 0 ]; then
    echo "ON DEMAND: FAILED - $exe is STALE: the build's last pass was stale (stale=$st stale_fork=$sf in $b/BUILT_FROM): build again (a fork file: build-ogre.sh first), then: $how"
    exit 1
fi
targets=$(sed -n 's/^targets=//p' "$b/BUILT_FROM")
case ",$targets," in
    *",$t,"*) ;;
    *)
        echo "ON DEMAND: FAILED - $exe is STALE: the build's stamp ($b/BUILT_FROM, targets=$targets) does not claim $t at its state: $how"
        exit 1 ;;
esac
exec "$@"
