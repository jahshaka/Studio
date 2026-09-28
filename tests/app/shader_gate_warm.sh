#!/usr/bin/env bash
#
# app.shader_gate_warm — THE STARTUP SHADER GATE KNOWS A WARM CACHE (TEST-TIER-1; the diagnosis
# is spikes/test-tier-1/coldgate/). Two boots in one home:
#
#   boot 1, --clear-shader-cache: a COLD cache — the gate compiles programs and runs its GI
#           warm-up (the ~1 GB Epic lighting arm, ~1 s);
#   boot 2, the same home: a WARM cache — the gate compiles ZERO programs and SKIPS the warm-up.
#
# ...in three arms (TESTING-DEBTS-1 T7): the document's tier, a Low test process
# (JAHSHAKA_TEST_TIER=low) and a machine with no ray query (JAHSHAKA_NO_RAY_QUERY=1).
#
# Until this lane the second boot after every cache rebuild compiled one Pbs colour-pass program
# (the lighting arm ready before the screen-probe gather: a state only a warm cache reaches inside
# the gate's first frames) and paid the whole warm-up again; the gate now draws that state on the
# cold boot. A red here is a program the cold warm-up does not draw — find it with
# JAHSHAKA_HLMS_DEBUG_DIR on both boots (the source that only boot 2 generated).
#
# usage: shader_gate_warm.sh <jahshaka-binary> <script.js>   (cwd = the row's run dir, HOME = its home)
set -u
BIN="$1"; SCRIPT="$2"
fail=0
ok()  { echo "shader_gate_warm: ok — $1"; }
bad() { echo "shader_gate_warm: FAIL — $1"; fail=1; }

line() { grep -o 'startup shader build: [0-9]* shaders ([0-9]* compiled, [0-9]* from cache) in [0-9]* ms' "$1" | tail -1; }
gi()   { grep -o "startup shader build: the GI compute set's warm-up took [0-9]* frames, [0-9]* ms.*" "$1" | head -1; }
compiled() { line "$1" | sed -E 's/.*\(([0-9]+) compiled.*/\1/'; }

# pair <arm> [VAR=value ...] — the two boots of one arm, each boot under the arm's environment.
# THREE ARMS (TESTING-DEBTS-1 T7): the document's tier (Epic); JAHSHAKA_TEST_TIER=low (the gate warms
# the tier a Low test process is born with); JAHSHAKA_NO_RAY_QUERY=1 (rays off: the machine without
# ray query — the Photon passes' ray properties absent). Each arm's first boot clears the cache.
pair() {
    local arm="$1"; shift
    env "$@" "$BIN" --clear-shader-cache --script "$SCRIPT" > "$arm-boot1.log" 2>&1 || {
        echo "$arm boot 1 exited $?"; tail -40 "$arm-boot1.log"; fail=1; return; }
    env "$@" "$BIN" --script "$SCRIPT" > "$arm-boot2.log" 2>&1 || {
        echo "$arm boot 2 exited $?"; tail -40 "$arm-boot2.log"; fail=1; return; }
    echo "$arm boot 1: $(line "$arm-boot1.log") | $(gi "$arm-boot1.log")"
    echo "$arm boot 2: $(line "$arm-boot2.log") | $(gi "$arm-boot2.log")"
    local c1 c2
    c1=$(compiled "$arm-boot1.log"); c2=$(compiled "$arm-boot2.log")
    [ -n "$c1" ] && [ "$c1" -gt 0 ] && ok "$arm: the cold boot compiled $c1 program(s)" || bad "$arm: the cold boot compiled '${c1}' programs (a cleared cache must compile)"
    gi "$arm-boot1.log" | grep -q 'warm cache: skipped' && bad "$arm: the cold boot SKIPPED the GI warm-up" || ok "$arm: the cold boot ran the GI warm-up"
    [ "$c2" = 0 ] && ok "$arm: the warm boot compiled 0 programs" || {
        bad "$arm: the warm boot compiled '${c2}' program(s):"; grep 'compiled successfully' "$arm-boot2.log" | head -10; }
    gi "$arm-boot2.log" | grep -q 'warm cache: skipped' && ok "$arm: the warm boot skipped the GI warm-up" || bad "$arm: the warm boot ran the GI warm-up: $(gi "$arm-boot2.log")"
}

pair epic
pair low JAHSHAKA_TEST_TIER=low
pair norays JAHSHAKA_NO_RAY_QUERY=1
exit $fail
