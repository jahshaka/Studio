#!/bin/bash
# tooling.warm_app_cache (URGENT #52) - scripts/warm-app-cache.sh's NO-OP PATH: warm at its key it answers at once and
# boots nothing; cold (no stamp, a moved key, a replaced cache) it says so - for each of its FOUR boots, the app rows' and
# the engine rows' caches with the driver's cache off and each exe's driver cache after it (PHOTON-7 11-2; the two-boot
# law #100/#102b; one boot a call, the card's wait bounded per boot, #106). On a scratch tests dir (--tests-dir), never
# the tree's shared cache; the boot itself is the script's own (not a test: once, uncapped, when the key moved).
# FIVE ROWS, ONE SCRIPT (TEST-STREAMLINE-1: the whole ran 35.8 s quiet at 8a8503057 against a CPU row's 10-s budget and a
# 6.48-s price line - split by proof): <part> = boots (tooling.warm_app_cache: the four boots' answers, the stamps, the
# fake boots, the budget and the bounded wait), cold (tooling.warm_cold_rows: the cold-on runner's rows - one at a time, the
# crashed prime, the seeds), coldcall (tooling.warm_cold_calls: a call's own bounds - the held card, the deadline, the logs)
# keys (tooling.warm_key: the key's terms, the top-up, the path-free stamps) or inherit (tooling.warm_inherit: a fresh tree
# takes its parent's caches at an equal key); none = all five, a hand run.
# usage: warm_app_cache.sh <source-dir> <build-dir> [boots|cold|coldcall|keys|inherit]
set -u
SRC=${1:?source}; BUILD=${2:?build}; PART=${3:-all}
case "$PART" in all|boots|cold|coldcall|keys|inherit) ;; *) echo "warm_app_cache.sh: <part> is boots, cold, coldcall, keys or inherit, not $PART" >&2; exit 64 ;; esac
want() { [ "$PART" = all ] || [ "$PART" = "$1" ]; }
W="$SRC/scripts/warm-app-cache.sh"
T=${KEEP_T:-$(mktemp -d)}; [ -n "${KEEP_T:-}" ] || trap 'rm -rf "$T"' EXIT   # (KEEP_T=<dir>: a hand debug run keeps its toys)
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
# THE TOP-UP's STAMP (TEST-STREAMLINE-1 1a (c)): the full engine build id an -off boot last ran at
EID=$(sed -n 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\{16\}\).*"$/\1/p' "$BUILD/engine/generated/jahshaka_engine_build_id.h")
echo "$EID" > "$T/.shadercache.engine-id"; echo "$EID" > "$T/.shadercache-engine.engine-id"
fail=0
ok() { echo "  ok: $1"; }
no() { echo "  FAIL: $1"; fail=1; }
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
printf 'jahshaka-shader-cache 2\nsaved 2\nshaders 3\n%s\n' "$files" > "$c/cache-manifest.txt"
if [ -n "${FAKE_RACE:-}" ]; then
    rm -f "$FAKE_RACE/cache-manifest.txt"
    printf 'jahshaka-shader-cache 2\nsaved 9\nshaders 4\n%s\n' "$files" > "$FAKE_RACE/cache-manifest.txt"
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
# A TOP-UP REPLACES THE UNION (the stage-1 audit's finding 4): an app-off boot whose ONLY why is the engine's moved code boots
# its home without the shared hlms layer (fresh_home.sh --drop-hlms), so the promoted cache holds only what the boot used -
# the shared hlms.1.bin of an earlier engine is gone after it (red before: the union kept it). A boot cold for another reason
# keeps the layer.
B2="$T/b2"; mkdir -p "$B2/tests"; touch "$B2/CMakeCache.txt"; toy_cache "$B2/tests/.shadercache" 1 2
printf '%s\n%s\n%s\n' "$key" "k1 k2 " "$SD" > "$B2/tests/.shadercache.warm-key"; echo 0000000000000000 > "$B2/tests/.shadercache.engine-id"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B2/tests" "${F[@]}" --only app-off); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q 'a TOP-UP' && echo "$out" | grep -q -- '--drop-hlms: the home.s hlms layer left out' &&
    ! grep -q '^file hlms\.' "$B2/tests/.shadercache/cache-manifest.txt" && grep -q '^file pipeline.cache' "$B2/tests/.shadercache/cache-manifest.txt" &&
    [ "$(cat "$B2/tests/.shadercache.engine-id")" = "$EID" ] &&
    ok "a TOP-UP boot (the engine's code moved alone) boots without the hlms layer and REPLACES the union: the old hlms entries gone, the pipelines kept" ||
    no "the top-up's union: rc $rc $(echo "$out" | tail -3) / $(tr '\n' ';' < "$B2/tests/.shadercache/cache-manifest.txt")"
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
out=$(JAH_GONE_X=1 JAHSHAKA_SHADER_CACHE_DIR=/x __GL_SHADER_DISK_CACHE=0 DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q '^warm_cold_rows: x.cold: PRIMED - its run [0-9]* s (exit 3; .*its own red, printed: FAIL: its own law' &&
    [ "$(sed -n 1p "$CS/x.cold.driver-key" 2>/dev/null)" = "$key" ] &&
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
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ ! -e "$T/cold-ran" ] && [ -e "$CS/y.cold.driver-key" ] && [ -e "$CS/z.cold.driver-key" ] &&
    [ "$(cut -d' ' -f1,2 "$T/order" | tr '\n' '|')" = "start y|end y|start z|end z|" ] &&
    ok "...run ONE AT A TIME (y ended before z started), the primed x.cold not run again, each stamped" ||
    no "one at a time: '$out' rc $rc order: $(tr '\n' '|' < "$T/order" 2>&1)"
