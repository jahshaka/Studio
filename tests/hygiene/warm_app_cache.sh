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
# THE STAMP ONLY ON A PROMOTION (the lead's gap, #52): a FAKE app (--app; it compiles nothing, touches no card - the
# lock is declared held - and needs no display) grows the home's cache; fresh_home's own rules promote it or not.
# A scratch build-shaped dir: fresh_home shares a cache only under a `tests` dir beside a CMakeCache.txt.
B="$T/b"; mkdir -p "$B/tests/.shadercache"; touch "$B/CMakeCache.txt"
manifest() { printf 'jahshaka-shader-cache 2\nsaved %s\nshaders %s\nfile hlms.1.bin 10 aa k1\nfile pipeline.cache 20 bb k2\n' "$1" "$2"; }
manifest 1 2 > "$B/tests/.shadercache/cache-manifest.txt"
cat > "$T/fake-app" <<'APP'
#!/bin/sh
# the fake boot: the home's cache grows (a NEW manifest - its seeded one is a hard link of the shared one); with
# FAKE_RACE set, another home's promotion lands first (the shared manifest rewritten at the same keys)
c="$HOME/.local/share/Jahshaka/shadercache"
mkdir -p "$c"; rm -f "$c/cache-manifest.txt"
printf 'jahshaka-shader-cache 2\nsaved 2\nshaders 3\nfile hlms.1.bin 10 aa k1\nfile pipeline.cache 20 bb k2\n' > "$c/cache-manifest.txt"
if [ -n "${FAKE_RACE:-}" ]; then
    rm -f "$FAKE_RACE/cache-manifest.txt"
    printf 'jahshaka-shader-cache 2\nsaved 9\nshaders 4\nfile hlms.1.bin 10 aa k1\nfile pipeline.cache 20 bb k2\n' > "$FAKE_RACE/cache-manifest.txt"
fi
exit 0
APP
chmod +x "$T/fake-app"
out=$(FAKE_RACE="$B/tests/.shadercache" DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" --app "$T/fake-app"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q 'NOT promoted' && [ ! -e "$B/tests/.shadercache.warm-key" ] &&
    ok "a promotion fresh_home SKIPS (its rule 3, NOT replaced): the boot passed, NO stamp" || no "skipped promotion: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'no warm-up recorded' && ok "...so the next call reads cold and warms again" || no "after the skip: '$out' rc $rc"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" --app "$T/fake-app"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "warm-app-cache: warm at $key" && [ -e "$B/tests/.shadercache.warm-key" ] &&
    grep -q 'shaders 3' "$B/tests/.shadercache/cache-manifest.txt" &&
    ok "a promotion that lands (the shared cache replaced by the boot's grown one): stamped" || no "landed promotion: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "...and the next call reads warm" || no "after the landing: '$out' rc $rc"
echo "tooling.warm_app_cache: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
