#!/bin/bash
# tooling.warm_app_cache (URGENT #52) - scripts/warm-app-cache.sh's NO-OP PATH: warm at its key it answers at once and
# boots nothing; cold (no stamp, a moved key, a replaced cache) it says so - for each of its FOUR boots, the app rows' and
# the engine rows' caches with the driver's cache off and each exe's driver cache after it (PHOTON-7 11-2; the two-boot
# law #100/#102b; one boot a call, the card's wait bounded per boot, #106). On a scratch tests dir (--tests-dir), never
# the tree's shared cache; the boot itself is the script's own (not a test: once, uncapped, when the key moved).
# usage: warm_app_cache.sh <source-dir> <build-dir>
set -u
SRC=${1:?source}; BUILD=${2:?build}
W="$SRC/scripts/warm-app-cache.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
CK="$SRC/tests/support/shadercache_check.py"
# A TOY CACHE THAT IS ONE GENERATION (URGENT #117): its two files and a manifest naming them at their length and the
# engine's hash - the warm reads a cache that fails its own manifest as cold
# toy_cache <dir> <saved> <shaders>
toy_cache() {
    mkdir -p "$1"; printf '0123456789' > "$1/hlms.1.bin"; printf 'abcdefghijklmnopqrst' > "$1/pipeline.cache"
    printf 'jahshaka-shader-cache 2\nsaved %s\nshaders %s\nfile hlms.1.bin 10 %s k1\nfile pipeline.cache 20 %s k2\n' "$2" "$3" \
        "$(python3 -I "$CK" --hash "$1/hlms.1.bin")" "$(python3 -I "$CK" --hash "$1/pipeline.cache")" > "$1/cache-manifest.txt"
}
toy_cache "$T/.shadercache" 1 2
toy_cache "$T/.shadercache-engine" 1 2
fail=0
ok() { echo "  ok: $1"; }
no() { echo "  FAIL: $1"; fail=1; }
# the exes the -on stamps name (the driver's per-exe key is the exe's name): two fakes, the same in every call below
printf '#!/bin/sh\nexit 0\n' > "$T/app"; printf '#!/bin/sh\nexit 0\n#lab\n' > "$T/lab"; chmod +x "$T/app" "$T/lab"
cp "$T/app" "$T/app2"
: > "$T/cold-none"   # no cold engine row: cold-on has nothing to warm (its own cases at the end)
X=(--app "$T/app" --lab "$T/lab" --cold-rows "$T/cold-none")
eid() { basename "$1"; }
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
key=$(echo "$out" | sed -n 's/^cold at \([0-9a-f]*\): .*/\1/p')
[ $rc = 3 ] && [ -n "$key" ] && echo "$out" | grep -q "the app rows' cache (app-off): no warm-up recorded" &&
    echo "$out" | grep -q "the lab exe's driver cache (engine-on): no driver-cache boot recorded" &&
    ok "no stamp: cold at $key, every boot named, exit 3" || no "no stamp: '$out' rc $rc"
[ "$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" | sed -n 's/^cold at \([0-9a-f]*\):.*/\1/p')" = "$key" ] &&
    ok "the key is the build's, the same twice" || no "the key moved between two reads"
# the app-off stamp's third line: the digest of the rows' test tiers (<build>/tests/test-boot-sets.txt)
SD="sets $( [ -f "$BUILD/tests/test-boot-sets.txt" ] && sha256sum < "$BUILD/tests/test-boot-sets.txt" | cut -c1-16 || echo none)"
printf '%s\n%s\n' "$key" "k1 k2 " > "$T/.shadercache.warm-key"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" --only app-off); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the rows' test tiers changed since" &&
    ok "an app-off stamp without the rows' test tiers: cold, exit 3" || no "no sets line: '$out' rc $rc"
printf '%s\n%s\n%s\n' "$key" "k1 k2 " "$SD" > "$T/.shadercache.warm-key"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" --only app-off); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "--only app-off answers for that boot alone: warm, exit 0" ||
    no "--only app-off: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" --only app); rc=$?
[ $rc = 3 ] && [ "$out" = "cold at $key: the app exe's driver cache (app-on): no driver-cache boot recorded" ] &&
    ok "the app's -off boot warm, its -on boot unstamped: the pair reads cold, exit 3" || no "app-on unstamped: '$out' rc $rc"