# A PRIME THAT CRASHED IS NEVER STAMPED (URGENT #155: .cold's prime died out of GPU memory and the stamp was written over it)
printf '#!/bin/sh\necho "VkResult = VK_ERROR_OUT_OF_DEVICE_MEMORY"\necho "*** Jahshaka crashed"\nexit 1\n' > "$T/oomfx"; chmod +x "$T/oomfx"
printf 'o.cold\t%s\t\t%s\n' "$T/cwd" "$T/oomfx" >> "$CR"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
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
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 SEED_FAILS=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 1 ] && echo "$out" | grep -q '^warm_cold_rows: s.seed: NOT PRIMED - the seed is not fresh after its run' && [ ! -e "$CS/s.seed.driver-key" ] &&
    ok "a seed row whose run left its seed stale: NOT PRIMED, no stamp, exit 1" || no "a seed left stale: '$out' rc $rc"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q '^warm_cold_rows: s.seed: PRIMED' && [ -e "$CS/s.seed.driver-key" ] && [ -f "$T/s/fresh" ] &&
    ok "...its run made it fresh: PRIMED and stamped" || no "a seed made: '$out' rc $rc"
rm -f "$T/s/fresh"
out=$("$W" "$BUILD" --check --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q '1 of 4 to prime: s.seed (its startup seed is stale (a new Studio build))' &&
    ok "a seed row stamped at the key whose --seed-check says stale (a new Studio build): cold-on cold for it alone, exit 3" ||
    no "a stale seed: '$out' rc $rc"
out=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on); rc=$?
[ $rc = 0 ] && [ -f "$T/s/fresh" ] && echo "$out" | grep -q 'warm_cold_rows: s.seed: PRIMED' && ! echo "$out" | grep -q 'warm_cold_rows: [xyz].cold: PRIMED' &&
    ok "...and the warm-up re-makes it before any row (the other rows not run again)" || no "the stale seed re-made: '$out' rc $rc"
rm -f "$T/s/fresh"; out=$(cd "$T/s" && "$T/seedfx" --seed); rc=$?
[ $rc = 1 ] && [ ! -f "$T/s/fresh" ] && ok "a stale seed met OUTSIDE the warm-up (no JAH_COLD_PRIME) fails at once, nothing seeded (the row never re-seeds under its cap)" ||
    no "the seed outside the warm-up: '$out' rc $rc"
