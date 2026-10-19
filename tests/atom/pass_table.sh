#!/usr/bin/env bash
#
# atom.pass_table — the driver (BUGS-1 item 1; the claims are tests/atom/scripts/atom_pass_table.js's).
# Runs the app on the script with JAHSHAKA_HLMS_DEBUG_DIR set (a variable the engine latches at
# boot: hence a row of its own), then reads every dumped pixel shader: its highest ogre_t slot must
# be under the fork's pass-texture table, and HlmsAtom (HLMS_USER0 = type 4, files 4xxxxxxxx) must
# have generated at least one (else the check is vacuous).
#
# usage: pass_table.sh <jahshaka-binary> <script.js> <NUM_BIND_TEXTURES>
# cwd is the row's scratch run dir; HOME is its fresh home.
set -u
BIN="$1"
SCRIPT="$2"
TABLE="$3"
DUMP="$PWD/hlms-dump"
rm -rf "$DUMP"
mkdir -p "$DUMP"

JAHSHAKA_HLMS_DEBUG_DIR="$DUMP/" "$BIN" --script "$SCRIPT" > run.log 2>&1
rc=$?
grep -E '^(ok:|atomStatus:|density:|engineErrors:|PASS )|assert failed|needs [0-9]+ texture|PASS-TABLE' run.log | cut -c1-400 | head -40
if [ "$rc" -ne 0 ]; then
    echo "pass_table: FAIL — the app exited $rc"
    tail -20 run.log | cut -c1-400
    exit 1
fi

atom=0
total=0
over=0
worst=-1
worstFile=""
for f in "$DUMP"/*PixelShader_ps.glsl; do
    [ -e "$f" ] || continue
    hi=$(grep -oE 'ogre_t[0-9]+' "$f" | sed 's/ogre_t//' | sort -n | tail -1)
    [ -n "$hi" ] || continue
    total=$((total + 1))
    case "$(basename "$f")" in 4[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]PixelShader*) atom=$((atom + 1)) ;; esac
    if [ "$hi" -ge "$TABLE" ]; then
        over=$((over + 1))
        echo "pass_table: OVER — $(basename "$f") reaches ogre_t$hi (the table holds $TABLE)"
    fi
    if [ "$hi" -gt "$worst" ]; then
        worst=$hi
        worstFile=$(basename "$f")
    fi
done
echo "pass_table: $total pixel shaders ($atom HlmsAtom); the highest slot ogre_t$worst ($worstFile); the table holds $TABLE"
if [ "$atom" -eq 0 ]; then
    echo "pass_table: FAIL — no HlmsAtom pixel shader was generated (the check would be vacuous)"
    exit 1
fi
if [ "$over" -ne 0 ]; then
    echo "pass_table: FAIL — $over pixel shader(s) past the table"
    exit 1
fi
echo "pass_table: PASS"
