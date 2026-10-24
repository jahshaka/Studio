#!/bin/bash
# scripts/lab/warm.sh — THE SHADER CACHE WARMED FOR A SCENE, BEFORE A ROW BOOTS ON IT (urgent #32, the lead's ruling: a
# row on a model never compiles its scene's shaders inside its 60 s - a cold Bistro cache cost a 60 s run, gi.walk_bistro
# timing out at its first session). Not a test: no 60 s cap, its own --run-timeout.
#
# usage: warm.sh [--data-root DIR] [--size WxH] [--tier low|medium|high|epic] [--camera x,y,z,tx,ty,tz]...
#                [--run-timeout S] <model file | --env NAME>
#        warm.sh --check [--data-root DIR] [--tier T] [--camera x,y,z,tx,ty,tz]... <model file | --env NAME>
#
# THE WARM RUN: jahlab, offscreen, under the card's lock (scripts/gpu-exclusive.sh --run-timeout S, default 900), the scene
# loaded, at every --camera (none: the scene's own) a settle (the Atom cut steady) and 30 frames, then a CLEAN quit -
# the shader cache is saved at the quit (a model is baked first, OFF the card: jahlab --bake-only into the data root - a
# run never bakes, it refuses a model whose bake is not cached, the urgent list's #30). On jahlab's exit 0 it STAMPS the
# data root's cache: one line a scene AND TIER in <data-root>/shadercache/warm-stamps.txt - the scene's key (an
# environment's name; a model's path and size), the TIER (jahlab's default epic when none is given), the CAMERAS it
# warmed at ("scene" when none: the scene's own) and THE TREE'S KEY - the sha256 of the jahlab binary (the engine is
# linked in: a C++ change can add a shader variant), of the staged media tree beside it (every Hlms template and compute
# source jahlab reads) and of the Ogre fork's commit. A tier is its own permutations and a camera its own visible set
# (stage 5's read W2: a Low warm stamped the scene warm for an Epic row, which then compiled inside its 60 s).
# THE CHECK (--check, the same --tier and --camera a row runs at): exit 0 when a stamp for the scene at that tier
# carries this tree's key and every camera asked for, else exit 3 naming the warm command - what a run that loads a
# model does before it boots (walk.sh --bistro, by hand: gi.walk_bistro is archived). After a media edit, a C++ build
# or a pin bump the cache is (partly) cold: the warm step is what is run, never a longer TIMEOUT.
# THE DISPLAY: $DISPLAY when it names a rig (never :0 - refused), else a private Xvfb on a free :60-:99 at 1920x1080x24,
# its pid recorded and killed at the end. The binary: $JAHLAB, else <this tree>/build-linux/bin/jahlab.
set -u
here="$(cd "$(dirname "$0")" && pwd)"
tree="$(cd "$here/../.." && pwd)"
lab="${JAHLAB:-$tree/build-linux/bin/jahlab}"
dataroot="${HOME}/.local/share/jahlab" size=960x540 tier="" timeout=900 env="" file="" check=0
cams=()
usage() { echo "usage: warm.sh [--check] [--data-root DIR] [--size WxH] [--tier T] [--camera x,y,z,tx,ty,tz]... [--run-timeout S] <file | --env NAME>" >&2; exit 64; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --check) check=1; shift ;;
        --data-root) dataroot="${2:?}"; shift 2 ;;
        --size) size="${2:?}"; shift 2 ;;
        --tier) tier="${2:?}"; shift 2 ;;
        --camera) cams+=("${2:?}"); shift 2 ;;
        --run-timeout) timeout="${2:?}"; shift 2 ;;
        --env) env="${2:?}"; shift 2 ;;
        -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
        -*) usage ;;
        *) [ -z "$file" ] || usage; file="$1"; shift ;;
    esac
done
[ -n "$env" ] || [ -n "$file" ] || usage
[ -x "$lab" ] || { echo "warm.sh: no jahlab at $lab (build the tree, or set JAHLAB)" >&2; exit 2; }
if [ -n "$file" ]; then
    [ -f "$file" ] || { echo "warm.sh: no model $file" >&2; exit 2; }
    file="$(cd "$(dirname "$file")" && pwd)/$(basename "$file")"
    scene="model:${file// /%20}:$(stat -c %s "$file")"   # (a stamp's fields hold no space)
else
    scene="env:$env"