fi
if want coldcall; then
# THE CALL's OWN CASES (tooling.warm_cold_calls) start from every listed cold row primed - alone, its three rows listed first
[ "$PART" = coldcall ] && printf 'y.cold\t%s\t\t%s\nz.cold\t%s\t\t%s\n' "$T/y" "$T/seqfx" "$T/z" "$T/seqfx" >> "$CR"
DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on > /dev/null 2>&1
# EACH ROW ITS OWN HOLD: the card held past a row's wait - that row NOT RUN, exit 75, nothing stamped, the next call primes it
rm -f "$CS/z.cold.driver-key"
exec 8>"$T/gpu.lock"; flock 8
out=$(env -u JAH_GPU_LOCK_HELD JAH_COLD_MIN_LEFT=5 DISPLAY=:fake JAH_GPU_LOCK="$T/gpu.lock" "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on --budget 22); rc=$?   # (the least budget whose row still starts: its card wait ~2 s, the law the same)
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
rm -f "$CS/x.cold.driver-key"; out1=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on)
rm -f "$CS/x.cold.driver-key"; out2=$(DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$BUILD" --tests-dir "$B/tests" "${C[@]}" --only cold-on)
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
    printf 'CMAKE_BUILD_TYPE:STRING=Debug\n' > "$b/CMakeCache.txt"
    printf 'h' > "$b/bin/media/Hlms/a.any"; printf 'p' > "$b/bin/media/Photon/b.any"; printf 'x' > "$b/bin/media/x/c.png"
    printf '#!/bin/sh\nexit 0\n' > "$b/tests/row/fx"; chmod +x "$b/tests/row/fx"
    printf 'p.cold\t%s\t-JAH_X\tenv\t__GL_SHADER_DISK_CACHE_PATH=%s\t%s\t--in\t%s\n' "$b/tests/row" "$b/tests/row/dc" "$b/tests/row/fx" \
        "$b/tests/row/in.js" > "$b/tests/rows.txt"
}
toy_tree "$T/tA"; toy_tree "$T/tB"; touch -d '2001-01-01' "$T/tB/build/bin/media/x/c.png"
for t in tA tB; do
    DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$T/$t/build" --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/$t/build/tests/rows.txt" \
        --only cold-on > "$T/$t.out" 2>&1
done
sa=$(sed -n 2,3p "$T/tA/build/tests/.cold-rows/p.cold.driver-key" 2>/dev/null); sb=$(sed -n 2,3p "$T/tB/build/tests/.cold-rows/p.cold.driver-key" 2>/dev/null)
[ -n "$sa" ] && [ "$sa" = "$sb" ] && ok "two trees at two paths, one content: their cold stamps equal (the row's line with <build>, the media by content, a file's time apart)" ||
    no "path-free stamps: [$sa] vs [$sb] ($(tail -3 "$T/tA.out"))"
out=$("$W" "$T/tB/build" --check --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/tB/build/tests/rows.txt" --only cold-on); rc=$?
[ $rc = 0 ] && ok "...and a staged file touched (its time alone): cold-on warm" || no "a touched media file: '$out' rc $rc"
printf 'y' > "$T/tB/build/bin/media/x/c.png"
out=$("$W" "$T/tB/build" --check --app "$T/fake-app" --lab "$T/fake-app" --cold-rows "$T/tB/build/tests/rows.txt" --only cold-on); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'p.cold (the staged media rebuilt since)' && ok "...a staged file's content moved: cold-on cold, the row named" ||
    no "a media content change: '$out' rc $rc"
# THE NARROWED KEY AND THE TOP-UP (TEST-STREAMLINE-1 1a; #126's nonce case on the narrowed key, CPU half): on a toy tree
# warmed by hand at its key - ONLY the engine's full build id moved: the key holds, the two -off boots read a TOP-UP, the -on
# boots and the cold rows warm; a byte of the staged Hlms media, a ray-tier SPIR-V header or the shader cache's code id moved:
# the KEY moves, every boot cold (the shader that changed is the one that runs - the card half is block A)
toy_tree "$T/tK"; KB="$T/tK/build"; KT="$KB/tests"; mkdir -p "$KB/engine/generated/rayquery"; printf 'spv1' > "$KB/engine/generated/rayquery/rq_x_spv.h"
kk=$("$W" "$KB" --key); kid=$(sed -n 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\{16\}\).*"$/\1/p' "$KB/engine/generated/jahshaka_engine_build_id.h")
KX=(--app "$T/app" --lab "$T/lab" --cold-rows "$KB/tests/rows.txt" --tests-dir "$KT")
toy_cache "$KT/.shadercache" 1 2; toy_cache "$KT/.shadercache-engine" 1 2
printf '%s\n%s\n%s\n' "$kk" "k1 k2 " "sets none" > "$KT/.shadercache.warm-key"; printf '%s\n%s\n' "$kk" "k1 k2 " > "$KT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$kk" app > "$KT/.shadercache.driver-key"; printf '%s\n%s\n' "$kk" lab > "$KT/.shadercache-engine.driver-key"
echo "$kid" > "$KT/.shadercache.engine-id"; echo "$kid" > "$KT/.shadercache-engine.engine-id"
DISPLAY=:fake JAH_GPU_LOCK_HELD=1 "$W" "$KB" "${KX[@]}" --only cold-on > /dev/null 2>&1
out=$("$W" "$KB" --check "${KX[@]}"); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $kk" ] && ok "the narrowed key's toy tree warm at $kk (every boot)" || no "the toy tree warm: '$out' rc $rc"
sed -i 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\)/#define JAHSHAKA_ENGINE_BUILD_ID "f\1/' "$KB/engine/generated/jahshaka_engine_build_id.h"
out=$("$W" "$KB" --check "${KX[@]}"); rc=$?
[ $rc = 3 ] && [ "$("$W" "$KB" --key)" = "$kk" ] &&
    echo "$out" | grep -q "the app rows' cache (app-off): the engine's code moved since .*a TOP-UP" &&
    echo "$out" | grep -q "the engine rows' cache (engine-off): the engine's code moved since .*a TOP-UP" &&
    ! echo "$out" | grep -qE '\((app-on|engine-on|cold-on)\)' &&
    ok "an engine edit alone (the full build id): the KEY holds, the two -off boots a TOP-UP, the -on boots and the cold rows warm" ||
    no "the engine edit alone: '$out' rc $rc"
