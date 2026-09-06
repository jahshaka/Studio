#!/usr/bin/env bash
# warmup-ab.sh — the cold-start warm-up A/B (SPECS/THREADING_ADOPTION_SPEC.md
# gate P4). Read scripts/threading/README.md.
#
# WHAT IS MEASURED. The startup shader-build gate (src/app/shaderbuildgate.cpp)
# already logs its own verdict:
#
#     startup shader build: N shaders (C compiled, K from cache) in T ms
#
# so there is nothing to instrument — this script only arranges the two
# conditions honestly and reads that line back. T is the number.
#
# THE TWO ARMS, and the caveat that has to travel with them:
#
#   A  JAHSHAKA_SCENE_THREADS=1 — every Tier::Primary scene gets ONE worker.
#      Parallel warm-up compile needs getNumWorkerThreads() > 1
#      (OgreRenderQueue.cpp:588) and getNumWorkerThreads() returns max(n, 1)
#      (OgreSceneManager.cpp:170), so at 1 the compile is serial: this is
#      exactly the pre-P4 behaviour of the warm-up scene.
#   B  the default — the warm-up scene is Tier::Primary (the machine, capped
#      at 8) since P4(a).
#
#   CAVEAT, STATED RATHER THAN HIDDEN: JAHSHAKA_SCENE_THREADS is the PRIMARY
#   TIER's override, so arm A also serialises the editor scene's OWN per-scene
#   warm-up (viewport/enginesceneviewport.cpp), which already ran at Primary and
#   became parallel the moment P1 landed. The delta below is therefore "parallel
#   warm-up compile, in total", of which P4(a) — moving the STARTUP scene off
#   Tier::Utility — is the part that needed a code change. A knob that isolated
#   the startup scene alone would be one more permanent piece of surface for one
#   measurement; the spec's own P4 text says the per-scene warm-up "becomes free
#   value the moment P1 lands" and is the baseline this must be compared against.
#
# TWO MODES, and the second one is the one that has a number in it:
#
#   --mode cold  (default) both caches wiped every run: ours under HOME and the
#                DRIVER's under XDG_CACHE_HOME (the driver's own is worth ~9% of
#                a cold launch — scripts/shadercache/README.md rule 1 — and a run
#                that leaves it warm is measuring NVIDIA).
#                MEASURED FINDING: on this tree the gate COMPILES NOTHING in this
#                mode, so both arms report the same number. Everything
#                process-wide (Hlms registration, the low-level material scripts,
#                the compositor chain) has already compiled inside MainWindow's
#                constructor by the time the gate runs, and a cold HOME has no
#                recorded warm-up set to replay — the gate's own frames build
#                zero shaders and it simply waits out its 250 ms settle. See the
#                lane report: P4's premise ("47 shaders in second 1") describes
#                the process, not this function.
#   --mode warm  HOME kept between runs (so warmup.set and the shader cache both
#                exist), driver cache still wiped. This is what an ordinary
#                second launch is, and it is the only mode in which the gate has
#                work to parallelise: applyWarmUpSet replays the recorded
#                permutations, and the Hlms disk cache removes template parsing
#                but NOT pipeline creation (OgreHlmsDiskCache.cpp:424-455 — the
#                PSO replay is commented out upstream).
#
# Usage:
#   scripts/threading/warmup-ab.sh --display :N [--mode cold|warm] [--runs 5]
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

binary="$root/build-linux/bin/Jahshaka"
runs=5; display="${DISPLAY:-}"; outdir=""; keep=0; mode=cold

while [ $# -gt 0 ]; do
  case "$1" in
    --binary)  binary="$2"; shift 2;;
    --runs)    runs="$2"; shift 2;;
    --mode)    mode="$2"; shift 2;;
    --display) display="$2"; shift 2;;
    --out)     outdir="$2"; shift 2;;
    --keep)    keep=1; shift;;
    -h|--help) sed -n '2,50p' "$0"; exit 0;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done

[ -x "$binary" ] || { echo "no such binary: $binary" >&2; exit 2; }
[ -n "$display" ] || { echo "no X display (pass --display :N)" >&2; exit 2; }
case "$mode" in cold|warm) ;; *) echo "bad --mode $mode" >&2; exit 2;; esac

if [ -z "$outdir" ]; then
  outdir="$(mktemp -d -t warmup-ab.XXXXXX)"
  trap '[ "$keep" = 1 ] || rm -rf "$outdir"' EXIT