fi
# THE TREE'S KEY: what the scene's compiled shaders are a function of
treekey() {
    local bin; bin="$(cd "$(dirname "$lab")" && pwd)"
    local fork; fork="$(git -C "$tree/irisgl/thirdparty/ogre-next" rev-parse HEAD 2>/dev/null || echo nofork)"
    { sha256sum < "$lab"; (cd "$bin/media" && find . -type f -print0 | sort -z | xargs -0 sha256sum); echo "$fork"; } |
        sha256sum | cut -c1-64
}
stamps="$dataroot/shadercache/warm-stamps.txt"
key="$(treekey)"
# THE STAMP'S FIELDS: <scene> tier=<tier> cams=<camera;camera...|scene> <tree key> (no field holds a space)
stier="${tier:-epic}"
scams="scene"; [ "${#cams[@]}" -gt 0 ] && scams="$(IFS=';'; echo "${cams[*]}")"
if [ "$check" = 1 ]; then
    if [ -f "$stamps" ]; then
        while read -r s_scene s_tier s_cams s_key; do
            [ "$s_scene" = "$scene" ] && [ "$s_tier" = "tier=$stier" ] && [ "$s_key" = "$key" ] || continue
            have=";${s_cams#cams=};" ok=1
            if [ "${#cams[@]}" -eq 0 ]; then case "$have" in *";scene;"*) ;; *) ok=0 ;; esac; fi
            for c in "${cams[@]}"; do case "$have" in *";$c;"*) ;; *) ok=0 ;; esac; done
            [ "$ok" = 1 ] && exit 0
        done < "$stamps"
    fi
    subject="$file"; [ -n "$env" ] && subject="--env $env"
    camargs=""; for c in "${cams[@]}"; do camargs="$camargs --camera $c"; done
    echo "warm.sh: the shader cache in $dataroot is COLD for $scene at tier $stier${cams[*]:+ and these cameras} on this tree - run first, off any row:" >&2
    echo "  scripts/lab/warm.sh --data-root $dataroot --tier $stier$camargs $subject" >&2
    exit 3
fi

# ---- the display ------------------------------------------------------------------------------------------------
xpid="" display=""
case "${DISPLAY:-}" in
    "" | :0 | :0.* | *:0 | *:0.*) [ -n "${DISPLAY:-}" ] && echo "warm: DISPLAY=$DISPLAY is the owner's display - a private Xvfb is used instead" ;;
    *) display="$DISPLAY" ;;
esac
work="$(mktemp -d "${TMPDIR:-/tmp}/warm.XXXXXX")"
if [ -z "$display" ]; then
    for n in $(seq 60 99); do
        [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ] && continue
        Xvfb ":$n" -screen 0 1920x1080x24 -nolisten tcp > "$work/xvfb.log" 2>&1 &
        xpid=$!
        sleep 1
        if kill -0 "$xpid" 2>/dev/null && [ -e "/tmp/.X11-unix/X$n" ]; then display=":$n"; echo "$xpid" > "$work/xvfb.pid"; break; fi
        xpid=""
    done
    [ -n "$display" ] || { echo "warm.sh: no free display in :60-:99 (and \$DISPLAY names no rig)" >&2; rm -rf "$work"; exit 2; }
fi
cleanup() { [ -n "$xpid" ] && kill "$xpid" 2>/dev/null; rm -rf "$work"; }
trap cleanup EXIT

# ---- the bake (a model: off the card) ---------------------------------------------------------------------------
mkdir -p "$dataroot"
if [ -n "$file" ]; then
    nice -n 19 "$lab" --bake-only "$file" --data-root "$dataroot" > "$work/bake.log" 2>&1 ||
        { echo "warm.sh: the bake of $file failed (the log's tail:)" >&2; tail -5 "$work/bake.log" >&2; exit 4; }
    grep -E '^LAB BAKE-ONLY' "$work/bake.log" | cut -c1-200 | sed 's/^/  warm: /'
fi

# ---- the run ----------------------------------------------------------------------------------------------------
{
    [ -n "$tier" ] && echo "tier $tier"
    if [ "${#cams[@]}" -eq 0 ]; then echo "settle"; echo "frames 30"; fi
    for c in "${cams[@]}"; do echo "camera $c"; echo "settle"; echo "frames 30"; done
    echo "quit"
} > "$work/warm.lab"
subject=("$file"); [ -n "$env" ] && subject=(--env "$env")
mkdir -p "$dataroot"
"$tree/scripts/gpu-exclusive.sh" --wait-max 600 --run-timeout "$timeout" --label "warm.sh" env DISPLAY="$display" \
    nice -n 19 "$lab" --headless --no-stdin --size "$size" --data-root "$dataroot" "${subject[@]}" --script "$work/warm.lab" \
    > "$work/warm.log" 2>&1
rc=$?
grep -E '^LAB (LOAD|FIRST FRAME|END|REFUSED|FAILED)' "$work/warm.log" | cut -c1-200 | sed 's/^/  warm: /'
if [ $rc -ne 0 ]; then echo "warm.sh: jahlab exited $rc - no stamp (the log's tail:)" >&2; tail -5 "$work/warm.log" >&2; exit $rc; fi
# THE STAMP: this scene's line AT THIS TIER replaced by this run's cameras and this tree's key
mkdir -p "$dataroot/shadercache"
{ [ -f "$stamps" ] && grep -vF "$scene tier=$stier " "$stamps"; echo "$scene tier=$stier cams=$scams $key"; } > "$work/stamps.new"
mv "$work/stamps.new" "$stamps"
echo "warm.sh: $scene warmed at tier $stier in $dataroot (tree key ${key:0:12})"
exit 0