out=$("$W" "$KB" --check "${KX[@]}" --only app-on); rc=$?
[ $rc = 0 ] && ok "...and an -on boot is not held behind its -off boot's top-up" || no "app-on behind a top-up: '$out' rc $rc"
nonce() {   # <what> <file> <bytes>: the key moves, every boot cold
    cp "$2" "$T/nonce.bak"; printf '%s' "$3" >> "$2"
    local o r; o=$("$W" "$KB" --check "${KX[@]}"); r=$?
    [ $r = 3 ] && [ "$("$W" "$KB" --key)" != "$kk" ] && echo "$o" | grep -q '(app-on): the key moved' &&
        echo "$o" | grep -q '(cold-on): 1 of 1 to prime: p.cold (the key moved' &&
        ok "a NONCE in $1: the key moves, every boot cold" || no "the nonce in $1: '$o' rc $r"
    cp "$T/nonce.bak" "$2"
}
nonce "the staged Hlms media" "$KB/bin/media/Hlms/a.any" "// nonce"
nonce "a ray-tier SPIR-V header" "$KB/engine/generated/rayquery/rq_x_spv.h" "1"
cp "$KB/engine/generated/jahshaka_engine_build_id.h" "$T/id.bak"
sed -i 's/^#define JAHSHAKA_SHADER_CACHE_CODE_ID "\([0-9a-f]\)/#define JAHSHAKA_SHADER_CACHE_CODE_ID "e\1/' "$KB/engine/generated/jahshaka_engine_build_id.h"
o=$("$W" "$KB" --check "${KX[@]}"); r=$?
[ $r = 3 ] && [ "$("$W" "$KB" --key)" != "$kk" ] && ok "the shader cache's own code moved (JAHSHAKA_SHADER_CACHE_CODE_ID): the key moves" || no "the code id: '$o' rc $r"
grep -v SHADER_CACHE_CODE_ID "$T/id.bak" > "$KB/engine/generated/jahshaka_engine_build_id.h"
"$W" "$KB" --key > /dev/null 2>&1; r=$?
[ $r = 2 ] && ok "a build id header without the code id (an unbuilt or older tree): no key, exit 2 - never a key without the cache's code" || no "no code id: rc $r"
fi
if want inherit; then
# THE INHERITANCE (TEST-STREAMLINE-1 1a; the reader's I1-I7): a parent toy tree warm at its key (both shared caches stamped,
# one generation each; a cold row primed, its private driver cache and its startup seed beside it) and a child toy tree of
# the same content at another path. --inherit: the child reads warm with no boot (I1); its files are the parent's bytes and a
# child promotion (a rename over its cache) leaves the parent's untouched (I2); a child whose key differs takes nothing (I3);
# a TORN parent cache is not taken (I4, #117); the parent's cache lock held - not taken, no torn copy (I5); the cold row's
# stamp, its driver cache and its seed taken (I6/I7, the path-free stamps of 1a-1)
[ "$PART" = inherit ] && toy_tree() {   # (the keys part's helper; this part may run alone)
    local b="$1/build"
    mkdir -p "$b/engine/generated" "$b/bin/media/Hlms" "$b/bin/media/Photon" "$b/bin/media/x" "$b/tests/row" "$b/tests/.shadercache"
    cp "$BUILD/engine/generated/jahshaka_engine_build_id.h" "$b/engine/generated/"
    printf 'CMAKE_BUILD_TYPE:STRING=Debug\n' > "$b/CMakeCache.txt"
    printf 'h' > "$b/bin/media/Hlms/a.any"; printf 'p' > "$b/bin/media/Photon/b.any"; printf 'x' > "$b/bin/media/x/c.png"
    printf '#!/bin/sh\nexit 0\n' > "$b/tests/row/fx"; chmod +x "$b/tests/row/fx"
    printf 'p.cold\t%s\t-JAH_X\tenv\t__GL_SHADER_DISK_CACHE_PATH=%s\t%s\t--in\t%s\n' "$b/tests/row" "$b/tests/row/dc" "$b/tests/row/fx" \
        "$b/tests/row/in.js" > "$b/tests/rows.txt"
}
toy_tree "$T/iP"; toy_tree "$T/iC"; PB="$T/iP/build"; CB="$T/iC/build"; PT="$PB/tests"; CT="$CB/tests"
IX() { "$W" "$1" --app "$T/app" --lab "$T/lab" --cold-rows "$1/tests/rows.txt" "${@:2}"; }
pk=$(IX "$PB" --key); pid=$(sed -n 's/^#define JAHSHAKA_ENGINE_BUILD_ID "\([0-9a-f]\{16\}\).*"$/\1/p' "$PB/engine/generated/jahshaka_engine_build_id.h")
rm -rf "$PT/.shadercache"; toy_cache "$PT/.shadercache" 1 2; toy_cache "$PT/.shadercache-engine" 1 2
printf '%s\n%s\n%s\n' "$pk" "k1 k2 " "sets none" > "$PT/.shadercache.warm-key"; printf '%s\n%s\n' "$pk" "k1 k2 " > "$PT/.shadercache-engine.warm-key"
printf '%s\n%s\n' "$pk" app > "$PT/.shadercache.driver-key"; printf '%s\n%s\n' "$pk" lab > "$PT/.shadercache-engine.driver-key"
echo "$pid" > "$PT/.shadercache.engine-id"; echo "$pid" > "$PT/.shadercache-engine.engine-id"
DISPLAY=:fake JAH_GPU_LOCK_HELD=1 IX "$PB" --only cold-on > /dev/null 2>&1
mkdir -p "$PT/row/dc" "$PT/row/seed-x"; echo drv > "$PT/row/dc/GLCache.bin"; echo k > "$PT/row/seed-x/tree-key.txt"
out=$(IX "$PB" --check); rc=$?
[ $rc = 0 ] && ok "the parent toy tree warm at $pk (every boot)" || no "the parent toy tree: '$out' rc $rc"
out=$(IX "$CB" --check); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q 'no warm-up recorded' && ok "...the child, the same content at another path: cold before" || no "the child before: '$out' rc $rc"
# I5 first: the parent's app cache lock held - that cache is not taken (no torn copy), the rest is
exec 7>"$PT/.shadercache.lock"; flock 7
out=$(JAH_INHERIT_LOCK_WAIT=1 IX "$CB" --inherit "$PB"); rc=$?
exec 7>&-
[ $rc = 0 ] && echo "$out" | grep -q "the parent's .shadercache not taken (its lock, or its manifest)" && [ ! -e "$CT/.shadercache/cache-manifest.txt" ] &&
    [ -f "$CT/.shadercache-engine/cache-manifest.txt" ] && echo "$out" | grep -q '1 cold row(s)' &&
    ok "I5: the parent's cache lock held - that cache NOT taken (no torn copy), the engine's and the cold row taken" ||
    no "I5 the held lock: '$out' rc $rc"
