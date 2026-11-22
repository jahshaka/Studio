#!/bin/sh
# on_demand.sh <build dir> <target> <binary> [<argument>...] — A ROW WHOSE BINARY IS BUILT ON DEMAND RUNS IT ONLY AS BUILT FROM
# THIS BUILD's SOURCES, OR FAILS BY NAME (ENGINE-HEADERS-1 stage 0). The sanitizer twin of the engine and the `*_asan`
# executables that link it are out of the default build (EXCLUDE_FROM_ALL: 53 of the 376 objects a Types.h edit recompiled);
# the lead tools build the targets a run's rows name (the row property JAH_BUILD_TARGETS) before the rows run. A row run any
# other way - a ctest by hand after a default build - would run a binary that is ABSENT (a skip, or a confusing exit 127) or
# STALE (a green from yesterday's engine): both are a FAILED row here, with the one command that builds it.
# THE PROOF OF "CURRENT": the target `<target>` (tests/CMakeLists.txt jah_on_demand) builds the binary and then copies the
# build's stamp <build>/BUILT_FROM (the commits and the content map of the sources the last default build compiled) to
# <binary>.built_from; the binary is current when that copy equals the stamp now. (A default build after an edit rewrites the
# stamp, so a twin not rebuilt since reads stale; a twin rebuilt reads current.)
# Exit: the binary's own, or 1 FAILED (not built / stale / no stamp), 64 usage.
[ $# -ge 3 ] || { echo "on_demand.sh: usage: on_demand.sh <build dir> <target> <binary> [<argument>...]" >&2; exit 64; }
b=$1; t=$2; shift 2
exe=$1
how="cmake --build $b --target $t   (the lead tools build it before the row: spec.sh test, sprint.sh ship)"
if [ ! -f "$b/BUILT_FROM" ]; then
    echo "ON DEMAND: FAILED - $b/BUILT_FROM is missing: the build has never finished a default pass; build it, then: $how"
    exit 1
fi
if [ ! -x "$exe" ]; then
    echo "ON DEMAND: FAILED - $exe is NOT BUILT (its target is out of the default build): $how"
    exit 1
fi
if [ ! -f "$exe.built_from" ]; then
    echo "ON DEMAND: FAILED - $exe has no stamp ($exe.built_from): it was not built by its on-demand target: $how"
    exit 1
fi
if ! cmp -s "$exe.built_from" "$b/BUILT_FROM"; then
    echo "ON DEMAND: FAILED - $exe is STALE: built at another state of the sources than the build's stamp ($b/BUILT_FROM): $how"
    exit 1
fi
exec "$@"