printf '%s\n%s\n' "$key" "$(eid "$T/app")" > "$T/.shadercache.driver-key"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 3 ] && [ "$out" = "cold at $key: the engine rows' cache (engine-off): no warm-up recorded; the lab exe's driver cache (engine-on): no driver-cache boot recorded" ] &&
    ok "the app's two boots warm, the engine's unstamped: cold, exit 3" || no "engine unstamped: '$out' rc $rc"
printf '%s\n%s\n' "$key" "k1 k2 " > "$T/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$key" "$(eid "$T/lab")" > "$T/.shadercache-engine.driver-key"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "all four stamps at the key: warm, exit 0" || no "warm stamps: '$out' rc $rc"
t0=$(date +%s%N)
out=$("$W" "$BUILD" --tests-dir "$T" "${X[@]}"); rc=$?
ms=$(( ($(date +%s%N) - t0) / 1000000 ))
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && [ $ms -lt 5000 ] && ok "THE NO-OP: warm at $key, exit 0, nothing booted, $ms ms" ||
    no "the no-op path: '$out' rc $rc in $ms ms"
touch -d '2001-01-01' "$T/app"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" --only app); rc=$?
[ $rc = 0 ] && ok "a relinked app exe of the same name: its driver cache still warm (the driver keys on the name), exit 0" ||
    no "relinked exe: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$T" --app "$T/app2" --lab "$T/lab" --only app); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "(app-on): stored for another exe (app)" && ok "an exe of another name: its driver cache reads cold, exit 3" ||
    no "another exe: '$out' rc $rc"
printf '%s\n%s\n' "$key" "$(eid "$T/app")" > "$T/.shadercache.driver-key"
printf '%s\n%s\n' "0123456789abcdef" "k1 k2 " > "$T/.shadercache.warm-key"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'the key moved (warmed at 0123456789abcdef)' && ok "a stamp at another key: cold, exit 3" || no "moved key: '$out' rc $rc"
printf '%s\n%s\n' "$key" "0123456789abcdef" > "$T/.shadercache.driver-key"
out=$("$W" "$BUILD" --tests-dir "$T" "${X[@]}" --only app-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'call --only app-off first; nothing booted' &&
    ok "an -on boot asked for alone on a cold -off cache: refused, nothing booted, exit 3" || no "-on before -off: '$out' rc $rc"
printf '%s\n%s\n%s\n' "$key" "k1 k2 " "$SD" > "$T/.shadercache.warm-key"
printf '%s\n%s\n' "$key" "$(eid "$T/app")" > "$T/.shadercache.driver-key"
# A TORN SHARED CACHE (URGENT #117): every stamp at the key and the layer keys as recorded, but a file of other bytes
# than its manifest's hash (the same length) - the engine boots cold on it, so the warm reads it cold
cp "$T/.shadercache-engine/pipeline.cache" "$T/pipeline.good"
printf 'ABCDEFGHIJKLMNOPQRST' > "$T/.shadercache-engine/pipeline.cache"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the engine rows' cache (engine-off): the shared cache fails its own manifest (pipeline.cache: its hash" &&
    ! echo "$out" | grep -q '(app-off)' &&
    ok "a TORN shared cache (a file of other bytes, the stamps and keys as recorded): that boot cold, exit 3" ||
    no "torn cache: '$out' rc $rc"
mv "$T/pipeline.good" "$T/.shadercache-engine/pipeline.cache"
rm "$T/.shadercache-engine/hlms.1.bin"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}" --only engine-off); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "hlms.1.bin: named by the manifest, not there" && ok "...and one missing a file its manifest names: cold, exit 3" ||
    no "missing file: '$out' rc $rc"