out=$(IX "$CB" --inherit "$PB"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "inherited at $pk from $PB: .shadercache (" && ! echo "$out" | grep -q 'cold row' &&
    echo "$out" | grep -q 'still to warm here: nothing' && ok "I1: --inherit again - the app cache taken now, the primed cold row not again, nothing left to warm" ||
    no "I1 inherit: '$out' rc $rc"
out=$(IX "$CB" --check); rc=$?
[ $rc = 0 ] && [ "$out" = "warm at $pk" ] && ok "...the child reads warm with no boot (every boot, cold-on included)" || no "the child after: '$out' rc $rc"
cmp -s "$PT/.shadercache/hlms.1.bin" "$CT/.shadercache/hlms.1.bin" && [ -f "$CT/row/dc/GLCache.bin" ] && [ -f "$CT/row/seed-x/tree-key.txt" ] &&
    [ "$(sed -n 2,3p "$CT/.cold-rows/p.cold.driver-key")" = "$(sed -n 2,3p "$PT/.cold-rows/p.cold.driver-key")" ] &&
    ok "I6/I7: the cache's bytes, the cold row's private driver cache and its seed taken; its stamp the parent's, paths aside" ||
    no "I6/I7: the taken state"
# I2: a child promotion renames a new directory over its cache - the parent's files stay as they were
mkdir -p "$CT/.shadercache.next"; printf 'NEW' > "$CT/.shadercache.next/hlms.1.bin"
rm -rf "$CT/.shadercache"; mv "$CT/.shadercache.next" "$CT/.shadercache"
[ "$(cat "$PT/.shadercache/hlms.1.bin")" = 0123456789 ] && python3 -I "$CK" "$PT/.shadercache" > /dev/null &&
    ok "I2: a child promotion (a rename over its cache) leaves the parent's cache whole" || no "I2: the parent's cache changed"
# I2b (the stage-1 audit's finding 1): the DRIVER writes its cache file in place - a child's driver appending to the taken
# cache must leave the parent's bytes as they were (red when the inheritance hard-linked it)
printf 'grown by the child' >> "$CT/row/dc/GLCache.bin"; printf 'x' >> "$CT/row/seed-x/tree-key.txt"
[ "$(cat "$PT/row/dc/GLCache.bin")" = drv ] && [ "$(cat "$PT/row/seed-x/tree-key.txt")" = k ] &&
    ok "I2b: a child's driver writing its taken driver cache in place (and its seed's stamp) leaves the parent's files as they were" ||
    no "I2b: the parent's driver cache or seed was written through the child ($(cat "$PT/row/dc/GLCache.bin"))"
# I3: a child whose key differs (a staged Hlms byte) takes nothing and stays cold
toy_tree "$T/iD"; DB="$T/iD/build"; printf 'H' > "$DB/bin/media/Hlms/a.any"
out=$(IX "$DB" --inherit "$PB"); rc=$?
[ $rc = 3 ] && echo "$out" | grep -q "inherited nothing - the parent's key $pk is not this tree's" && [ ! -e "$DB/tests/.shadercache.warm-key" ] &&
    ok "I3: a child at another key: nothing taken, exit 3" || no "I3 another key: '$out' rc $rc"
# I4: a TORN parent cache (a file of other bytes than its manifest) is not taken
toy_tree "$T/iE"; EB="$T/iE/build"; printf '9876543210' > "$PT/.shadercache/hlms.1.bin.t"; mv "$PT/.shadercache/hlms.1.bin.t" "$PT/.shadercache/hlms.1.bin"
out=$(IX "$EB" --inherit "$PB"); rc=$?
[ $rc = 0 ] && echo "$out" | grep -q "the parent's .shadercache not taken" && [ ! -e "$EB/tests/.shadercache/cache-manifest.txt" ] &&
    ok "I4: a TORN parent cache is not taken (#117)" || no "I4 torn: '$out' rc $rc"
fi
echo "tooling.warm_app_cache ($PART): $([ $fail = 0 ] && echo PASS || echo FAIL)"
exit $fail
