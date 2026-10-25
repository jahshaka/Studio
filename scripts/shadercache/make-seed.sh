#!/bin/bash
# make-seed.sh <cache-dir> <seed-dir> — THE SHIPPED MICROCODE SEED, made from a warm shader cache (COLD-BOOT-1 S1).
#
# WHAT A SEED IS. A build carries <build>/bin/shadercache-seed (EngineHost::shaderCacheSeedDirectory; the variable
# JAHSHAKA_SHADER_SEED names another directory, or `none`): the MICROCODE layer a warm run of THE SAME BUILD saved —
# the one layer whose key names only its producer (the fork commit, glslang, the build type, the render system) and
# nothing of the GPU or the driver. The engine reads it as a read-only fallback when a launch's own cache holds no
# microcode layer at this build's key (irisgl engine/src/OgreShaderCache.cpp, readSeed): a first launch serves its
# SPIR-V from it and glslang runs only for a miss. A seed made at another key is not read at all (a miss, never
# stale); the engine never writes, locks or wipes it.
#
# WHAT THIS DOES. <cache-dir> is that warm run's shader cache directory (<data-root>/shadercache, or a build's shared
# tests/.shadercache). Its microcode.cache is copied and a manifest of the header line and the microcode line alone
# is written beside it (the engine reads that one line; the other layers are device-keyed and never shipped). The
# seed REPLACES <seed-dir> whole (a sibling directory, then two renames), so a reader never meets half a seed.
# The size is checked here; the checksum (MurmurHash3, the manifest's third column) is checked by the engine at load.
#
# Exit: 0 made, 1 the cache holds no usable microcode layer, 2 the copy failed, 64 usage.
set -u
[ $# -eq 2 ] || { echo "usage: make-seed.sh <cache-dir> <seed-dir>" >&2; exit 64; }
SRC="${1%/}"; DST="${2%/}"
MAN="$SRC/cache-manifest.txt"
[ -f "$MAN" ] || { echo "make-seed: $SRC has no cache-manifest.txt" >&2; exit 1; }
HEADER=$(grep -m1 '^jahshaka-shader-cache ' "$MAN")
LINE=$(grep -m1 '^file microcode\.cache ' "$MAN")
[ -n "$HEADER" ] || { echo "make-seed: $MAN has no header line" >&2; exit 1; }
[ -n "$LINE" ] || { echo "make-seed: $MAN names no microcode.cache (the run compiled nothing, or never saved)" >&2; exit 1; }
BYTES=$(echo "$LINE" | awk '{ print $3 }')
KEY=$(echo "$LINE" | awk '{ print $5 }')
[ -f "$SRC/microcode.cache" ] || { echo "make-seed: $SRC/microcode.cache is missing" >&2; exit 1; }
HAVE=$(stat -c %s "$SRC/microcode.cache")
[ "$HAVE" = "$BYTES" ] || { echo "make-seed: $SRC/microcode.cache is $HAVE bytes, its manifest says $BYTES" >&2; exit 1; }

PARENT=$(dirname "$DST")
mkdir -p "$PARENT" || exit 2
NEW="$DST.new.$$"; OLD="$DST.old.$$"
rm -rf "$NEW" "$OLD"
mkdir "$NEW" || exit 2
cp "$SRC/microcode.cache" "$NEW/microcode.cache" || { rm -rf "$NEW"; exit 2; }
[ "$(stat -c %s "$NEW/microcode.cache")" = "$BYTES" ] || { echo "make-seed: the copy is short" >&2; rm -rf "$NEW"; exit 2; }
printf '%s\n%s\n' "$HEADER" "$LINE" > "$NEW/cache-manifest.txt" || { rm -rf "$NEW"; exit 2; }
if [ -e "$DST" ]; then mv "$DST" "$OLD" || { rm -rf "$NEW"; exit 2; }; fi
mv "$NEW" "$DST" || { [ -e "$OLD" ] && mv "$OLD" "$DST"; rm -rf "$NEW"; exit 2; }
rm -rf "$OLD"
echo "make-seed: $DST: microcode.cache $BYTES bytes at microcode key $KEY"
