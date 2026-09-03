#!/usr/bin/env bash
# shadercache-bench.sh — warm-vs-cold measurement for the persistent shader cache.
#
# Read scripts/shadercache/README.md first. The two rules this script exists to
# enforce: the GPU DRIVER has its own shader cache (worth ~9% of a cold launch),
# and OUR cache lives under AppDataLocation — so every run pins BOTH
# XDG_CACHE_HOME (the driver's) and HOME (ours) into a scratch tree. Numbers from
# a run that did not do this are not numbers.
#
# Usage:
#   scripts/shadercache/shadercache-bench.sh [options]
#     --binary PATH     the Jahshaka executable  (default: build-linux/bin/Jahshaka)
#     --runs N          measured runs per mode   (default: 3)
#     --mode M          cold | warm | warm-driver | warm-all  (default: cold)
#                         cold        both caches scratch-fresh every run
#                         warm        OUR cache warm, driver cache scratch-fresh
#                         warm-driver driver cache warm, OUR cache scratch-fresh
#                         warm-all    both warm (what a real second launch is)
#     --scenario S      selftest | script:FILE   (default: selftest)
#     --display :N      X display to use (default: $DISPLAY)
#     --probe           also build+preload the attribution probes
#     --out DIR         scratch root (default: a mktemp dir, removed at exit)
#     --keep            keep the scratch root
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

binary="$root/build-linux/bin/Jahshaka"
runs=3; mode=cold; scenario=selftest; display="${DISPLAY:-}"; probe=0; outdir=""; keep=0

while [ $# -gt 0 ]; do
  case "$1" in
    --binary)   binary="$2"; shift 2;;
    --runs)     runs="$2"; shift 2;;
    --mode)     mode="$2"; shift 2;;
    --scenario) scenario="$2"; shift 2;;
    --display)  display="$2"; shift 2;;
    --probe)    probe=1; shift;;
    --out)      outdir="$2"; shift 2;;
    --keep)     keep=1; shift;;
    -h|--help)  sed -n '2,25p' "$0"; exit 0;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done

[ -x "$binary" ] || { echo "no such binary: $binary" >&2; exit 2; }
[ -n "$display" ] || { echo "no X display (pass --display :N)" >&2; exit 2; }
case "$mode" in cold|warm|warm-driver|warm-all) ;; *) echo "bad --mode $mode" >&2; exit 2;; esac

if [ -z "$outdir" ]; then outdir="$(mktemp -d -t shadercache-bench.XXXXXX)"; trap '[ "$keep" = 1 ] || rm -rf "$outdir"' EXIT; fi
mkdir -p "$outdir"/{home,cache,run}

# --- the probes -------------------------------------------------------------
preload=""; probe_env=()
if [ "$probe" = 1 ]; then
  rs="$(ls -1 "${OGRE_NEXT_PREFIX:-$HOME/Developer/engines/ogre-next-install}"/lib/OGRE-Next/RenderSystem_Vulkan.so 2>/dev/null | head -1)"
  cc -O2 -fPIC -shared -o "$outdir/vkprobe.so"  "$here/vkprobe.c"    -ldl || exit 1
  c++ -O2 -fPIC -shared -o "$outdir/ogreprobe.so" "$here/ogreprobe.cpp" -ldl || exit 1
  preload="$outdir/vkprobe.so $outdir/ogreprobe.so"
  probe_env=(SC_VULKAN_RS_SO="$rs")
fi

# --- one run ----------------------------------------------------------------
# Everything the app could persist lives under $outdir/home; everything the
# DRIVER persists lives under $outdir/cache. Which of the two survives between
# runs is the whole experiment.
runone() {
  local n="$1"
  local rundir="$outdir/run/$n"
  rm -rf "$rundir"; mkdir -p "$rundir"
  case "$mode" in
    cold)        rm -rf "$outdir/cache" "$outdir/home/.local/share/Jahshaka/shadercache";;
    warm)        rm -rf "$outdir/cache";;
    warm-driver) rm -rf "$outdir/home/.local/share/Jahshaka/shadercache";;
    warm-all)    ;;
  esac
  mkdir -p "$outdir/cache"

  local args=(--engine-selftest "$rundir/out.png")
  case "$scenario" in script:*) args=(--script "${scenario#script:}");; esac

  local t0 t1
  t0=$(date +%s.%N)
  ( cd "$rundir" && env HOME="$outdir/home" XDG_CACHE_HOME="$outdir/cache" DISPLAY="$display" \
        ${preload:+LD_PRELOAD="$preload"} "${probe_env[@]}" \
        SC_VKPROBE_OUT="$rundir/vk.txt" SC_GLSLPROBE_OUT="$rundir/ogre.txt" \
        SC_GLSLPROBE_OUT2="$rundir/rg.txt" \
        "$binary" "${args[@]}" ) > "$rundir/stdout.log" 2>&1
  local rc=$?
  t1=$(date +%s.%N)

  local wall compiled hits log="$rundir/jahshaka-ogre.log"
  wall=$(awk -v a="$t0" -v b="$t1" 'BEGIN{printf "%.2f", b-a}')
  # grep -c PRINTS 0 and EXITS 1 on no match: "|| echo 0" would append a second
  # line and corrupt every downstream field.
  compiled=$(grep -c "compiled successfully" "$log" 2>/dev/null || true); compiled=${compiled:-0}
  hits=$(grep -c "was in microcode cache" "$log" 2>/dev/null || true); hits=${hits:-0}
  printf "%-4s %-7s wall=%6ss  compiled=%-4s microcode-hits=%-4s rc=%s\n" "$n" "$mode" "$wall" "$compiled" "$hits" "$rc"
  echo "$wall $compiled $hits $rc" >> "$outdir/results.txt"
  [ "$probe" = 1 ] && { cat "$rundir/vk.txt" "$rundir/ogre.txt" "$rundir/rg.txt" 2>/dev/null | sed 's/^/       /'; }
  return 0
}

echo "binary   : $binary"
echo "mode     : $mode   runs: $runs   display: $display"
echo "scratch  : $outdir   (HOME and XDG_CACHE_HOME both pinned here)"
echo
: > "$outdir/results.txt"
# One un-measured priming run for the warm modes: they are meaningless until
# whichever cache they warm has been written once.
case "$mode" in warm|warm-driver|warm-all) echo "priming..."; runone prime >/dev/null; : > "$outdir/results.txt";; esac
for i in $(seq 1 "$runs"); do runone "$i"; done

echo
awk '{w+=$1; c+=$2; h+=$3; n++; if(min==""||$1<min)min=$1; if($1>max)max=$1}
     END{ if(n) printf "mean wall %.2fs  (min %.2f, max %.2f)  mean compiled %.1f  mean microcode-hits %.1f  over %d runs\n", w/n, min, max, c/n, h/n, n }' \
    "$outdir/results.txt"
[ "$keep" = 1 ] && echo "scratch kept at $outdir"
exit 0
