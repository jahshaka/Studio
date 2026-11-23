#!/bin/bash
# tooling.warm_app_cache (URGENT #52) - scripts/warm-app-cache.sh's NO-OP PATH: warm at its key it answers at once and
# boots nothing; cold (no stamp, a moved key, a replaced cache) it says so - for each of its FOUR boots, the app rows' and
# the engine rows' caches with the driver's cache off and each exe's driver cache after it (PHOTON-7 11-2; the two-boot
# law #100/#102b; one boot a call, the card's wait bounded per boot, #106). On a scratch tests dir (--tests-dir), never
# the tree's shared cache; the boot itself is the script's own (not a test: once, uncapped, when the key moved).
# SEVEN ROWS, ONE SCRIPT (TEST-STREAMLINE-1: the whole ran 35.8 s quiet at 8a8503057 against a CPU row's 10-s budget and a
# 6.48-s price line - split by proof): <part> = boots (tooling.warm_app_cache: the four boots' answers, the stamps, the
# fake boots, the budget and the bounded wait), short (tooling.warm_short: a predicted nothing-built boot's short hold and its
# proof by the engine's count, the boot's own printed wall, the hold's label - split from boots by proof, TE-WARM-C), cold (tooling.warm_cold_rows: the cold-on runner's rows - one at a time, the
# crashed prime, the seeds), coldcall (tooling.warm_cold_calls: a call's own bounds - the held card, the deadline, the logs)
# keys (tooling.warm_key: the key's terms, no top-up, the path-free stamps) or inherit (tooling.warm_inherit: a fresh tree
# takes its parent's caches at an equal key) or store (tooling.warm_store: the box's warm store); none = all seven, a hand run.
# usage: warm_app_cache.sh <source-dir> <build-dir> [boots|short|cold|coldcall|keys|inherit|store]
set -u
# (the boots' DISPLAY is :99 - a RIG number, URGENT #195: warm-app-cache.sh refuses any other; the fakes connect to nothing)
SRC=${1:?source}; BUILD=${2:?build}; PART=${3:-all}
case "$PART" in all|boots|short|cold|coldcall|keys|inherit|store) ;; *) echo "warm_app_cache.sh: <part> is boots, short, cold, coldcall, keys, inherit or store, not $PART" >&2; exit 64 ;; esac
want() { [ "$PART" = all ] || [ "$PART" = "$1" ]; }
W="$SRC/scripts/warm-app-cache.sh"
T=${KEEP_T:-$(mktemp -d)}; [ -n "${KEEP_T:-}" ] || trap 'rm -rf "$T"' EXIT   # (KEEP_T=<dir>: a hand debug run keeps its toys)
CK="$SRC/tests/support/shadercache_check.py"
# A TOY CACHE THAT IS ONE GENERATION (URGENT #117): its two files and a manifest naming them at their length and the
# engine's hash - the warm reads a cache that fails its own manifest as cold
# toy_cache <dir> <saved> <shaders>
toy_cache() {
    mkdir -p "$1"; printf '0123456789' > "$1/hlms.1.bin"; printf 'abcdefghijklmnopqrst' > "$1/pipeline.cache"
    printf 'JAHCOMP1' > "$1/compute.bin"
    printf 'jahshaka-shader-cache 2\nsaved %s\nshaders %s\nfile hlms.1.bin 10 %s k1\nfile pipeline.cache 20 %s k2\nfile compute.bin 8 %s k1\n' "$2" "$3" \
        "$(python3 -I "$CK" --hash "$1/hlms.1.bin")" "$(python3 -I "$CK" --hash "$1/pipeline.cache")" \
        "$(python3 -I "$CK" --hash "$1/compute.bin")" > "$1/cache-manifest.txt"
    # (warm in EVERY term: the startup pass's record at the real build's pass id - the cases that drive the script on the real
    # build judge it; a toy build with no generated id judges none; P1-P3 and S9 write their own)
    [ -z "$RPASS" ] || printf 'pass=%s|media=m|cache=c\n' "$RPASS" > "$1/warm-pass.key"
}
# THE FORK's SOURCE, as the real build names it (CMakeCache OGRE_NEXT_SOURCE): the cold key hashes its Hlms implementation
# (TE-WARM-C), so every toy build the key is computed on carries the line
OGRE_LINE=$(grep -m1 '^OGRE_NEXT_SOURCE:PATH=' "$BUILD/CMakeCache.txt")
RPASS=$(sed -n 's/^#define JAH_STARTUP_PASS_CODE_ID "\([0-9a-f]*\)"$/\1/p' "$BUILD/generated/jah_startup_pass_id.h" 2>/dev/null)
toy_cache "$T/.shadercache" 1 2
toy_cache "$T/.shadercache-engine" 1 2
fail=0
ok() { echo "  ok: $1"; }
no() { echo "  FAIL: $1"; fail=$((fail + 1)); }
# the exes the -on stamps name (the driver's per-exe key is the exe's name): two fakes, the same in every call below
printf '#!/bin/sh\nexit 0\n' > "$T/app"; printf '#!/bin/sh\nexit 0\n#lab\n' > "$T/lab"; chmod +x "$T/app" "$T/lab"
cp "$T/app" "$T/app2"
: > "$T/cold-none"   # no cold row: cold-on has nothing to warm (its own cases at the end)
X=(--app "$T/app" --lab "$T/lab" --cold-rows "$T/cold-none")
eid() { basename "$1"; }
key=$("$W" "$BUILD" --key)   # the build's key (each part reads it; the boots part proves --check names it)
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
printf 'jahshaka-shader-cache 2\nsaved 2\nshaders %s\n%s\n' "${FAKE_SHADERS:-3}" "$files" > "$c/cache-manifest.txt"   # (FAKE_SHADERS: (B)'s count)
if [ -n "${FAKE_RACE:-}" ]; then
    rm -f "$FAKE_RACE/cache-manifest.txt"
    printf 'jahshaka-shader-cache 2\nsaved 9\nshaders 4\n%s\n' "$files" > "$FAKE_RACE/cache-manifest.txt"
fi
# with FAKE_PASS_LOG set, the startup gate's global pass as the app decides it: a record in the cache = skipped; none = the
# pass runs and writes its record (a new file - the seeded one is a hard link of the shared one). With FAKE_PASS_ID set too,
# the app's own rule (src/app/shaderbuildgate.cpp keyMoved): skipped only when the record names THIS build's pass id
if [ -n "${FAKE_PASS_LOG:-}" ]; then
    if [ -n "${FAKE_PASS_ID:-}" ] && grep -q "^pass=$FAKE_PASS_ID|" "$c/warm-pass.key" 2>/dev/null; then echo skipped >> "$FAKE_PASS_LOG"
    elif [ -z "${FAKE_PASS_ID:-}" ] && [ -f "$c/warm-pass.key" ]; then echo skipped >> "$FAKE_PASS_LOG"
    else echo ran >> "$FAKE_PASS_LOG"; rm -f "$c/warm-pass.key"; printf 'pass=%s|media=m|cache=c\n' "${FAKE_PASS_ID:-this-boot}" > "$c/warm-pass.key"; fi