toy_cache "$T/.shadercache-engine" 1 2
sed -i 's/ k2$/ k3/' "$T/.shadercache/cache-manifest.txt"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'replaced at another key' && ok "the shared cache replaced at another layer key: cold, exit 3" || no "replaced cache: '$out' rc $rc"
rm -rf "$T/.shadercache"
out=$("$W" "$BUILD" --check --tests-dir "$T" "${X[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'the shared cache is gone' && ok "no shared cache: cold, exit 3" || no "gone cache: '$out' rc $rc"
"$W" "$BUILD" --check --tests-dir "$T" --only app-of > /dev/null 2>&1; rc=$?
[ $rc = 64 ] && ok "a bad --only value: exit 64" || no "a bad --only: rc $rc"
# THE STAMP ONLY ON A PROMOTION (the lead's gap, #52): a FAKE app (--app; it compiles nothing, touches no card - the
# lock is declared held - and needs no display) grows the home's cache; fresh_home's own rules promote it or not.
# A scratch build-shaped dir: fresh_home shares a cache only under a `tests` dir beside a CMakeCache.txt.
B="$T/b"; mkdir -p "$B/tests/.shadercache" "$B/tests/.shadercache-engine"; touch "$B/CMakeCache.txt"
toy_cache "$B/tests/.shadercache" 1 2
toy_cache "$B/tests/.shadercache-engine" 1 2
cat > "$T/fake-app" <<'APP'
#!/bin/sh
# the fake boot: the home's cache grows (a NEW manifest - its seeded one is a hard link of the shared one); with
# FAKE_RACE set, another home's promotion lands first (the shared manifest rewritten at the same keys)
# (the file lines kept as seeded: the linked files are the shared cache's, one generation)
c="$HOME/.local/share/Jahshaka/shadercache"
mkdir -p "$c"; files=$(grep '^file ' "$c/cache-manifest.txt"); rm -f "$c/cache-manifest.txt"
printf 'jahshaka-shader-cache 2\nsaved 2\nshaders 3\n%s\n' "$files" > "$c/cache-manifest.txt"
if [ -n "${FAKE_RACE:-}" ]; then
    rm -f "$FAKE_RACE/cache-manifest.txt"
    printf 'jahshaka-shader-cache 2\nsaved 9\nshaders 4\n%s\n' "$files" > "$FAKE_RACE/cache-manifest.txt"
fi
exit 0
APP
chmod +x "$T/fake-app"
F=(--app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/cold-none")
out=$(FAKE_RACE="$B/tests/.shadercache" DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q 'NOT promoted' && [ ! -e "$B/tests/.shadercache.warm-key" ] &&
    ok "a promotion fresh_home SKIPS (its rule 3, NOT replaced): the boot passed, NO stamp" || no "skipped promotion: '$out' rc $rc"
[ ! -e "$B/tests/.shadercache.driver-key" ] && echo "$out" | grep -q 'app-on waits for the app rows. cache' &&
    ok "...and the app's -on boot does not run on the unpromoted cache: not stamped" || no "app-on after a skip: '$out'"
[ -e "$B/tests/.shadercache-engine.warm-key" ] && [ -e "$B/tests/.shadercache-engine.driver-key" ] &&
    grep -q 'shaders 3' "$B/tests/.shadercache-engine/cache-manifest.txt" &&
    ok "...while the engine rows' cache, booted by the lab and promoted, is stamped, and its -on boot after it" || no "the engine boots: '$out'"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'no warm-up recorded' && ok "...so the next call reads cold and warms again" || no "after the skip: '$out' rc $rc"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "warm-app-cache: warm at $key" && [ -e "$B/tests/.shadercache.warm-key" ] &&
    [ -e "$B/tests/.shadercache.driver-key" ] && [ "$(sed -n 3p "$B/tests/.shadercache.warm-key")" = "$SD" ] &&
    echo "$out" | grep -q "^warm_app_boot: test tier " && grep -q 'shaders 3' "$B/tests/.shadercache/cache-manifest.txt" &&
    ok "a promotion that lands (the shared cache replaced by the boot's grown one): stamped, then the -on boot" ||
    no "landed promotion: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "...and the next call reads warm" || no "after the landing: '$out' rc $rc"
awk '{ n[$1] = 1 } END { exit (n["app-off"] && n["app-on"] && n["engine-off"] && n["engine-on"]) ? 0 : 1 }' "$B/tests/.warm-walls" &&
    ok "each boot's wall recorded (.warm-walls: the next call's estimate)" || no "the walls: $(cat "$B/tests/.warm-walls" 2>&1)"
# THE BUDGET (#106): a boot that no longer fits the call's budget is not started - exit 75, the boots left named
rm -f "$B/tests/.shadercache.warm-key" "$B/tests/.shadercache.driver-key"
echo "app-on 1000" >> "$B/tests/.warm-walls"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}" --only app --budget 100); rc=$?
[ $rc = 75 ] && echo "$out" | grep -q 'the app-on boot (est. 1000 s) does not fit this call.s budget' &&
    echo "$out" | grep -q 'call again: --only app-on$' && [ -e "$B/tests/.shadercache.warm-key" ] &&
    [ ! -e "$B/tests/.shadercache.driver-key" ] &&
    ok "the -off boot ran, the -on boot past the budget not started: exit 75, 'call again: --only app-on'" ||
    no "the budget: '$out' rc $rc"
# THE CARD's WAIT BOUNDED PER BOOT (#106): the card held past the boot's wait - exit 75, nothing ran
# (this shell holds the lock on its own descriptor: no holder process outlives the row)
exec 8>"$T/gpu.lock"; flock 8
t0=$(date +%s)
# (JAH_GPU_LOCK_HELD unset: a row run under a holder - spec.sh test, gpu-exclusive.sh - inherits it, and the script
# under test would take the held card for its own and boot)
out=$(env -u JAH_GPU_LOCK_HELD DISPLAY=:fake JAH_GPU_LOCK="$T/gpu.lock" "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}" --only app-on --budget 10); rc=$?
s=$(( $(date +%s) - t0 ))
exec 8>&-
[ $rc = 75 ] && echo "$out" | grep -q 'the card stayed held 1 s' && echo "$out" | grep -q 'call again: --only app-on$' &&
    [ ! -e "$B/tests/.shadercache.driver-key" ] && [ $s -lt 10 ] &&
    ok "the card held past the boot's wait (a tenth of a 10-s budget): exit 75 in $s s, nothing booted" ||
    no "the held card: '$out' rc $rc in $s s"
# THE COLD ENGINE ROWS' EXES (cold-on, URGENT #119): a row declared COLD boots on no shared pipeline cache, so its exe's
# own driver cache is all it has; cold-on runs each listed exe once, uncapped, in its row's working directory, the
# shared-cache variable unset; stamped with the key and the list's digest. A FAKE cold row (it records where and how it
# ran, then exits 3: a row's own red still stored its driver cache, printed, not judged)
CR="$T/cold-rows"; mkdir -p "$T/cwd"
printf '#!/bin/sh\necho "$PWD|$1|${JAHSHAKA_SHADER_CACHE_DIR:-unset}|${__GL_SHADER_DISK_CACHE:-on}" > "%s/cold-ran"\nexit 3\n' "$T" > "$T/coldfx"
chmod +x "$T/coldfx"
printf 'x.cold\t%s\t%s\targ1\n' "$T/cwd" "$T/coldfx" > "$CR"
C=(--app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$CR")
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the cold engine rows' exes' driver caches (cold-on): no driver-cache run recorded" &&
    ok "a cold engine row listed, never run at the key: cold-on cold, exit 3" || no "cold-on unstamped: '$out' rc $rc"
out=$(JAHSHAKA_SHADER_CACHE_DIR=/x __GL_SHADER_DISK_CACHE=0 DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q '^warm_cold_rows: x.cold: exit 3' && [ -e "$B/tests/.cold-engine-rows.driver-key" ] &&
    [ "$(cat "$T/cold-ran" 2>/dev/null)" = "$T/cwd|arg1|unset|on" ] &&
    ok "cold-on runs the row's exe once in its working directory with its args, the shared cache unset, the driver's cache on; its red printed, the stamp written" ||
    no "cold-on run: '$out' rc $rc ran: $(cat "$T/cold-ran" 2>&1)"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "...and the next call reads cold-on warm" || no "cold-on after: '$out' rc $rc"
printf 'y.cold\t%s\t%s\n' "$T/cwd" "$T/coldfx" >> "$CR"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'the cold rows changed since' && ok "a row newly registered COLD: cold-on cold again, exit 3" ||
    no "a new cold row: '$out' rc $rc"
printf '#!/bin/sh\nsleep 30\n' > "$T/slowfx"; chmod +x "$T/slowfx"
printf 'z.slow\t%s\t%s\n' "$T/cwd" "$T/slowfx" > "$T/slow-rows"
t0=$(date +%s)
out=$(bash "$SRC/tests/support/warm_cold_rows.sh" "$T/slow-rows" 1 "$T/slow-logs"); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q 'z.slow: exit 124, .* KILLED at the bound' && [ $(( $(date +%s) - t0 )) -lt 15 ] &&
    ok "an exe past the call's bound is killed and named - exit 1, no stamp from such a boot" || no "the bound: '$out' rc $rc"
echo "tooling.warm_app_cache: $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
