#!/bin/bash
# tooling.warm_app_cache (URGENT #52) - scripts/warm-app-cache.sh's NO-OP PATH: warm at its key it answers at once and
# boots nothing; cold (no stamp, a moved key, a replaced cache) it says so. On a scratch tests dir (--tests-dir), never
# the tree's shared cache; the boot itself is the script's own (not a test: once, uncapped, when the key moved).
# usage: warm_app_cache.sh <source-dir> <build-dir>
set -u
SRC=${1:?source}; BUILD=${2:?build}
W="$SRC/scripts/warm-app-cache.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
mkdir -p "$T/.shadercache"
printf 'jahshaka-shader-cache 2\nshaders 2\nfile hlms.1.bin 10 aa k1\nfile pipeline.cache 20 bb k2\n' > "$T/.shadercache/cache-manifest.txt"
fail=0
ok() { echo "  ok: $1"; }
no() { echo "  FAIL: $1"; fail=1; }
out=$("$W" "$BUILD" --check --tests-dir "$T"); rc=$?
key=$(echo "$out" | sed -n 's/^cold at \([0-9a-f]*\): .*/\1/p')
[ $rc = 3 ] && [ -n "$key" ] && echo "$out" | grep -q 'no warm-up recorded' && ok "no stamp: cold at $key, exit 3 ($out)" || no "no stamp: '$out' rc $rc"
[ "$("$W" "$BUILD" --check --tests-dir "$T" | sed -n 's/^cold at \([0-9a-f]*\):.*/\1/p')" = "$key" ] && ok "the key is the build's, the same twice" || no "the key moved between two reads"
printf '%s\n%s\n' "$key" "k1 k2 " > "$T/.shadercache.warm-key"
out=$("$W" "$BUILD" --check --tests-dir "$T"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "a stamp at the key with the cache's layer keys: warm, exit 0" || no "warm stamp: '$out' rc $rc"
t0=$(date +%s%N)
out=$("$W" "$BUILD" --tests-dir "$T"); rc=$?
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && [ $ms -lt 5000 ] && ok "THE NO-OP: warm at $key, exit 0, nothing booted, $ms ms" ||
    no "the no-op path: '$out' rc $rc in $ms ms"
printf '%s\n%s\n' "0123456789abcdef" "k1 k2 " > "$T/.shadercache.warm-key"
out=$("$W" "$BUILD" --check --tests-dir "$T"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'the key moved (warmed at 0123456789abcdef)' && ok "a stamp at another key: cold, exit 3" || no "moved key: '$out' rc $rc"
printf '%s\n%s\n' "$key" "k1 k2 " > "$T/.shadercache.warm-key"
sed -i 's/ k2$/ k3/' "$T/.shadercache/cache-manifest.txt"
out=$("$W" "$BUILD" --check --tests-dir "$T"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'replaced at another key' && ok "the shared cache replaced at another layer key: cold, exit 3" || no "replaced cache: '$out' rc $rc"
rm -rf "$T/.shadercache"
out=$("$W" "$BUILD" --check --tests-dir "$T"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'the shared cache is gone' && ok "no shared cache: cold, exit 3" || no "gone cache: '$out' rc $rc"
echo "tooling.warm_app_cache: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