fi
exit 0
APP
chmod +x "$T/fake-app"
F=(--app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/cold-none")
# THE COLD ROWS' FAKES (the cold and coldcall parts): a fake that records how it ran and exits with its own red, and a
# fake that records its start and end (0.3 s apart: the one-at-a-time law reads their order, not their length)
CR="$T/cold-rows"; mkdir -p "$T/cwd"; CS="$B/tests/.cold-rows"
printf '#!/bin/sh\necho "$PWD|$1|${JAHSHAKA_SHADER_CACHE_DIR:-unset}|${__GL_SHADER_DISK_CACHE:-on}|${JAH_TIER_X:-none}|${JAH_GONE_X:-unset}" > "%s/cold-ran"\necho "FAIL: its own law"\nexit 3\n' "$T" > "$T/coldfx"
chmod +x "$T/coldfx"
printf 'x.cold\t%s\tJAH_TIER_X=epic photon\x1f-JAH_GONE_X\t%s\targ1\n' "$T/cwd" "$T/coldfx" > "$CR"
C=(--app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$CR")
printf '#!/bin/sh\necho "start $(basename "$PWD") $(date +%%s%%N)" >> "%s/order"; sleep 0.3; echo "end $(basename "$PWD") $(date +%%s%%N)" >> "%s/order"\nexit 0\n' "$T" "$T" > "$T/seqfx"
chmod +x "$T/seqfx"; mkdir -p "$T/y" "$T/z"
# THE STALE-RECORD TOY (the boots part's P1-P3 and the short part's B1-B4): a toy build warm at its key whose app cache's
# startup-pass record names pass aaaa.. while the build's pass id is bbbb.. (sets B4, k4, i4)
b4_toy() {
    B4="$T/b4"; rm -rf "$B4"; mkdir -p "$B4/tests" "$B4/engine/generated" "$B4/generated" "$B4/bin/media"; echo "$OGRE_LINE" > "$B4/CMakeCache.txt"; toy_cache "$B4/tests/.shadercache" 1 2
    cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$B4/engine/generated/"; cp -r "$BUILD/bin/media/Hlms" "$BUILD/bin/media/Photon" "$B4/bin/media/" 2>/dev/null
    printf '#define JAH_STARTUP_PASS_CODE_ID "%s"\n' bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb > "$B4/generated/jah_startup_pass_id.h"
    printf 'pass=%s|media=m|cache=c\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa > "$B4/tests/.shadercache/warm-pass.key"
    k4=$("$W" "$B4" --key); printf '%s\n%s\n%s\n' "$k4" "k1 k2 " "sets none" > "$B4/tests/.shadercache.warm-key"
    i4=$(stat -c %i "$B4/tests/.shadercache/hlms.1.bin"); : > "$T/b4.pass"
}
if want boots; then
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
out=$(FAKE_RACE="$B/tests/.shadercache" DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q 'NOT promoted' && [ ! -e "$B/tests/.shadercache.warm-key" ] &&
    ok "a promotion fresh_home SKIPS (its rule 3, NOT replaced): the boot passed, NO stamp" || no "skipped promotion: '$out' rc $rc"
[ ! -e "$B/tests/.shadercache.driver-key" ] && echo "$out" | grep -q 'app-on waits for the app rows. cache' &&
    ok "...and the app's -on boot does not run on the unpromoted cache: not stamped" || no "app-on after a skip: '$out'"
[ -e "$B/tests/.shadercache-engine.warm-key" ] && [ -e "$B/tests/.shadercache-engine.driver-key" ] &&
    grep -q 'shaders 3' "$B/tests/.shadercache-engine/cache-manifest.txt" &&
    ok "...while the engine rows' cache, booted by the lab and promoted, is stamped, and its -on boot after it" || no "the engine boots: '$out'"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'no warm-up recorded' && ok "...so the next call reads cold and warms again" || no "after the skip: '$out' rc $rc"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "warm-app-cache: warm at $key" && [ -e "$B/tests/.shadercache.warm-key" ] &&
    [ -e "$B/tests/.shadercache.driver-key" ] && [ "$(sed -n 3p "$B/tests/.shadercache.warm-key")" = "$SD" ] &&
    echo "$out" | grep -q "^warm_app_boot: test tier " && grep -q 'shaders 3' "$B/tests/.shadercache/cache-manifest.txt" &&
    ok "a promotion that lands (the shared cache replaced by the boot's grown one): stamped, then the -on boot" ||
    no "landed promotion: '$out' rc $rc"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${F[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "...and the next call reads warm" || no "after the landing: '$out' rc $rc"
# THE DISPLAY IS THE TREE's RIG (WORKFLOW-STREAMLINE-1 5.8: one way to get a display): with no DISPLAY a boot runs on
# `rig.sh up --env` and the rig it started comes down at the end. Hermetic: a tree of links to this one's scripts and tests (its
# rig-xvfb.pid there, never in the real tree; no lane's live rig reused) and a FAKE X server (a script named Xvfb on PATH, its
# files in a private X dir). (red on the old script: it started its own Xvfb, a real one, recorded by `$!` of its wrapper)
mkdir -p "$T/rt/scripts" "$T/xr" "$T/xbin"; ln -s "$SRC"/scripts/* "$T/rt/scripts/"; ln -s "$SRC/tests" "$T/rt/tests"
printf '%s\n' '#!/bin/bash' 'n=${1#:}; d=${JAH_RIG_XDIR:?}' 'printf "%10d\n" $$ > "$d/.X$n-lock"; mkdir -p "$d/.X11-unix"; : > "$d/.X11-unix/X$n"' \
    'trap '"'"'rm -f "$d/.X$n-lock" "$d/.X11-unix/X$n"; exit 0'"'"' TERM' 'for i in $(seq 300); do sleep 0.2; done' > "$T/xbin/Xvfb"; chmod +x "$T/xbin/Xvfb"
rm -f "$B/tests/.shadercache.driver-key"
out=$(env -u DISPLAY PATH="$T/xbin:$PATH" JAH_RIG_XDIR="$T/xr" JAH_GPU_LOCK_HELD=1 "$T/rt/scripts/warm-app-cache.sh" "$BUILD" --tests-dir "$B/tests" "${F[@]}" --only app-on); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q 'booting the app exe.s driver cache (app-on): .* on :60 ' && [ -e "$B/tests/.shadercache.driver-key" ] \
    && [ ! -e "$T/xr/.X60-lock" ] && [ ! -e "$T/rt/rig-xvfb.pid" ] \
    && ok "no DISPLAY: the boot ran on the tree's rig (rig.sh up --env, :60), stamped, and the rig it started is down (no pid file, no server)" \
    || no "the boot on the rig: rc $rc '$(echo "$out" | head -4)' pid file $(cat "$T/rt/rig-xvfb.pid" 2>&1)"
awk '{ n[$1] = 1 } END { exit (n["app-off"] && n["app-on"] && n["engine-off"] && n["engine-on"]) ? 0 : 1 }' "$B/tests/.warm-walls" &&
    ok "each boot's wall recorded (.warm-walls: the next call's estimate)" || no "the walls: $(cat "$B/tests/.warm-walls" 2>&1)"
# THE BUDGET (#106): a boot that no longer fits the call's budget is not started - exit 75, the boots left named
rm -f "$B/tests/.shadercache.warm-key" "$B/tests/.shadercache.driver-key"
echo "app-on 1000" >> "$B/tests/.warm-walls"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}" --only app --budget 100); rc=$?
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
# (A BOOT THAT FITS DECLARES A SHORT HOLD, CARD-QUEUE-1: app-on's measured own wall 20 s -> --short 50, its ticket - kept as the
# exit 75's reservation - named <n>.s50)
cp "$B/tests/.warm-walls" "$T/walls.keep" 2> /dev/null; echo "app-on 20" > "$B/tests/.warm-walls"
out=$(env -u JAH_GPU_LOCK_HELD DISPLAY=:99 JAH_GPU_LOCK="$T/gpu.lock" "$W" "$BUILD" --tests-dir "$B/tests" "${F[@]}" --only app-on --budget 10); rc=$?
s=$(( $(date +%s) - t0 ))
exec 8>&-
if [ -e "$T/walls.keep" ]; then mv "$T/walls.keep" "$B/tests/.warm-walls"; else rm -f "$B/tests/.warm-walls"; fi
ls "$T/gpu.lock.q" 2> /dev/null | grep -qE '^[0-9]{12}\.s50$' && ok "a boot that fits a short hold declares one: app-on (own wall 20 s) took ticket <n>.s50" \
    || no "app-on with an own wall of 20 s did not declare --short 50: $(ls "$T/gpu.lock.q" 2> /dev/null | tr '\n' ' ')"
[ $rc = 75 ] && echo "$out" | grep -q 'the card stayed held 1 s' && echo "$out" | grep -q 'call again: --only app-on$' &&
    [ ! -e "$B/tests/.shadercache.driver-key" ] && [ $s -lt 10 ] &&
    ok "the card held past the boot's wait (a tenth of a 10-s budget): exit 75 in $s s, nothing booted" ||
    no "the held card: '$out' rc $rc in $s s"
# NO TOP-UP (WARM-SHARE-1 ruling 5; spikes/warm-share/drop-hlms.md: the top-up's --drop-hlms left the PBS recipes 41 -> 0 and
# every row compiled again): an -off cache warm at the key whose ENGINE BUILD ID moved since its stamp (the engine's code alone
# changed) is WARM - nothing boots, the shared union keeps every recipe a green row promoted (its hlms and compute layers and
# the startup pass's record untouched, the same inodes). (red on the old script: "a TOP-UP", a boot whose promotion dropped
# the hlms and compute layers) This toy build has NO generated pass id: its record is not judged - the real engine id is a
# term of the pass id, and a moved pass id is the case below (P1-P3), one boot that keeps the union
B2="$T/b2"; mkdir -p "$B2/tests" "$B2/engine/generated" "$B2/bin/media"; echo "$OGRE_LINE" > "$B2/CMakeCache.txt"; toy_cache "$B2/tests/.shadercache" 1 2
cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$B2/engine/generated/"; cp -r "$BUILD/bin/media/Hlms" "$BUILD/bin/media/Photon" "$B2/bin/media/" 2>/dev/null
printf 'pass=an-earlier-boot\n' > "$B2/tests/.shadercache/warm-pass.key"
k2=$("$W" "$B2" --key); printf '%s\n%s\n%s\n' "$k2" "k1 k2 " "sets none" > "$B2/tests/.shadercache.warm-key"
echo 0000000000000000 > "$B2/tests/.shadercache.engine-id"   # (an old tree's leftover stamp: read by nothing now)
sed -i 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\)/#define JAHSHAKA_ENGINE_BUILD_ID "f\1/' "$B2/engine/generated/jahshaka_engine_build_id.h"
i2=$(stat -c %i "$B2/tests/.shadercache/hlms.1.bin"); : > "$T/b2.pass"
out=$(FAKE_PASS_LOG="$T/b2.pass" DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$B2" --tests-dir "$B2/tests" "${F[@]}" --only app-off); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $k2" ] && [ ! -s "$T/b2.pass" ] && [ "$("$W" "$B2" --key)" = "$k2" ] &&
    grep -q '^file hlms\.1\.bin' "$B2/tests/.shadercache/cache-manifest.txt" && grep -q '^file compute\.bin' "$B2/tests/.shadercache/cache-manifest.txt" &&
    [ "$(stat -c %i "$B2/tests/.shadercache/hlms.1.bin")" = "$i2" ] && [ "$(cat "$B2/tests/.shadercache/warm-pass.key")" = pass=an-earlier-boot ] &&
    ok "NO TOP-UP: the engine's code moved alone (its build id) - warm at the key, nothing booted, the union's hlms and compute layers and the pass record kept (the same inode)" ||
    no "no top-up: rc $rc '$out' pass [$(tr '\n' ' ' < "$T/b2.pass")] $(tr '\n' ';' < "$B2/tests/.shadercache/cache-manifest.txt")"
# THE STARTUP PASS's RECORD (TE-WARM-WHY (D); spikes/validation-slow/): a cache warm at the key whose record names ANOTHER
# build's pass id (an engine or a Studio edit kept the key; a copy taken from the store) is COLD for app-off - ONE boot over the
# warm cache (no --drop-hlms: the hlms and compute layers kept, the same inodes) runs the pass, its promotion carries the record;
# the next call is warm and boots nothing; an equal record boots nothing (the control). The fake app runs the pass unless the
# record names FAKE_PASS_ID. (red on the script before (D): "warm at", nothing booted - the record stays stale, every row pays)
b4_toy
P4() { FAKE_PASS_ID=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb FAKE_PASS_LOG="$T/b4.pass" DISPLAY=:99 JAH_GPU_LOCK_HELD=1 \
       "$W" "$B4" --tests-dir "$B4/tests" "${F[@]}" --only app-off "$@"; }
out=$(P4 --check); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the startup pass's record is another build's (pass aaaaaaaa, this build's bbbbbbbb)" &&
    ok "P1: a record of another build's pass id reads COLD under --check, naming both ids" || no "P1 --check: rc $rc '$out'"
out=$(P4); rc=$?
[ $rc = 0 ] && [ "$(cat "$T/b4.pass")" = ran ] && grep -q '^pass=bbbbbbbb' "$B4/tests/.shadercache/warm-pass.key" &&
    grep -q '^file hlms\.1\.bin' "$B4/tests/.shadercache/cache-manifest.txt" && grep -q '^file compute\.bin' "$B4/tests/.shadercache/cache-manifest.txt" &&
    [ "$(stat -c %i "$B4/tests/.shadercache/hlms.1.bin")" = "$i4" ] &&
    ok "P2: ONE app-off boot ran the pass and its promotion carried the record (pass bbbbbbbb), the hlms and compute layers kept (the same inode)" ||
    no "P2 the refresh: rc $rc pass [$(tr '\n' ' ' < "$T/b4.pass")] record [$(cut -c1-13 "$B4/tests/.shadercache/warm-pass.key")] '$(echo "$out" | tail -3)'"
out=$(P4); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $k4" ] && [ "$(cat "$T/b4.pass")" = ran ] &&
    ok "P3: the next call is warm at the key and boots nothing (the control: an equal record)" ||
    no "P3 after the refresh: rc $rc '$out' pass [$(tr '\n' ' ' < "$T/b4.pass")]"
# A COVERAGE BUILD's BOOT WRITES ITS PROFILE UNDER coverage/boots/<boot> (the 2a audit's finding 1; red before: nothing set it)
B3="$T/b3"; mkdir -p "$B3/tests"; printf 'JAH_COVERAGE:BOOL=ON\n%s\n' "$OGRE_LINE" > "$B3/CMakeCache.txt"; toy_cache "$B3/tests/.shadercache" 1 2
printf '#!/bin/sh\necho "$GCOV_PREFIX" > %s/b3.prefix\nexit 0\n' "$T" > "$T/covapp"; chmod +x "$T/covapp"
mkdir -p "$B3/engine/generated" "$B3/bin/media"; cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$B3/engine/generated/"
cp -r "$BUILD/bin/media/Hlms" "$BUILD/bin/media/Photon" "$B3/bin/media/" 2>/dev/null
DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$B3" --tests-dir "$B3/tests" --app "$T/covapp" --lab "$T/covapp" --cold-rows "$T/cold-none" --only app-on > /dev/null 2>&1
printf '%s\n%s\n%s\n' "$("$W" "$B3" --key)" "k1 k2 " "sets none" > "$B3/tests/.shadercache.warm-key"
DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$B3" --tests-dir "$B3/tests" --app "$T/covapp" --lab "$T/covapp" --cold-rows "$T/cold-none" --only app-on > /dev/null 2>&1
[ "$(cat "$T/b3.prefix" 2>/dev/null)" = "$B3/coverage/boots/app-on" ] && [ -d "$B3/coverage/boots/app-on" ] &&
    ok "a JAH_COVERAGE build's boot writes its profile under coverage/boots/<boot>" || no "the boot's GCOV_PREFIX: '$(cat "$T/b3.prefix" 2>/dev/null)'"
fi
if want short; then
# THE SHORT HOLDS AND THE BOOT's OWN WALL (split from boots by proof, TE-WARM-C: the boots part ran 13.5 s against a CPU row's
# 10-s budget, its 8.38-s price older than TE-WARM-WHY's B1-B4): what a warm-up's hold DECLARES - a predicted nothing-built boot's
# short ticket (B1), the prediction proved by the engine's own count (B2-B4) - and what its line PRINTS (the boot's own wall, the
# card's wait apart; the hold's label carries the key)
b4_toy
# A PREDICTED NOTHING-BUILT BOOT IS A SHORT HOLD (TE-WARM-WHY (B)): the app-off boot whose cache is warm at the key and whose
# only why is the pass record declares its OWN nothing-built wall + 50 % - the tree's measured "app-off-nothing 40" -> --short
# 60, whatever the cold boot's "app-off 400" says (2 x 400 + 10 would be long); with none measured, the box's price as the CALLER
# gives it (JAH_WARM_NOTHING_S=50 -> .s75) - and with none given, LONG (the script names no box price: CODING_RULES § 7). The
# card held: exit 75, the ticket kept as the reservation. (red before (B): long tickets; before the final read's fix: .s75 with
# nothing given)
PB4() {   # <walls line>...: the stale-record toy B4 again, the card held by this shell, a 5-s budget; prints the ticket's suffix
    printf 'pass=%s|media=m|cache=c\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa > "$B4/tests/.shadercache/warm-pass.key"
    printf '%s\n' "$@" > "$B4/tests/.warm-walls"; rm -rf "$T/gpuP.lock.q"
    exec 8>"$T/gpuP.lock"; flock 8
    # (--budget 5: the first boot's least wait is a tenth of the budget in whole seconds - 0 here, so the held card answers at
    # once instead of 1 s a call; the law read is the TICKET's suffix, which no budget enters)
    env -u JAH_GPU_LOCK_HELD DISPLAY=:99 JAH_GPU_LOCK="$T/gpuP.lock" "$W" "$B4" --tests-dir "$B4/tests" "${F[@]}" --only app-off --budget 5 > /dev/null 2>&1
    echo "rc $? $(ls "$T/gpuP.lock.q" 2> /dev/null | grep -E '^[0-9]{12}' | tr '\n' ' ')"
    exec 8>&-
}
unset JAH_WARM_NOTHING_S   # (the box's price arrives only as the caller's variable: none here unless a case sets it)
r1=$(PB4 "app-off 400" "app-off-nothing 40"); r2=$(JAH_WARM_NOTHING_S=50 PB4 "app-off 400"); r3=$(PB4 "app-off 400")
case "$r1" in "rc 75 "[0-9]*.s60" ") true ;; *) false ;; esac && case "$r2" in "rc 75 "[0-9]*.s75" ") true ;; *) false ;; esac &&
    case "$r3" in "rc 75 "[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]" ") true ;; *) false ;; esac &&
    ok "B1: a predicted nothing-built app-off declares its own wall + 50 %: 40 s -> .s60 beside a cold 400 s; none measured: the caller's JAH_WARM_NOTHING_S=50 -> .s75; none given -> LONG" ||
    no "B1 the nothing-built short hold: [$r1] [$r2] [$r3]"
# ...AND THE PREDICTION IS PROVED BY THE ENGINE's OWN COUNT (B2-B4): the shared manifest's `shaders` before and after. B2: the boot
# grew it (2 -> 3: an engine edit's new permutations at the same key) - said, "the price not kept", the nothing-built wall stays
# 40, NO FAULT; B3 (the control): it grew nothing - the wall IS kept as the nothing-built price; B4: the record's media= term moved
# (a term no key holds whole) - a FAULT naming it. (red before the final read's fix: B2 printed FAULT from one log line, B3/B4 absent)
B4P() { printf 'pass=%s|media=%s|cache=c\n' aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "$1" > "$B4/tests/.shadercache/warm-pass.key"
        echo "app-off-nothing 40" > "$B4/tests/.warm-walls"; shift
        env "$@" FAKE_PASS_ID=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb FAKE_PASS_LOG="$T/b4.pass" DISPLAY=:99 JAH_GPU_LOCK_HELD=1 \
            "$W" "$B4" --tests-dir "$B4/tests" "${F[@]}" --only app-off; }
wall_of() { awk '$1 == "app-off-nothing" { print $2 }' "$B4/tests/.warm-walls"; }
sed -i 's/^shaders .*/shaders 2/' "$B4/tests/.shadercache/cache-manifest.txt"
out=$(B4P m FAKE_SHADERS=3); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "predicted to build nothing; its cache holds 2 -> 3 shaders: the price not kept" &&
    ! echo "$out" | grep -q FAULT && [ "$(wall_of)" = 40 ] &&
    ok "B2: a predicted-nothing boot that grew the cache (2 -> 3 shaders) is said, the price not kept, no FAULT" ||
    no "B2 the prediction missed: rc $rc '$(echo "$out" | grep -E "predicted|FAULT|warm at" | head -2)' walls [$(tr '\n' ' ' < "$B4/tests/.warm-walls")]"
out=$(B4P m FAKE_SHADERS=3); rc=$?   # (the cache holds 3 now: the same count after)
[ $rc = 0 ] && ! echo "$out" | grep -qE "predicted|FAULT" && [ "$(wall_of)" != 40 ] &&
    ok "B3: a predicted-nothing boot that grew nothing - its wall kept as the nothing-built price ($(wall_of) s)" ||
    no "B3 the control: rc $rc '$(echo "$out" | grep -E "predicted|FAULT|warm at" | head -2)' walls [$(tr '\n' ' ' < "$B4/tests/.warm-walls")]"
out=$(B4P old-media FAKE_SHADERS=3); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "FAULT: the startup pass's record moved in a term no key saw (pass, media)" &&
    ok "B4: the record's media= term moved unseen by any key - a FAULT naming it" || no "B4 the unseen term: rc $rc '$(echo "$out" | grep -E "FAULT|warm at" | head -2)'"
# THE BOOT's OWN WALL (block A, 2026-11-03: "the boot 75 s" was 27.5 s of the card's wait + 47.5 s of boot): another holder keeps
# the card 2 s past the call's ticket, the toy boot takes ~0 s - the line prints the boot's own wall and the wait beside it (red
# before: "the boot 3 s")
B4="$T/b4"; mkdir -p "$B4/tests"; echo "$OGRE_LINE" > "$B4/CMakeCache.txt"; toy_cache "$B4/tests/.shadercache" 1 2   # (cold: no warm-up recorded)
# (RED ONCE UNDER -j4, 2026-11-03, two races: (1) the walls were whole seconds and the wait floored - "own = wall - wait"
# read 2 s for a 0.4-s boot when the 2.1 s spanned three second boundaries; both are tenths now; (2) "sleep 0.3" assumed the
# holder had the lock by then - a loaded box can start the call first (wait 0): the call now starts only once the holder has
# written its mark under the lock, at most 5 s)
# THE LAW, RE-DERIVED (WS1-FIX2 item 9, 2026-11-20: red in PHOTON-7's first --code call at load ~40, "the boot 2.1 s (the card's
# wait 1.2 s)"). The old bar "the printed boot is UNDER the printed wait" compared two FORK-BOUND quantities: the toy boot
# (fresh_home + five fake processes: 0.3-0.6 s at load ~20, 2.1 s at ~40 under ctest -j12) against the holder's 2-s sleep MINUS
# the script's own start-up to its lock request (0.3-0.6 s at ~20, ~0.8 s at ~40) - its margin was forks, which CODING_RULES §6
# forbids (a toy holds its margin to a bar in SLEEPS). The rig is not in it: DISPLAY is given, so rig.sh is never called.
# What the line must prove is that the printed boot is the boot's OWN wall, the wait apart: the two printed figures SPLIT the
# call - boot + wait <= the call's own wall measured here (each printed figure is floored to tenths, so their sum never exceeds
# the true wall). The old defect printed the boot WITH the wait in it: boot + wait = wall + wait, over the call's wall by the
# whole wait. THE WAIT IS A SLEEP, never sleep-minus-start-up: the holder keeps the card until the call's TICKET is in the
# queue (<lock>.q/<n>, written by gpu-exclusive.sh just before its wait clock starts; a glob poll, no fork), THEN sleeps 2 s - so
# the printed wait is ~2 s however long the script takes to reach its lock request (a fixed 2-s or 3-s hold left 0 s of wait on
# one CPU shared with three busy loops: the start-up outlasted it). So the bar's margin is the holder's sleep alone.
# ITS DIRECTION: load only ADDS to the call's wall, never to the printed sum of a correct line, so load cannot make this bar a
# false red (measured on one CPU shared with three busy loops: boot 8.4 + wait 1.5 <= wall 11.8). What load can do is hide the
# defect: the defect overshoots by the wait (~2 s) and the call's own start-up and tail sit in its wall as slack - at a quiet
# box the defect reads red (boot 2.4 + wait 2.2 > wall 2.8), at that 1/4-CPU squeeze its slack (1.8 s) passed it. A regression
# is caught on any normal run; no load is a false red.
rm -f "$T/held4"
( flock 7; : > "$T/held4"
  for _ in $(seq 300); do set -- "$T/gpu4.lock.q"/[0-9]*; [ -e "$1" ] && break; sleep 0.05; done; sleep 2 ) 7>"$T/gpu4.lock" & hp=$!
for _ in $(seq 50); do [ -e "$T/held4" ] && break; sleep 0.1; done
tc0=$(date +%s%N)
out=$(env -u JAH_GPU_LOCK_HELD DISPLAY=:99 JAH_GPU_LOCK="$T/gpu4.lock" "$W" "$BUILD" --tests-dir "$B4/tests" "${F[@]}" --only app-off --budget 300); rc=$?
cw=$(( ($(date +%s%N) - tc0) / 100000000 ))   # the call's own wall, tenths (floored: the bar below is the stricter for it, +1)
wait $hp
ln=$(echo "$out" | grep "warm at $key - the boot")
bw=$(echo "$ln" | sed -n "s/.*the boot \([0-9]*\)\.\([0-9]\) s (the card's wait \([0-9]*\)\.\([0-9]\) s).*/\1\2 \3\4/p")
[ $rc = 0 ] && [ -e "$T/held4" ] && [ -n "$bw" ] && [ "${bw#* }" -ge 10 ] && [ "$(( 10#${bw% *} + 10#${bw#* } ))" -le "$(( cw + 1 ))" ] &&
    ok "the boot's printed wall is its own, the card's wait beside it, in tenths: boot ${bw% *} + wait ${bw#* } <= the call's wall $cw (+1)" ||
    no "the boot's wall: rc $rc '$ln', the call's wall $cw tenths (the holder's mark: $([ -e "$T/held4" ] && echo set || echo NOT set))"
# THE HOLD's LABEL CARRIES THE KEY IT WARMS (WARM-SHARE-1 S2: a hit - no hold - and a miss - a hold at that key - are read from
# the hold log; red before: "warm-app-cache (app-off)", no key)
grep -q "	warm-app-cache (app-off) at $key	" "$T/gpu4.lock.holds" &&
    ok "the warm-up's hold is logged with the key it warms: 'warm-app-cache (app-off) at $key'" ||
    no "the hold's label: $(cut -f6 "$T/gpu4.lock.holds" 2>/dev/null | sort -u | tr '\n' '|')"
fi
if want cold; then
# THE COLD ROWS (cold-on, URGENT #119; SHIP-CUT-2 0b): a row declared COLD makes its own cache, so cold-on runs each listed
# command once, uncapped, in its row's working directory, the shared-cache variable unset, ONE ROW AT A TIME, EACH ITS OWN HOLD
# (the lock declared held here: nested, at once), and stamps EACH ROW it judged PRIMED (.cold-rows/<row>.driver-key: the key, the
# row's line, the built digest). A FAKE cold row records where and how it ran, then exits 3: a row's own red, its run at its end
# - PRIMED, the red printed.
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the cold rows' own caches (cold-on): 1 of 1 to prime: x.cold (no prime recorded)" &&
    ok "a cold row listed, never primed at the key: cold-on cold, the row named, exit 3" || no "cold-on unstamped: '$out' rc $rc"
printf '# toy prices\nx.cold 12.6  a toy line\n' > "$T/prices.txt"
out=$(JAH_GATE_TIMES="$T/prices.txt" JAH_GONE_X=1 JAHSHAKA_SHADER_CACHE_DIR=/x __GL_SHADER_DISK_CACHE=0 DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
# (URGENT #204: what it will prime and for how long, said before its first hold)
echo "$out" | grep -q "^warm-app-cache: booting .* (cold-on): 1 of 1 row(s) on :99, .*, expected ~13 s by the rows' prices (prices.txt): x.cold$" &&
    ok "cold-on says what it will prime before its first hold: the rows, their count, ~13 s by the price file (#204)" ||
    no "cold-on's line (#204): $(echo "$out" | grep 'booting' | head -1)"
[ $rc = 0 ] && echo "$out" | grep -q '^warm_cold_rows: x.cold: PRIMED - its run [0-9]* s (exit 3; .*its own red, printed: FAIL: its own law' &&
    [ "$(sed -n 1p "$CS/x.cold.driver-key" 2>/dev/null)" = "$("$W" "$BUILD" --cold-key)" ] &&   # (the cold rows' own key: TE-WARM-WHY (C))
    [ "$(cat "$T/cold-ran" 2>/dev/null)" = "$T/cwd|arg1|unset|on|epic photon|unset" ] &&
    ok "cold-on runs the row's command once in its working directory with its args and its test tier (set and unset), the shared cache unset, the driver's cache on; its own red printed, the row PRIMED and stamped" ||
    no "cold-on run: '$out' rc $rc ran: $(cat "$T/cold-ran" 2>&1)"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] && ok "...and the next call reads cold-on warm" || no "cold-on after: '$out' rc $rc"
touch -d '2001-01-02' "$T/fake-app" "$T/coldfx"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $key" ] &&
    ok "the app and a cold row's exe relinked (no shader moved): cold-on stays warm (URGENT #125: a relink is not a shader change)" ||
    no "a relinked app: '$out' rc $rc"
k2=$("$W" "$BUILD" --key); rc=$?
[ $rc = 0 ] && [ "$k2" = "$key" ] && ok "--key prints the tree's shader key alone ($k2), exit 0" || no "--key: '$k2' rc $rc"
# ONE AT A TIME, ONLY WHAT IS NOT PRIMED: two more rows (each records its start and end) - the primed x.cold is not run again,
# y and z run one after the other, never together
rm -f "$T/cold-ran"
printf 'y.cold\t%s\t\t%s\nz.cold\t%s\t\t%s\n' "$T/y" "$T/seqfx" "$T/z" "$T/seqfx" >> "$CR"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q '2 of 3 to prime: y.cold (no prime recorded); z.cold (no prime recorded)' &&
    ok "two rows newly registered COLD: cold-on cold for those two alone, exit 3" || no "new cold rows: '$out' rc $rc"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ ! -e "$T/cold-ran" ] && [ -e "$CS/y.cold.driver-key" ] && [ -e "$CS/z.cold.driver-key" ] &&
    [ "$(cut -d' ' -f1,2 "$T/order" | tr '\n' '|')" = "start y|end y|start z|end z|" ] &&
    ok "...run ONE AT A TIME (y ended before z started), the primed x.cold not run again, each stamped" ||
    no "one at a time: '$out' rc $rc order: $(tr '\n' '|' < "$T/order" 2>&1)"
# A PRIME THAT CRASHED IS NEVER STAMPED (URGENT #155: .cold's prime died out of GPU memory and the stamp was written over it)
printf '#!/bin/sh\necho "VkResult = VK_ERROR_OUT_OF_DEVICE_MEMORY"\necho "*** Jahshaka crashed"\nexit 1\n' > "$T/oomfx"; chmod +x "$T/oomfx"
printf 'o.cold\t%s\t\t%s\n' "$T/cwd" "$T/oomfx" >> "$CR"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q '^warm_cold_rows: o.cold: NOT PRIMED - its run CRASHED (exit 1, VK_ERROR_OUT_OF_DEVICE_MEMORY)' &&
    [ "$(echo "$out" | tail -1 | grep -c 'the cold-on boot FAILED after [0-9]* s - A PRIME CRASHED, NOT STAMPED: o.cold (its run CRASHED: exit 1, VK_ERROR_OUT_OF_DEVICE_MEMORY)')" = 1 ] && [ ! -e "$CS/o.cold.driver-key" ] &&
    "$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on | grep -q '1 of 4 to prime: o.cold (no prime recorded)' &&
    ok "a prime whose run CRASHED (the engine's OOM line): NOT PRIMED, exit 1, the call's LAST line names the row and its exit, no stamp - the next call primes it again" ||
    no "a crashed prime: '$out' rc $rc"
sed -i '/^o\.cold/d' "$CR"
# A STALE STARTUP SEED IS RE-MADE BY THE WARM-UP (SHIP-CUT-2 0d): a seed row primed at the key whose own --seed-check says stale
# (a new Studio build) runs again; it is PRIMED only when its --seed-check says fresh after the run
# (the fake seeds only under the warm-up's JAH_COLD_PRIME=1, as the real seed row does since SHIP-CUT-2 0d: every PRIMED below is
# the runner passing it; a stale seed met outside the warm-up fails at once)
printf '#!/bin/sh\ncase "$1" in --seed-check) [ -f fresh ] && exit 0; exit 3 ;; --seed) [ -f fresh ] && exit 0; [ "$JAH_COLD_PRIME" = 1 ] || { echo "FAIL: stale, the warm-up makes it"; exit 1; }; [ -n "$SEED_FAILS" ] || touch fresh; echo PASS; exit 0 ;; esac\nexit 64\n' > "$T/seedfx"
chmod +x "$T/seedfx"; mkdir -p "$T/s"
printf 's.seed\t%s\t\t%s\t--seed\n' "$T/s" "$T/seedfx" >> "$CR"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 SEED_FAILS=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q '^warm_cold_rows: s.seed: NOT PRIMED - the seed is not fresh after its run' && [ ! -e "$CS/s.seed.driver-key" ] &&
    ok "a seed row whose run left its seed stale: NOT PRIMED, no stamp, exit 1" || no "a seed left stale: '$out' rc $rc"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q '^warm_cold_rows: s.seed: PRIMED' && [ -e "$CS/s.seed.driver-key" ] && [ -f "$T/s/fresh" ] &&
    ok "...its run made it fresh: PRIMED and stamped" || no "a seed made: '$out' rc $rc"
rm -f "$T/s/fresh"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q '1 of 4 to prime: s.seed (its startup seed is stale (a new Studio build))' &&
    ok "a seed row stamped at the key whose --seed-check says stale (a new Studio build): cold-on cold for it alone, exit 3" ||
    no "a stale seed: '$out' rc $rc"
out=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ -f "$T/s/fresh" ] && echo "$out" | grep -q 'warm_cold_rows: s.seed: PRIMED' && ! echo "$out" | grep -q 'warm_cold_rows: [xyz].cold: PRIMED' &&
    ok "...and the warm-up re-makes it before any row (the other rows not run again)" || no "the stale seed re-made: '$out' rc $rc"
rm -f "$T/s/fresh"; out=$(cd "$T/s" && "$T/seedfx" --seed); rc=$?
[ $rc = 1 ] && [ ! -f "$T/s/fresh" ] && ok "a stale seed met OUTSIDE the warm-up (no JAH_COLD_PRIME) fails at once, nothing seeded (the row never re-seeds under its cap)" ||
    no "the seed outside the warm-up: '$out' rc $rc"
fi
if want coldcall; then
# THE CALL's OWN CASES (tooling.warm_cold_calls) start from every listed cold row primed - alone, its three rows listed first
[ "$PART" = coldcall ] && printf 'y.cold\t%s\t\t%s\nz.cold\t%s\t\t%s\n' "$T/y" "$T/seqfx" "$T/z" "$T/seqfx" >> "$CR"
DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on > /dev/null 2>&1
# EACH ROW ITS OWN HOLD: the card held past a row's wait - that row NOT RUN, exit 75, nothing stamped, the next call primes it
rm -f "$CS/z.cold.driver-key"
exec 8>"$T/gpu.lock"; flock 8
out=$(env -u JAH_GPU_LOCK_HELD JAH_COLD_MIN_LEFT=5 DISPLAY=:99 JAH_GPU_LOCK="$T/gpu.lock" "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on --budget 22); rc=$?   # (the least budget whose row still starts: its card wait ~2 s, the law the same)
exec 8>&-
[ $rc = 75 ] && echo "$out" | grep -q '^warm_cold_rows: z.cold: NOT RUN - the card stayed held' && [ ! -e "$CS/z.cold.driver-key" ] &&
    echo "$out" | grep -q 'call again: --only cold-on$' &&
    ok "the card held past the row's own wait: that row NOT RUN, exit 75 (call again), not stamped" || no "a held row: '$out' rc $rc"
# THE CALL's END (the bound): an exe still running at the call's deadline is killed and named - exit 75 (the next call primes it)
printf '#!/bin/sh\nsleep 30\n' > "$T/slowfx"; chmod +x "$T/slowfx"
printf 'z.slow\t%s\t\t%s\n' "$T/cwd" "$T/slowfx" > "$T/slow-rows"
t0=$(date +%s)
out=$(JAH_GPU_LOCK_HELD=1 JAH_COLD_MIN_LEFT=0 bash "$SRC/tests/support/warm_cold_rows.sh" "$T/slow-rows" $(( $(date +%s) + 1 )) "$T/slow-logs"); rc=$?
[ $rc = 75 ] && echo "$out" | grep -q 'z.slow: NOT PRIMED - killed at the call.s end' && [ $(( $(date +%s) - t0 )) -lt 15 ] &&
    ok "an exe past the call's end (its deadline) is killed and named - NOT PRIMED, exit 75" || no "the bound: '$out' rc $rc"
# EACH CALL's LOGS KEPT (SHIP-CUT-2 0c): two calls that boot leave two log directories, each with its rows' logs
rm -f "$CS/x.cold.driver-key"; out1=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on)
rm -f "$CS/x.cold.driver-key"; out2=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on)
l1=$(echo "$out1" | sed -n "s/^warm-app-cache: this call's logs: //p"); l2=$(echo "$out2" | sed -n "s/^warm-app-cache: this call's logs: //p")
[ -n "$l1" ] && [ -n "$l2" ] && [ "$l1" != "$l2" ] && [ -f "$l1/cold-on/x.cold.log" ] && [ -f "$l2/cold-on/x.cold.log" ] && [ -f "$l1/cold-on.log" ] &&
    ok "two calls leave two sets of logs (each call's directory, its rows' logs in it)" || no "two calls' logs: [$l1] [$l2]"
fi
if want keys; then
# A COLD STAMP CARRIES NO TREE PATH (CODING_RULES §6; TEST-STREAMLINE-1, measured before the fix: ship-cut-2 and photon-2 at
# one key held 6 of 6 cold stamps apart in lines 2 and 3): two toy trees at two paths, the same content, each primes the same
# cold row (its line spelled with its own tree) - their stamps are EQUAL; a media file's TIME moved reads warm, its CONTENT
# moved reads "the staged media rebuilt since"
toy_tree() {   # <dir>: a toy build the key reads, a cold row under it
    local b="$1/build"
    mkdir -p "$b/engine/generated" "$b/bin/media/Hlms" "$b/bin/media/Photon" "$b/bin/media/x" "$b/tests/row" "$b/tests/.shadercache"
    cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$b/engine/generated/"
    printf 'CMAKE_BUILD_TYPE:STRING=Debug\n%s\n' "$OGRE_LINE" > "$b/CMakeCache.txt"
    printf 'h' > "$b/bin/media/Hlms/a.any"; printf 'p' > "$b/bin/media/Photon/b.any"; printf 'x' > "$b/bin/media/x/c.png"
    printf '#!/bin/sh\nexit 0\n' > "$b/tests/row/fx"; chmod +x "$b/tests/row/fx"
    printf 'p.cold\t%s\t-JAH_X\tenv\t__GL_SHADER_DISK_CACHE_PATH=%s\t%s\t--in\t%s\n' "$b/tests/row" "$b/tests/row/dc" "$b/tests/row/fx" \
        "$b/tests/row/in.js" > "$b/tests/rows.txt"
}
toy_tree "$T/tA"; toy_tree "$T/tB"; touch -d '2001-01-01' "$T/tB/build/bin/media/x/c.png"
for t in tA tB; do
    DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$T/$t/build" --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/$t/build/tests/rows.txt" \
        --only cold-on > "$T/$t.out" 2>&1
done
sa=$(sed -n 2,3p "$T/tA/build/tests/.cold-rows/p.cold.driver-key" 2>/dev/null); sb=$(sed -n 2,3p "$T/tB/build/tests/.cold-rows/p.cold.driver-key" 2>/dev/null)
[ -n "$sa" ] && [ "$sa" = "$sb" ] && ok "two trees at two paths, one content: their cold stamps equal (the row's line with <build>, the media by content, a file's time apart)" ||
    no "path-free stamps: [$sa] vs [$sb] ($(tail -3 "$T/tA.out"))"
out=$("$W" "$T/tB/build" --check --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/tB/build/tests/rows.txt" --only cold-on); rc=$?
[ $rc = 0 ] && ok "...and a staged file touched (its time alone): cold-on warm" || no "a touched media file: '$out' rc $rc"
# THE OTHER SPELLING (the stage-1 audit's finding 11): the same tree called through a symlink (~/Developer -> /mnt/work/Developer
# on this box) reads its stamps warm - its lines' spelling stripped whichever way it was called (red on the base: stale)
ln -s "$T/tB" "$T/tB-link"
out=$("$W" "$T/tB-link/build" --check --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/tB/build/tests/rows.txt" --only cold-on); rc=$?
[ $rc = 0 ] && ok "...and called through another spelling of its path (a symlink): cold-on still warm" || no "another spelling: '$out' rc $rc"
printf 'y' > "$T/tB/build/bin/media/x/c.png"
out=$("$W" "$T/tB/build" --check --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/tB/build/tests/rows.txt" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'p.cold (the staged media rebuilt since)' && ok "...a staged file's content moved: cold-on cold, the row named" ||
    no "a media content change: '$out' rc $rc"
# THE NARROWED KEY, NO TOP-UP (TEST-STREAMLINE-1 1a; WARM-SHARE-1 ruling 5; #126's nonce case on the narrowed key, CPU half): on
# a toy tree warmed by hand at its key - ONLY the engine's full build id moved: the key holds, EVERY boot warm (no top-up); a byte of the staged Hlms media, a ray-tier SPIR-V header or the shader cache's code id moved:
# the KEY moves, every boot cold (the shader that changed is the one that runs - the card half is block A)
toy_tree "$T/tK"; KB="$T/tK/build"; KT="$KB/tests"; mkdir -p "$KB/engine/generated/rayquery"; printf 'spv1' > "$KB/engine/generated/rayquery/rq_x_spv.h"
printf '[{"command": "c++ -DJAHSHAKA_OGRE_FORK_COMMIT=%s -c x.cpp"}]\n' 1111111111111111111111111111111111111111 > "$KB/compile_commands.json"
kk=$("$W" "$KB" --key); ck=$("$W" "$KB" --cold-key)
KX=(--app "$T/app" --lab "$T/lab" --cold-rows "$KB/tests/rows.txt" --tests-dir "$KT")
toy_cache "$KT/.shadercache" 1 2; toy_cache "$KT/.shadercache-engine" 1 2
printf '%s\n%s\n%s\n' "$kk" "k1 k2 " "sets none" > "$KT/.shadercache.warm-key"; printf '%s\n%s\n' "$kk" "k1 k2 " > "$KT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$kk" app > "$KT/.shadercache.driver-key"; printf '%s\n%s\n' "$kk" lab > "$KT/.shadercache-engine.driver-key"
DISPLAY=:99 JAH_GPU_LOCK_HELD=1 "$W" "$KB" "${KX[@]}" --only cold-on > /dev/null 2>&1
# WHY A CALL BOOTS, KEPT (TE-WARM-WHY (A)): the cold-on call's own log dir holds why.txt - its "cold at <key>: <why>" line and the
# key's inputs, one line a term (red before (A): no why.txt)
wy=$(ls -1t "$KT"/warm-logs/*/why.txt 2> /dev/null | head -1)
[ -n "$wy" ] && grep -q "^cold at $kk: the cold rows' own caches (cold-on): 1 of 1 to prime: p.cold (no prime recorded)" "$wy" &&
    grep -q '^inputs:$' "$wy" && grep -q '^cache-code [0-9a-f]' "$wy" && grep -q '^media-Hlms [0-9a-f]' "$wy" && grep -q '^rq-spirv [0-9a-f]' "$wy" &&
    ok "A2: a booting call keeps why.txt in its log dir - the cold-at line and the key's inputs, a term a line" ||
    no "A2 why.txt: [${wy:-none}] $(tr '\n' '|' < "${wy:-/dev/null}" | cut -c1-300)"
out=$("$W" "$KB" --check "${KX[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $kk" ] && ok "the narrowed key's toy tree warm at $kk (every boot)" || no "the toy tree warm: '$out' rc $rc"
sed -i 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\)/#define JAHSHAKA_ENGINE_BUILD_ID "f\1/' "$KB/engine/generated/jahshaka_engine_build_id.h"
out=$("$W" "$KB" --check "${KX[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $kk" ] && [ "$("$W" "$KB" --key)" = "$kk" ] &&
    ok "an engine edit alone (the full build id): the KEY holds and every boot reads WARM - no top-up (WARM-SHARE-1 ruling 5)" ||
    no "the engine edit alone: '$out' rc $rc"
nonce() {   # <what> <file> <bytes> <term>: the key moves, every boot cold; a stamp that kept its inputs names the moved term
    # (TE-WARM-WHY (A): the cold row's stamp was written by its prime - <stamp>.inputs; the hand-made -off/-on stamps kept none)
    cp "$2" "$T/nonce.bak"; printf '%s' "$3" >> "$2"
    local o r; o=$("$W" "$KB" --check "${KX[@]}"); r=$?
    [ $r = 3 ] && [ "$("$W" "$KB" --key)" != "$kk" ] && echo "$o" | grep -q '(app-on): the key moved' &&
        echo "$o" | grep -q "(cold-on): 1 of 1 to prime: p.cold (the cold key moved (primed at $ck - moved: $4))" &&
        ok "a NONCE in $1: the key moves, every boot cold, the prime's stamp names the term that moved ($4)" || no "the nonce in $1: '$o' rc $r"
    cp "$T/nonce.bak" "$2"
}
nonce "the staged Hlms media" "$KB/bin/media/Hlms/a.any" "// nonce" media-Hlms
nonce "a ray-tier SPIR-V header" "$KB/engine/generated/rayquery/rq_x_spv.h" "1" rq-spirv
# THE COLD ROWS' KEY (TE-WARM-WHY (C)): a FORK-ONLY move (the fork commit, no producer file: the three compile-path ids equal)
# moves the rig KEY - the -off/-on boots cold - and leaves the cold rows WARM (their driver caches read no fork commit); a
# compile-path id moved re-primes them, the term named. (red before (C): the fork commit re-primed every cold row)
cp "$KB/compile_commands.json" "$T/cc.bak"; sed -i 's/=1111111111111111111111111111111111111111/=2222222222222222222222222222222222222222/' "$KB/compile_commands.json"
o=$("$W" "$KB" --check "${KX[@]}"); r=$?
[ $r = 3 ] && [ "$("$W" "$KB" --key)" != "$kk" ] && [ "$("$W" "$KB" --cold-key)" = "$ck" ] && echo "$o" | grep -q '(app-off): the key moved' &&
    ! echo "$o" | grep -q '(cold-on)' && [ -z "$("$W" "$KB" --check "${KX[@]}" --only cold-on | grep -v "^warm at")" ] &&
    ok "C1: a fork-only move (the commit, the compile-path ids equal): the rig key moves, the cold rows stay WARM at their cold key" ||
    no "C1 the fork-only move: '$o' rc $r"
cp "$T/cc.bak" "$KB/compile_commands.json"; cp "$KB/engine/generated/jahshaka_engine_build_id.h" "$T/idc.bak"
sed -i 's/^#define JAHSHAKA_COMPILE_PATH_MICROCODE_ID "\([0-9a-f]\)/#define JAHSHAKA_COMPILE_PATH_MICROCODE_ID "f\1/' "$KB/engine/generated/jahshaka_engine_build_id.h"
o=$("$W" "$KB" --check "${KX[@]}" --only cold-on); r=$?
[ $r = 3 ] && echo "$o" | grep -q "p.cold (the cold key moved (primed at $ck - moved: compile-microcode))" &&
    ok "C2: a compile-path id moved (a producer file of the fork): the cold rows re-prime, the term named (compile-microcode)" ||
    no "C2 the compile-path move: '$o' rc $r"
cp "$T/idc.bak" "$KB/engine/generated/jahshaka_engine_build_id.h"
cp "$KB/engine/generated/jahshaka_engine_build_id.h" "$T/id.bak"
sed -i 's/^#define JAHSHAKA_SHADER_CACHE_CODE_ID "\([0-9a-f]\)/#define JAHSHAKA_SHADER_CACHE_CODE_ID "e\1/' "$KB/engine/generated/jahshaka_engine_build_id.h"
o=$("$W" "$KB" --check "${KX[@]}"); r=$?
[ $r = 3 ] && [ "$("$W" "$KB" --key)" != "$kk" ] && ok "the shader cache's own code moved (JAHSHAKA_SHADER_CACHE_CODE_ID): the key moves" || no "the code id: '$o' rc $r"
grep -v SHADER_CACHE_CODE_ID "$T/id.bak" > "$KB/engine/generated/jahshaka_engine_build_id.h"
"$W" "$KB" --key > /dev/null 2>&1; r=$?
[ $r = 2 ] && ok "a build id header without the code id (an unbuilt or older tree): no key, exit 2 - never a key without the cache's code" || no "no code id: rc $r"
# C3 THE COLD KEY FAILS CLOSED (the lead's ruling 2026-11-23): a header that cannot say one of the three compile-path ids gives
# NO key - --cold-key and --key exit 2 naming the id - never a cold key that silently drops the fork's producers (red before: a
# cold key printed with an empty term, exit 0); the control: the whole header gives both keys
c3=""
for g in MICROCODE PREPROCESSOR COMPUTE; do
    grep -v "JAHSHAKA_COMPILE_PATH_${g}_ID" "$T/id.bak" > "$KB/engine/generated/jahshaka_engine_build_id.h"
    e=$("$W" "$KB" --cold-key 2>&1 >/dev/null); r1=$?; "$W" "$KB" --key > /dev/null 2>&1; r2=$?
    [ $r1 = 2 ] && [ $r2 = 2 ] && echo "$e" | grep -q "no JAHSHAKA_COMPILE_PATH_${g}_ID in " || c3="$c3 $g:$r1/$r2"
done
cp "$T/id.bak" "$KB/engine/generated/jahshaka_engine_build_id.h"
[ -z "$c3" ] && [ "$("$W" "$KB" --cold-key)" = "$ck" ] && [ "$("$W" "$KB" --key)" = "$kk" ] &&
    ok "C3: a header without a compile-path id: --cold-key and --key exit 2 naming it (each of the three); the whole header gives both keys" ||
    no "C3 the cold key fails closed: [$c3] ($("$W" "$KB" --cold-key 2>&1))"
# C4 THE FORK's Hlms IMPLEMENTATION IS A TERM OF THE COLD KEY (the lead's ruling 2026-11-23, from the final read: OgreHlmsPbs.cpp
# and OgreHlmsUnlit.cpp WRITE shader text - setProperty/setPiece - and are in no compile-path id): the toy build's fork is a toy
# tree (OGRE_NEXT_SOURCE) - an edit of a fork file OUTSIDE Components/Hlms (OgreMain) leaves the cold key equal; an edit of
# Components/Hlms/*/src and of */include moves it, a cold row naming the term (hlms-impl); a fork with no Components/Hlms gives
# no key (exit 2, failed closed). (red before: the src edit moved nothing, the missing tree gave a key)
FK="$T/fork4"; mkdir -p "$FK/OgreMain/src" "$FK/Components/Hlms/Pbs/src" "$FK/Components/Hlms/Pbs/include"
printf 'log 1' > "$FK/OgreMain/src/OgreLog.cpp"; printf 'pbs 1' > "$FK/Components/Hlms/Pbs/src/OgreHlmsPbs.cpp"
printf 'hdr 1' > "$FK/Components/Hlms/Pbs/include/OgreHlmsPbs.h"
cp "$KB/CMakeCache.txt" "$T/kcache.bak"; printf 'CMAKE_BUILD_TYPE:STRING=Debug\nOGRE_NEXT_SOURCE:PATH=%s\n' "$FK" > "$KB/CMakeCache.txt"
c40=$("$W" "$KB" --cold-key); k40=$("$W" "$KB" --key)
printf 'log 2' > "$FK/OgreMain/src/OgreLog.cpp"; c41=$("$W" "$KB" --cold-key)
printf 'pbs 2' > "$FK/Components/Hlms/Pbs/src/OgreHlmsPbs.cpp"; c42=$("$W" "$KB" --cold-key)
o=$("$W" "$KB" --check "${KX[@]}" --only cold-on)
printf 'hdr 2' > "$FK/Components/Hlms/Pbs/include/OgreHlmsPbs.h"; c43=$("$W" "$KB" --cold-key); k43=$("$W" "$KB" --key)
mv "$FK/Components" "$FK/Components.gone"; e=$("$W" "$KB" --cold-key 2>&1 >/dev/null); r=$?
mv "$FK/Components.gone" "$FK/Components"; cp "$T/kcache.bak" "$KB/CMakeCache.txt"
[ -n "$c40" ] && [ "$c41" = "$c40" ] && [ "$c42" != "$c41" ] && [ "$c43" != "$c42" ] && [ "$k43" = "$k40" ] &&
    echo "$o" | grep -q "moved: .*hlms-impl" && [ $r = 2 ] && echo "$e" | grep -q "no fork Hlms implementation" &&
    ok "C4: the fork's Hlms implementation is a cold-key term - an OgreMain edit leaves it equal, a Components/Hlms src or include edit moves it (named hlms-impl), the rig key unmoved by either; no Components/Hlms: exit 2" ||
    no "C4 the Hlms implementation term: keys [$c40 $c41 $c42 $c43] rig [$k40 $k43] '$(echo "$o" | head -1 | cut -c1-200)' missing rc $r '$e'"
fi
if want inherit; then
# THE INHERITANCE (TEST-STREAMLINE-1 1a; the reader's I1-I7; WARM-SHARE-1 B4): a parent toy tree warm at its key (both shared
# caches stamped; a cold row primed, its private driver cache and its startup seed beside it) and a child toy tree of the same
# content at another path. --inherit: the cold row's stamp, its driver cache and its seed taken (I6/I7, the path-free stamps of
# 1a-1), the two shared caches NEVER (I1: the box's warm store is the one way); the child's driver writing its taken files
# leaves the parent's as they were (I2b); a child whose key differs takes nothing (I3).
[ "$PART" = inherit ] && toy_tree() {   # (the keys part's helper; this part may run alone)
    local b="$1/build"
    mkdir -p "$b/engine/generated" "$b/bin/media/Hlms" "$b/bin/media/Photon" "$b/bin/media/x" "$b/tests/row" "$b/tests/.shadercache"
    cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$b/engine/generated/"
    printf 'CMAKE_BUILD_TYPE:STRING=Debug\n%s\n' "$OGRE_LINE" > "$b/CMakeCache.txt"
    printf 'h' > "$b/bin/media/Hlms/a.any"; printf 'p' > "$b/bin/media/Photon/b.any"; printf 'x' > "$b/bin/media/x/c.png"
    printf '#!/bin/sh\nexit 0\n' > "$b/tests/row/fx"; chmod +x "$b/tests/row/fx"
    printf 'p.cold\t%s\t-JAH_X\tenv\t__GL_SHADER_DISK_CACHE_PATH=%s\t%s\t--in\t%s\n' "$b/tests/row" "$b/tests/row/dc" "$b/tests/row/fx" \
        "$b/tests/row/in.js" > "$b/tests/rows.txt"
}
toy_tree "$T/iP"; toy_tree "$T/iC"; PB="$T/iP/build"; CB="$T/iC/build"; PT="$PB/tests"; CT="$CB/tests"
IX() { "$W" "$1" --app "$T/app" --lab "$T/lab" --cold-rows "$1/tests/rows.txt" "${@:2}"; }
pk=$(IX "$PB" --key)
rm -rf "$PT/.shadercache"; toy_cache "$PT/.shadercache" 1 2; toy_cache "$PT/.shadercache-engine" 1 2
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$PT/.shadercache.warm-key"; printf '%s\n%s\n' "$pk" "k1 k2 " > "$PT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$pk" app > "$PT/.shadercache.driver-key"; printf '%s\n%s\n' "$pk" lab > "$PT/.shadercache-engine.driver-key"
DISPLAY=:99 JAH_GPU_LOCK_HELD=1 IX "$PB" --only cold-on > /dev/null 2>&1
mkdir -p "$PT/row/dc" "$PT/row/seed-x"; echo drv > "$PT/row/dc/GLCache.bin"; echo k > "$PT/row/seed-x/tree-key.txt"
out=$(IX "$PB" --check); rc=$?
[ $rc = 0 ] && ok "the parent toy tree warm at $pk (every boot)" || no "the parent toy tree: '$out' rc $rc"
out=$(IX "$CB" --check); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'no warm-up recorded' && ok "...the child, the same content at another path: cold before" || no "the child before: '$out' rc $rc"
# I1 (WARM-SHARE-1 B4: THE STORE IS THE ONE WAY): --inherit takes the parent's cold-row prime - its stamp, its private driver
# cache, its startup seed - and NEVER the two shared caches: the child's app and engine caches stay its own (cold here; the
# box's warm store serves them at its first warm-up). (red on the old script: both shared caches taken, the child "warm")
out=$(IX "$CB" --inherit "$PB"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "inherited at $pk from $PB: 1 cold row(s)" && ! echo "$out" | grep -q '\.shadercache' &&
    [ ! -e "$CT/.shadercache/cache-manifest.txt" ] && [ ! -e "$CT/.shadercache-engine/cache-manifest.txt" ] &&
    [ ! -e "$CT/.shadercache.warm-key" ] && [ ! -e "$CT/.shadercache.driver-key" ] &&
    ok "I1: --inherit takes the cold row's prime and NEVER the two shared caches (the store is the one way, B4)" ||
    no "I1 inherit: '$out' rc $rc"
out=$(IX "$CB" --check); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "the app rows' cache (app-off): no warm-up recorded" && ! echo "$out" | grep -q '(cold-on)' &&
    ok "...the child's cold rows warm, its shared caches cold until the store or a boot warms them" || no "the child after: '$out' rc $rc"
[ -f "$CT/row/dc/GLCache.bin" ] && [ -f "$CT/row/seed-x/tree-key.txt" ] &&
    [ "$(sed -n 2,3p "$CT/.cold-rows/p.cold.driver-key")" = "$(sed -n 2,3p "$PT/.cold-rows/p.cold.driver-key")" ] &&
    ok "I6/I7: the cold row's private driver cache and its seed taken; its stamp the parent's, paths aside" ||
    no "I6/I7: the taken state"
# I2b (the stage-1 audit's finding 1): the DRIVER writes its cache file in place - a child's driver appending to the taken
# cache must leave the parent's bytes as they were (red when the inheritance hard-linked it)
printf 'grown by the child' >> "$CT/row/dc/GLCache.bin"; printf 'x' >> "$CT/row/seed-x/tree-key.txt"
[ "$(cat "$PT/row/dc/GLCache.bin")" = drv ] && [ "$(cat "$PT/row/seed-x/tree-key.txt")" = k ] &&
    ok "I2b: a child's driver writing its taken driver cache in place (and its seed's stamp) leaves the parent's files as they were" ||
    no "I2b: the parent's driver cache or seed was written through the child ($(cat "$PT/row/dc/GLCache.bin"))"
# I3: a child whose key differs (a staged Hlms byte) takes nothing and stays cold
toy_tree "$T/iD"; DB="$T/iD/build"; printf 'H' > "$DB/bin/media/Hlms/a.any"
out=$(IX "$DB" --inherit "$PB"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "inherited nothing - the parent's cold key $("$W" "$PB" --cold-key) is not this tree's" && [ ! -e "$DB/tests/.shadercache.warm-key" ] &&
    ok "I3: a child at another key: nothing taken, exit 3" || no "I3 another key: '$out' rc $rc"
fi
if want store; then
# THE BOX's WARM STORE (URGENT #193's class; the lead's design 2026-11-21): JAH_WARM_STORE names a store directory (the lead's
# tools set it; this repo names no box path). S1 a tree warm at its key PUBLISHES its caches (and its -on stamps) when it
# answers "warm" - BY HARD LINKS (one inode, no copy); S2 another tree at the SAME key, cold, TAKES them - warm with no boot
# (its fake app never runs), its files the store's own inodes, and a write the engine's way (by rename) leaves the store's
# bytes as they were; S3 a TORN stored cache is not taken - that boot runs; S4 with
# JAH_WARM_STORE unset a call that would boot cold is REFUSED (B13) and with --no-store nothing is read or written there (as before); S5 the key's lock held by another tree: this call WAITS on
# it (said, no card) and takes after it; S6 another key takes nothing; S7 a toy-shaped call under an inherited store writes nothing;
# S8 ONE FILESYSTEM (the owner, 2026-11-22: the box's store beside the trees): a store on another filesystem is NOT taken from
# (said, the tree boots) and a shared cache on another is NOT linked into a home by fresh_home.sh (said) - never a copy
# (red on the old harness: both copied, silently)
[ "$PART" = store ] && toy_tree() {   # (the keys and inherit parts' helper; this part may run alone)
    local b="$1/build"
    mkdir -p "$b/engine/generated" "$b/bin/media/Hlms" "$b/bin/media/Photon" "$b/bin/media/x" "$b/tests/.shadercache"
    cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$b/engine/generated/"
    printf 'CMAKE_BUILD_TYPE:STRING=Debug\n%s\n' "$OGRE_LINE" > "$b/CMakeCache.txt"
    printf 'h' > "$b/bin/media/Hlms/a.any"; printf 'p' > "$b/bin/media/Photon/b.any"; printf 'x' > "$b/bin/media/x/c.png"
}
ST="$T/store"; mkdir -p "$ST"
printf '#!/bin/sh\necho "BOOTED $*" >> %s/s-booted\nexec %s "$@"\n' "$T" "$T/fake-app" > "$T/s-app"; chmod +x "$T/s-app"
# (the store toys drive the script with fakes: they say so - JAH_WARM_STORE_ALLOW_FAKES=1, with THEIR OWN scratch store - or the
# store refuses a call of a toy's shape, S7)
SX() { local b="$1"; shift; DISPLAY=:99 JAH_GPU_LOCK_HELD=1 JAH_WARM_STORE_ALLOW_FAKES=1 "$W" "$b" --app "$T/s-app" --lab "$T/s-app" \
           --cold-rows "$T/cold-none" --only app-off,app-on,engine-off,engine-on "$@"; }
toy_tree "$T/sP"; PB="$T/sP/build"; PT="$PB/tests"
pk=$("$W" "$PB" --key)
rm -rf "$PT/.shadercache"; toy_cache "$PT/.shadercache" 1 2; toy_cache "$PT/.shadercache-engine" 1 2
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$PT/.shadercache.warm-key"; printf '%s\n%s\n' "$pk" "k1 k2 " > "$PT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$pk" s-app > "$PT/.shadercache.driver-key"; printf '%s\n%s\n' "$pk" s-app > "$PT/.shadercache-engine.driver-key"
out=$(JAH_WARM_STORE="$ST" SX "$PB"); rc=$?
[ $rc = 0 ] && [ "$(echo "$out" | head -1)" = "warm at $pk" ] && echo "$out" | grep -q "PUBLISHED to the box's warm store at $pk" &&
    cmp -s "$PT/.shadercache/hlms.1.bin" "$ST/$pk/app/cache/hlms.1.bin" && [ -f "$ST/$pk/engine/cache/cache-manifest.txt" ] &&
    [ "$(stat -c %i "$PT/.shadercache/hlms.1.bin")" = "$(stat -c %i "$ST/$pk/app/cache/hlms.1.bin")" ] &&
    [ "$(sed -n 2p "$ST/$pk/app/driver-key")" = s-app ] && ok "S1: a tree warm at $pk publishes its two caches and their -on stamps when it answers warm" ||
    no "S1 publish: '$out' rc $rc"
toy_tree "$T/sC"; CB="$T/sC/build"; CT="$CB/tests"; rm -f "$T/s-booted"
out=$(JAH_WARM_STORE="$ST" SX "$CB"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "TAKEN from the box's warm store at $pk" && echo "$out" | grep -q "from the store, nothing booted" &&
    [ ! -e "$T/s-booted" ] && cmp -s "$ST/$pk/app/cache/hlms.1.bin" "$CT/.shadercache/hlms.1.bin" &&
    [ "$(stat -c %i "$ST/$pk/app/cache/hlms.1.bin")" = "$(stat -c %i "$CT/.shadercache/hlms.1.bin")" ] &&
    [ "$(SX "$CB" --check)" = "warm at $pk" ] && ok "S2: a tree cold at the SAME key takes both caches and the -on stamps: warm, its app never booted" ||
    no "S2 take: '$out' rc $rc booted [$(cat "$T/s-booted" 2>/dev/null)]"
printf 'GROWN' > "$CT/.shadercache/hlms.1.bin.tmp"; mv "$CT/.shadercache/hlms.1.bin.tmp" "$CT/.shadercache/hlms.1.bin"
[ "$(cat "$ST/$pk/app/cache/hlms.1.bin")" = 0123456789 ] &&
    ok "S2b: the taken files are the store's own (one inode) - a tree's write the engine's way (by rename) leaves the store's as they were" ||
    no "S2b: the store was written through the tree"
# S2c THE SERVE TAKES NO LOCK (WARM-SHARE-1's law: a second tree at one KEY is served with no card): a third tree at the same
# key with the card's lock (a private one) HELD by another process all through - the serve ends while the holder still holds
# it (the margin: the holder's 8-s sleep), nothing booted, and the lock's hold log gains no line. (red with the store taken
# away - JAH_WARM_STORE unset: the tree boots and waits on the held card)
sN() { toy_tree "$T/sN$1"; rm -f "$T/s-booted" "$T/gpuN.lock.holds"
       ( exec 8>"$T/gpuN.lock"; flock 8; : > "$T/gpuN.held"; sleep 8 ) & np=$!
       for _ in $(seq 50); do [ -e "$T/gpuN.held" ] && break; sleep 0.1; done
       out=$(env -u JAH_GPU_LOCK_HELD DISPLAY=:99 JAH_GPU_LOCK="$T/gpuN.lock" JAH_WARM_STORE_ALLOW_FAKES=1 ${2:+JAH_WARM_STORE="$2"} "$W" "$T/sN$1/build" \
             --app "$T/s-app" --lab "$T/s-app" --cold-rows "$T/cold-none" --only app-off,app-on,engine-off,engine-on --budget 20 2>&1); rc=$?
       kill -0 $np 2>/dev/null; still=$?; kill $np 2>/dev/null; wait $np 2>/dev/null; rm -f "$T/gpuN.held"; }
sN 1 "$ST"
[ $rc = 0 ] && [ $still = 0 ] && echo "$out" | grep -q "from the store, nothing booted" && [ ! -e "$T/s-booted" ] && [ ! -s "$T/gpuN.lock.holds" ] &&
    ok "S2c: the serve takes NO lock - a second tree at the key warm while another holds the card, nothing booted, no hold logged" ||
    no "S2c the serve's lock: rc $rc still-held $still booted [$(cat "$T/s-booted" 2>/dev/null)] holds [$(cat "$T/gpuN.lock.holds" 2>/dev/null)] '$(echo "$out" | tail -2)'"
toy_tree "$T/sU"; UB="$T/sU/build"; rm -f "$T/s-booted"
out0=$(env -u JAH_WARM_STORE bash -c "$(declare -f SX); W='$W'; T='$T'; SX '$UB' --only engine-off" 2>&1); rc0=$?
booted0=$([ -e "$T/s-booted" ] && echo y || echo n)
out=$(env -u JAH_WARM_STORE bash -c "$(declare -f SX); W='$W'; T='$T'; SX '$UB' --only engine-off --no-store" 2>&1); rc=$?
[ $rc0 = 2 ] && [ $booted0 = n ] && echo "$out0" | grep -q "REFUSED (exit 2) - this call would boot cold" && echo "$out0" | grep -q -- "--no-store" \
    && [ -e "$T/s-booted" ] && ! echo "$out" | grep -q "store" \
    && ok "S4: JAH_WARM_STORE unset - a call that would boot cold is REFUSED (exit 2, the lines to run named; the errors audit B13); with --no-store no store is read or written and the cold tree boots as before" ||
    no "S4 unset: rc $rc0 booted $booted0 '$out0' // rc $rc '$out'"
printf '9876543210' > "$ST/$pk/engine/cache/hlms.1.bin.t"; mv "$ST/$pk/engine/cache/hlms.1.bin.t" "$ST/$pk/engine/cache/hlms.1.bin"   # (by rename: its inode is the publisher's too)
toy_tree "$T/sT"; TB="$T/sT/build"; rm -f "$T/s-booted"
out=$(JAH_WARM_STORE="$ST" SX "$TB" --only engine-off); rc=$?
echo "$out" | grep -q "the store's engine cache at $pk fails its own manifest - not taken" && [ -e "$T/s-booted" ] &&
    ok "S3: a TORN stored cache is not taken - its boot runs (#117's rule)" || no "S3 torn: '$out' rc $rc"
rm -rf "$ST/$pk/engine"; cp -a "$PT/.shadercache-engine" "$T/eng.ok"; mkdir -p "$ST/$pk/engine"; cp -a "$T/eng.ok" "$ST/$pk/engine/cache"
cp "$PT/.shadercache-engine.warm-key" "$ST/$pk/engine/warm-key"
toy_tree "$T/sL"; LB="$T/sL/build"; rm -f "$T/s-booted"
( exec 8>"$ST/$pk/.key.lock"; flock 8; sleep 1.5 ) & lp=$!
sleep 0.3
out=$(JAH_WARM_STORE="$ST" SX "$LB" --only engine-off --budget 20); rc=$?
wait $lp
[ $rc = 0 ] && echo "$out" | grep -q "is being warmed by another tree .* waiting on the store's key lock (no card)" &&
    echo "$out" | grep -q "TAKEN from the box's warm store" && [ ! -e "$T/s-booted" ] &&
    ok "S5: the key's lock held by another tree - this call waits on it (no card) and then takes, no boot" || no "S5 the key lock: '$out' rc $rc"
# S7 THE NEGATIVE CONTROL (URGENT #193's own red, 2026-11-21 16:01): a toy run under an INHERITED real-looking store - the
# script on the REAL build dir (its real key), fakes, a scratch tests dir, no ALLOW - writes NOTHING there: neither the cold
# toy's take/boot nor the warm toy's publish (red on photon-7 8407d7fff: it published 10-byte caches at the real key)
RS="$T/real-looking-store"; mkdir -p "$RS"; RB="$T/s7"; mkdir -p "$RB/tests"
o1=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 JAH_WARM_STORE="$RS" "$W" "$BUILD" --tests-dir "$RB/tests" "${F[@]}" --only engine-off 2>&1)
o2=$(DISPLAY=:99 JAH_GPU_LOCK_HELD=1 JAH_WARM_STORE="$RS" "$W" "$PB" --app "$T/s-app" --lab "$T/s-app" --cold-rows "$T/cold-none" \
     --only app-off,engine-off 2>&1)
[ -z "$(ls -A "$RS")" ] && echo "$o1" | grep -q "the warm store is NOT used - this call overrides" &&
    echo "$o2" | grep -q "the warm store is NOT used" && ok "S7: a toy-shaped call under an inherited store (the real build's key, fakes) writes NOTHING there, warm or cold" ||
    no "S7 the inherited store: $(ls -A "$RS" | head -3) // $(echo "$o1" | tail -1) // $(echo "$o2" | tail -1)"
# S8 ONE FILESYSTEM: the same warm store on ANOTHER filesystem (/dev/shm, a tmpfs) - not taken, said, the tree boots; and a
# tree's shared cache on another filesystem - fresh_home.sh links none of it into a home and says so (never a copy)
XS=$(mktemp -d /dev/shm/warm-store-toy.XXXXXX); cp -a "$ST/$pk" "$XS/"
toy_tree "$T/sX"; XB="$T/sX/build"; rm -f "$T/s-booted"
out=$(JAH_WARM_STORE="$XS" SX "$XB" --only engine-off); rc=$?
mkdir -p "$T/b8/tests"; touch "$T/b8/CMakeCache.txt"; toy_cache "$XS/fh" 1 2; f8=$(find "$XS/fh" -type f | wc -l); ln -s "$XS/fh" "$T/b8/tests/.shadercache"
o8=$(env -u DISPLAY JAH_WARM_CHECK=0 sh "$SRC/tests/support/fresh_home.sh" --warm --keep-home "$T/b8/tests/h8" -- true 2>&1)
n8=$(find "$T/b8/tests/h8/.local/share/Jahshaka/shadercache" -type f 2>/dev/null | wc -l)
rm -rf "$XS"
echo "$out" | grep -q "the store's engine cache NOT taken - $XS is not on the filesystem of" && [ -e "$T/s-booted" ] &&
    echo "$o8" | grep -q "$f8 file(s) of $T/b8/tests/.shadercache NOT linked into the home" && [ "$n8" = 0 ] &&
    ok "S8: ONE FILESYSTEM - a store on another is not taken (said, the tree boots); a shared cache on another is not linked by fresh_home.sh (said) - never a copy" ||
    no "S8 one filesystem: rc $rc '$(echo "$out" | grep -i store | head -2)' fresh_home '$o8' files $n8"
toy_tree "$T/sK"; KB="$T/sK/build"; printf 'H' > "$KB/bin/media/Hlms/a.any"; rm -f "$T/s-booted"
out=$(JAH_WARM_STORE="$ST" SX "$KB" --only engine-off); rc=$?
! echo "$out" | grep -q "TAKEN" && [ -e "$T/s-booted" ] && ok "S6: another key takes nothing from the store - it warms itself" || no "S6 another key: '$out' rc $rc"
# S9 THE STORE's COPY CARRIES ITS PUBLISHER's STARTUP-PASS RECORD (TE-WARM-WHY (D); spikes/validation-slow/: the box's store held
# records of 14:18 and 15:10 while every tree taking from it had another pass id): a tree at the SAME key but another pass id
# takes the app cache and its -on stamp from the store (no card) and is still COLD for the record - ONE app-off boot (the fake
# app runs the pass; the union kept), then warm. (red on the script before (D): "from the store, nothing booted", the record
# the publisher's - every app row of that tree then paid the pass, the two validation rows under the layer each)
p9a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa; p9b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
ST9="$T/store9"; mkdir -p "$ST9"
toy_tree "$T/sQ"; QB="$T/sQ/build"; QT="$QB/tests"; mkdir -p "$QB/generated"
printf '#define JAH_STARTUP_PASS_CODE_ID "%s"\n' "$p9a" > "$QB/generated/jah_startup_pass_id.h"
rm -rf "$QT/.shadercache"; toy_cache "$QT/.shadercache" 1 2; toy_cache "$QT/.shadercache-engine" 1 2
printf 'pass=%s|media=m|cache=c\n' "$p9a" > "$QT/.shadercache/warm-pass.key"
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$QT/.shadercache.warm-key"; printf '%s\n%s\n' "$pk" "k1 k2 " > "$QT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$pk" s-app > "$QT/.shadercache.driver-key"; printf '%s\n%s\n' "$pk" s-app > "$QT/.shadercache-engine.driver-key"
o9p=$(JAH_WARM_STORE="$ST9" SX "$QB")
toy_tree "$T/sR"; RB9="$T/sR/build"; mkdir -p "$RB9/generated"
printf '#define JAH_STARTUP_PASS_CODE_ID "%s"\n' "$p9b" > "$RB9/generated/jah_startup_pass_id.h"
rm -f "$T/s-booted"; : > "$T/s9.pass"
out=$(FAKE_PASS_ID=$p9b FAKE_PASS_LOG="$T/s9.pass" JAH_WARM_STORE="$ST9" SX "$RB9" --only app-off,app-on); rc=$?
[ $rc = 0 ] && echo "$o9p" | grep -q "PUBLISHED to the box's warm store at $pk" && echo "$out" | grep -q "TAKEN from the box's warm store at $pk" &&
    echo "$out" | grep -q "the startup pass's record is another build's (pass aaaaaaaa, this build's bbbbbbbb)" &&
    [ "$(grep -c BOOTED "$T/s-booted" 2>/dev/null)" = 1 ] && [ "$(cat "$T/s9.pass")" = ran ] &&
    grep -q "^pass=$p9b|" "$RB9/tests/.shadercache/warm-pass.key" && [ "$(SX "$RB9" --only app-off,app-on --check)" = "warm at $pk" ] &&
    ok "S9: a store copy at the key carries its publisher's pass record - taken, then ONE app-off boot ran the pass, the record this build's, warm" ||
    no "S9 the store's pass record: rc $rc booted [$(cat "$T/s-booted" 2>/dev/null | tr '\n' ' ')] pass [$(tr '\n' ' ' < "$T/s9.pass")] '$(echo "$out" | grep -iE "taken|record|cold at|warm at" | head -4 | tr '\n' '|')'"
# S10 A CACHE WARM AT THE KEY NEVER TAKES FROM THE STORE (TE-WARM-WHY's final read, VERDICT BACK, the lead's ruling 2026-11-22):
# a tree warm at the key whose ONLY why is a stale pass record, the store at the same key holding a DIFFERENT (older) cache - the
# refresh runs on the tree's OWN union: nothing TAKEN, the store's key lock never taken (held here by another process all through:
# a call that opened the store would wait on it and say so), ONE app-off boot, the local hlms and compute files the same inodes,
# the record this build's. (red on 94461a5cb: the store's copy TAKEN over the tree's union after the lock's wait - the class
# WARM-SHARE-1 ruling 5 deleted; measured on the box: 1189 -> 804 shaders)
p10a=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa; p10b=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
ST10="$T/store10"; mkdir -p "$ST10/$pk/app"
toy_cache "$ST10/$pk/app/cache" 7 9; printf 'pass=%s|media=m|cache=c\n' "$p10a" > "$ST10/$pk/app/cache/warm-pass.key"
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$ST10/$pk/app/warm-key"
toy_tree "$T/sW"; WB="$T/sW/build"; WT="$WB/tests"; mkdir -p "$WB/generated"
printf '#define JAH_STARTUP_PASS_CODE_ID "%s"\n' "$p10b" > "$WB/generated/jah_startup_pass_id.h"
rm -rf "$WT/.shadercache"; toy_cache "$WT/.shadercache" 1 2; printf 'pass=%s|media=m|cache=c\n' "$p10a" > "$WT/.shadercache/warm-pass.key"
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$WT/.shadercache.warm-key"
iH=$(stat -c %i "$WT/.shadercache/hlms.1.bin"); iC=$(stat -c %i "$WT/.shadercache/compute.bin")
rm -f "$T/k10.held" "$T/s-booted"; : > "$T/s10.pass"
( exec 8>"$ST10/$pk/.key.lock"; flock 8; : > "$T/k10.held"; sleep ${S10_HOLD:-15} ) & kp=$!
for _ in $(seq 50); do [ -e "$T/k10.held" ] && break; sleep 0.1; done
out=$(FAKE_PASS_ID=$p10b FAKE_PASS_LOG="$T/s10.pass" JAH_WARM_STORE="$ST10" SX "$WB" --only app-off --budget 20 2>&1); rc=$?
# (the holder is KILLED once the call is back, after asserting it still held the lock all through - its margin a 15-s sleep;
# waiting its sleep out cost the row 4 s of its 10-s budget)
kill -0 $kp 2>/dev/null; held=$?; kill $kp 2>/dev/null; wait $kp 2>/dev/null
[ $rc = 0 ] && [ $held = 0 ] && ! echo "$out" | grep -q "TAKEN" && ! echo "$out" | grep -q "being warmed by another tree" &&
    [ "$(grep -c BOOTED "$T/s-booted" 2>/dev/null)" = 1 ] && [ "$(cat "$T/s10.pass")" = ran ] &&
    [ "$(stat -c %i "$WT/.shadercache/hlms.1.bin")" = "$iH" ] && [ "$(stat -c %i "$WT/.shadercache/compute.bin")" = "$iC" ] &&
    ! grep -q '^shaders 9' "$WT/.shadercache/cache-manifest.txt" && grep -q "^pass=$p10b|" "$WT/.shadercache/warm-pass.key" &&
    ok "S10: a cache warm at the key with only a stale pass record - no take, no key lock, ONE refresh boot on the tree's own union (the same inodes)" ||
    no "S10 the warm tree and the store: rc $rc still-held $held booted [$(cat "$T/s-booted" 2>/dev/null | tr '\n' ' ')] pass [$(tr '\n' ' ' < "$T/s10.pass")] '$(echo "$out" | grep -iE "taken|lock|warmed by|cold at|warm at" | head -4 | tr '\n' '|')'"
# S11 THE STORE TAKES A REFRESHED RECORD, NEVER A POORER UNION (WARM-SHARE-1 stage 3's line, the lead's ruling 2026-11-22): a tree
# warm at the key the store already holds (the same stamp) whose startup-pass record differs - a POORER union (fewer shaders than
# the store's) refreshes ONLY the store's record (a new file: the store's caches and every taker's links untouched); a union at
# least as rich replaces the store's copy whole. (red before: the store kept its first publisher's record, each taker paying one
# refresh boot)
ST11="$T/store11"; mkdir -p "$ST11"
s11() {   # <dir> <shaders> <record id>: a tree warm at $pk, its app cache holding <shaders>, its record <id>
    toy_tree "$1"; local t="$1/build/tests"; rm -rf "$t/.shadercache"; toy_cache "$t/.shadercache" 1 "$2"
    printf 'pass=%s|media=m|cache=c\n' "$3" > "$t/.shadercache/warm-pass.key"
    printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$t/.shadercache.warm-key"; printf '%s\n%s\n' "$pk" "k1 k2 " > "$t/.shadercache-engine.warm-key"
    toy_cache "$t/.shadercache-engine" 1 2
    printf '%s\n%s\n' "$pk" s-app > "$t/.shadercache.driver-key"; printf '%s\n%s\n' "$pk" s-app > "$t/.shadercache-engine.driver-key"
}
s11 "$T/s11P" 9 p11a; JAH_WARM_STORE="$ST11" SX "$T/s11P/build" > /dev/null 2>&1
iS=$(stat -c %i "$ST11/$pk/app/cache/hlms.1.bin")
s11 "$T/s11Q" 2 p11b; o1=$(JAH_WARM_STORE="$ST11" SX "$T/s11Q/build" 2>&1)
r1="$(sed -n 's/^shaders //p' "$ST11/$pk/app/cache/cache-manifest.txt") $(cut -d'|' -f1 "$ST11/$pk/app/cache/warm-pass.key") $([ "$(stat -c %i "$ST11/$pk/app/cache/hlms.1.bin")" = "$iS" ] && echo same || echo moved)"
s11 "$T/s11R" 12 p11c; o2=$(JAH_WARM_STORE="$ST11" SX "$T/s11R/build" 2>&1)
r2="$(sed -n 's/^shaders //p' "$ST11/$pk/app/cache/cache-manifest.txt") $(cut -d'|' -f1 "$ST11/$pk/app/cache/warm-pass.key")"
[ "$r1" = "9 pass=p11b same" ] && echo "$o1" | grep -q "PUBLISHED to the box's warm store at $pk.*app-record" && [ "$r2" = "12 pass=p11c" ] &&
    ok "S11: a poorer tree's refreshed record reaches the store alone (9 shaders kept, the record p11b, the store's files untouched); a richer tree replaces the store's copy (12, p11c)" ||
    no "S11 the store's record: poorer [$r1] '$(echo "$o1" | grep PUBLISHED)' richer [$r2] '$(echo "$o2" | grep PUBLISHED)'"
fi
# THE LAST LINE IS THE VERDICT (CODING_RULES § 4: `<name>: PASSED` or `<name>: FAILED (<n>)`, a non-zero exit on failure)
echo "tooling.warm_app_cache ($PART): $([ $fail = 0 ] && echo PASSED || echo "FAILED ($fail)")"
exit $(( fail > 0 ))