fi
mkdir -p "$outdir"/{home,cache,run}

# The scenario: start the app, ask nothing, quit. The startup gate has already
# run by the time a --script file is read (src/app/main.cpp:321), so this is a
# measurement of the gate and of nothing else the script could add.
cat > "$outdir/noop.js" <<'JS'
// warmup-ab's scenario: the startup shader-build gate has already run by the
// time this is read. Report what it did, and stop.
console.log("shaderCache: " + JSON.stringify(app.shaderCache()));
console.log("threading: " + JSON.stringify(app.threading()));
JS

echo "binary   : $binary"
echo "scratch  : $outdir   (HOME and XDG_CACHE_HOME both pinned here)"
echo "display  : $display   runs per arm: $runs   mode: $mode"
echo

runone() {
  local arm="$1" label="$2"
  local rundir="$outdir/run/$label"
  rm -rf "$rundir"; mkdir -p "$rundir"
  # The driver's cache is scratch-fresh in BOTH modes; ours survives in warm.
  rm -rf "$outdir/cache"
  [ "$mode" = cold ] && rm -rf "$outdir/home/.local/share/Jahshaka/shadercache"
  mkdir -p "$outdir/cache"
  local env_arm=()
  [ "$arm" = A ] && env_arm=(JAHSHAKA_SCENE_THREADS=1)
  ( cd "$rundir" && env HOME="$outdir/home" XDG_CACHE_HOME="$outdir/cache" \
        DISPLAY="$display" "${env_arm[@]}" \
        "$binary" --script "$outdir/noop.js" ) > "$rundir/stdout.log" 2>&1
  local line ms shaders compiled
  line="$(grep -m1 "startup shader build:" "$rundir/stdout.log" || true)"
  [ -n "$line" ] || { echo "$label ($arm): no gate line — see $rundir/stdout.log" >&2; return 1; }
  # "startup shader build: N shaders (C compiled, K from cache) in T ms"
  shaders="$(echo "$line" | sed -n 's/.*build: \([0-9]*\) shaders.*/\1/p')"
  compiled="$(echo "$line" | sed -n 's/.*(\([0-9]*\) compiled.*/\1/p')"
  ms="$(echo "$line" | sed -n 's/.*in \([0-9]*\) ms.*/\1/p')"
  printf "%-6s %s  gateMs=%-6s shaders=%-4s compiled=%s\n" "$label" "$arm" "$ms" "$shaders" "$compiled"
  echo "$arm $ms $shaders $compiled" >> "$outdir/results.txt"
}

# A warm mode is meaningless until something has warmed it: one unmeasured run
# writes the shader cache AND the recorded warm-up set the measured ones replay.
if [ "$mode" = warm ]; then
  echo "priming (1 unmeasured run)..."
  runone B prime >/dev/null || exit 1
fi
: > "$outdir/results.txt"
for i in $(seq 1 "$runs"); do
  runone A "a$i" || exit 1
  runone B "b$i" || exit 1
done

echo
awk '
{ arm=$1; idx=n[arm]+0; v[arm,idx]=$2+0; s[arm,idx]=$3+0; n[arm]=idx+1 }
function median(arm,   cnt,i,j,t,a) {
  cnt=n[arm]; if(!cnt) return -1
  for(i=0;i<cnt;++i) a[i]=v[arm,i]
  for(i=0;i<cnt;++i) for(j=i+1;j<cnt;++j) if(a[j]<a[i]){t=a[i];a[i]=a[j];a[j]=t}
  lo=a[0]; hi=a[cnt-1]
  return (cnt%2)?a[int(cnt/2)]:(a[cnt/2-1]+a[cnt/2])/2 }
END {
  ma=median("A"); loa=lo; hia=hi
  mb=median("B"); lob=lo; hib=hi
  printf "gate ms   A (serial warm-up) median %.0f  (min %.0f, max %.0f)\n", ma, loa, hia
  printf "gate ms   B (parallel)       median %.0f  (min %.0f, max %.0f)\n", mb, lob, hib
  if (ma) printf "                             B/A %.2fx   (%.0f ms saved)\n", mb/ma, ma-mb
}' "$outdir/results.txt"

echo
echo "raw: $outdir/results.txt"
[ "$keep" = 1 ] && echo "scratch kept at $outdir"
exit 0
