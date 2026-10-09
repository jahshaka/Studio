#!/usr/bin/env bash
#
# app.test_needs_boot — the declaration reaches the process (TEST-NEEDS-1). Three boots, one per
# declaration, each under its own JAHSHAKA_TEST_TIER / JAHSHAKA_TEST_NEEDS (the row's own
# declaration is the heaviest of them, for its tokens): test_needs_boot.js asserts the features;
# this harness asserts app.testTier() names the declaration. Then the REFUSALS: a list the app
# cannot honour exits 2 before a window exists.
#
# usage: test_needs_boot.sh <jahshaka-binary> <script.js>     (cwd: the scratch run dir)
set -u
BIN="$1"; SCRIPT="$2"
fail=0
ok()  { echo "ok: $*"; }
bad() { echo "FAIL: $*"; fail=1; }

arm() {   # arm <name> <tier> <needs> <expected testTier json>
    local name="$1" tier="$2" needs="$3" want="$4"
    JAHSHAKA_TEST_TIER="$tier" JAHSHAKA_TEST_NEEDS="$needs" "$BIN" --script "$SCRIPT" > "$name.log" 2>&1
    local rc=$?
    grep -E '^(ok|TESTTIER|new scene|opened scene)' "$name.log" | sed "s/^/  [$name] /"
    [ "$rc" -eq 0 ] && ok "$name: the script passed" || { bad "$name: exited $rc"; tail -30 "$name.log"; }
    grep -qF "TESTTIER $want" "$name.log" && ok "$name: app.testTier() is $want" \
        || bad "$name: app.testTier() is not $want ($(grep TESTTIER "$name.log"))"
}
arm high_photon high "photon" '{"needs":["photon"],"tier":"high"}'
arm low_none low "none" '{"needs":[],"tier":"low"}'
arm epic_photon_bloom epic "photon bloom" '{"needs":["photon","bloom"],"tier":"epic"}'

refuse() {   # refuse <name> <env…>
    local name="$1"; shift
    env "$@" "$BIN" --script "$SCRIPT" > "$name.log" 2>&1
    local rc=$?
    [ "$rc" -eq 2 ] && grep -q 'JAHSHAKA_TEST_NEEDS' "$name.log" && ok "$name: refused (exit 2: $(grep -m1 JAHSHAKA_TEST_NEEDS "$name.log"))" \
        || { bad "$name: exit $rc, not refused"; tail -5 "$name.log"; }
}
refuse unknown_word JAHSHAKA_TEST_TIER=low "JAHSHAKA_TEST_NEEDS=shadows"
refuse none_plus JAHSHAKA_TEST_TIER=low "JAHSHAKA_TEST_NEEDS=none photon"
refuse no_tier -u JAHSHAKA_TEST_TIER "JAHSHAKA_TEST_NEEDS=photon"
exit $fail
