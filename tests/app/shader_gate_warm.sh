#!/usr/bin/env bash
#
# app.shader_gate_warm — THE STARTUP SHADER GATE KNOWS A WARM CACHE (TEST-TIER-1; the diagnosis
# is spikes/test-tier-1/coldgate/). Two boots in one home:
#
#   boot 1, --clear-shader-cache: a COLD cache — the gate compiles programs and runs its GI
#           warm-up (the ~1 GB Epic lighting arm, ~1 s);
#   boot 2, the same home: a WARM cache — the gate compiles ZERO programs and SKIPS the warm-up.
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

"$BIN" --clear-shader-cache --script "$SCRIPT" > boot1.log 2>&1 || { echo "boot 1 exited $?"; tail -40 boot1.log; exit 1; }
"$BIN" --script "$SCRIPT" > boot2.log 2>&1 || { echo "boot 2 exited $?"; tail -40 boot2.log; exit 1; }

line() { grep -o 'startup shader build: [0-9]* shaders ([0-9]* compiled, [0-9]* from cache) in [0-9]* ms' "$1" | tail -1; }
gi()   { grep -o "startup shader build: the GI compute set's warm-up took [0-9]* frames, [0-9]* ms.*" "$1" | head -1; }
compiled() { line "$1" | sed -E 's/.*\(([0-9]+) compiled.*/\1/'; }

echo "boot 1: $(line boot1.log) | $(gi boot1.log)"
echo "boot 2: $(line boot2.log) | $(gi boot2.log)"
c1=$(compiled boot1.log); c2=$(compiled boot2.log)
[ -n "$c1" ] && [ "$c1" -gt 0 ] && ok "the cold boot compiled $c1 program(s)" || bad "the cold boot compiled '${c1}' programs (a cleared cache must compile)"
gi boot1.log | grep -q 'warm cache: skipped' && bad "the cold boot SKIPPED the GI warm-up" || ok "the cold boot ran the GI warm-up"
[ "$c2" = 0 ] && ok "the warm boot compiled 0 programs" || {
    bad "the warm boot compiled '${c2}' program(s):"; grep 'compiled successfully' boot2.log | head -10; }
gi boot2.log | grep -q 'warm cache: skipped' && ok "the warm boot skipped the GI warm-up" || bad "the warm boot ran the GI warm-up: $(gi boot2.log)"
exit $fail
