#!/usr/bin/env bash
# texture-ab.sh — the batched-texture-loading A/B (SPECS/THREADING_ADOPTION_SPEC.md
# gate G2-c). Read scripts/threading/README.md before quoting a number from it.
#
#   A = today's behaviour, reachable by environment and nothing else:
#       JAH_TEXTURE_SYNC_LOAD=1 (loadTexture waits per texture, as it did before
#       P2), JAH_TEXTURE_MULTILOAD=0 (one decode thread), JAH_TEXTURE_CACHE=0
#       (neither the metadata cache nor the channel sidecar).
#   B = the new default: schedule-then-wait-at-the-frame-edge, the multiload
#       pool, and both caches.
#
# Alternated A/B/A/B for N pairs, in ONE pinned HOME, against ONE project built
# once by the fixture.
#
# WHY ONE HOME AND NOT A FRESH ONE PER RUN, which is what the spec's first draft
# asked for: a fresh HOME means a cold SHADER cache, and a cold shader cache
# means the first frame of every run also compiles several dozen shaders. That is
# the number P1's A/B already measured (interaction I-3: "measure P1 and P2
# separately, or the open-time numbers cannot be attributed"). Pinning HOME and
# priming once removes the compile burst from BOTH arms and leaves the texture
# work, which is the subject. The HOME is still scratch and still thrown away —
# it is fresh for the experiment, not for each run inside it.
#
# Usage:
#   scripts/threading/texture-ab.sh [options]
#     --binary PATH     the Jahshaka executable (default: build-linux/bin/Jahshaka)
#     --pairs N         A/B pairs to measure       (default: 5)
#     --textures N      images in the fixture      (default: 48)
#     --size N          texture edge in pixels     (default: 2048)
#     --display :N      X display                  (default: $DISPLAY)
#     --out DIR         scratch root (default: a mktemp dir, removed at exit)
#     --keep            keep the scratch root
set -u

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"

binary="$root/build-linux/bin/Jahshaka"
pairs=5; textures=48; size=2048; display="${DISPLAY:-}"; outdir=""; keep=0

while [ $# -gt 0 ]; do
  case "$1" in
    --binary)   binary="$2"; shift 2;;
    --pairs)    pairs="$2"; shift 2;;
    --textures) textures="$2"; shift 2;;
    --size)     size="$2"; shift 2;;
    --display)  display="$2"; shift 2;;
    --out)      outdir="$2"; shift 2;;
    --keep)     keep=1; shift;;
    -h|--help)  sed -n '2,32p' "$0"; exit 0;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done

[ -x "$binary" ] || { echo "no such binary: $binary" >&2; exit 2; }
[ -n "$display" ] || { echo "no X display (pass --display :N)" >&2; exit 2; }

if [ -z "$outdir" ]; then
  outdir="$(mktemp -d -t texture-ab.XXXXXX)"
  trap '[ "$keep" = 1 ] || rm -rf "$outdir"' EXIT
fi
mkdir -p "$outdir"/{home,tex,run}

echo "binary   : $binary"
echo "scratch  : $outdir"
echo "display  : $display   pairs: $pairs   textures: $textures @ ${size}^2"
echo

# --- the art --------------------------------------------------------------
python3 "$here/gen-textures.py" "$outdir/tex" "$textures" "$size" || exit 1

# --- the fixture ----------------------------------------------------------
# The generator's directory is substituted in rather than read from the
# environment, because a script run by --script has no getenv.
sed "s|@TEXDIR@|$outdir/tex|g" "$here/build-texture-scene.js.in" > "$outdir/build-texture-scene.js"
echo "building the fixture project (this imports $textures large images — minutes, once)..."
( cd "$outdir/run" && env HOME="$outdir/home" DISPLAY="$display" \
      "$binary" --script "$outdir/build-texture-scene.js" ) > "$outdir/fixture.log" 2>&1
grep -q "^BUILT " "$outdir/fixture.log" || {
    echo "the fixture did not build — see $outdir/fixture.log" >&2
    tail -20 "$outdir/fixture.log" >&2
    exit 1
}
grep "^BUILT " "$outdir/fixture.log"

# --- one measured run -----------------------------------------------------
# $1 = arm (A|B), $2 = run label. Everything that distinguishes the arms is an
# environment variable, so the BINARY is identical between them — no rebuild, no
# #ifdef, nothing that could differ by accident.
runone() {
  local arm="$1" label="$2"
  local rundir="$outdir/run/$label"
  rm -rf "$rundir"; mkdir -p "$rundir"
  local env_arm=()
  if [ "$arm" = A ]; then
      env_arm=(JAH_TEXTURE_SYNC_LOAD=1 JAH_TEXTURE_MULTILOAD=0 JAH_TEXTURE_CACHE=0)
  fi
  ( cd "$rundir" && env HOME="$outdir/home" DISPLAY="$display" "${env_arm[@]}" \
        "$binary" --script "$here/measure-texture-open.js" ) > "$rundir/stdout.log" 2>&1
  local line
  line="$(grep -m1 "^RESULT " "$rundir/stdout.log" || true)"
  [ -n "$line" ] || { echo "$label ($arm): NO RESULT — see $rundir/stdout.log" >&2; return 1; }
  echo "$arm $line" >> "$outdir/results.txt"
  printf "%-6s %s  %s\n" "$label" "$arm" "${line#RESULT }"
}

: > "$outdir/results.txt"
# TWO priming runs, one per arm, unmeasured: the first launch in a fresh HOME
# compiles the shaders and writes the caches, and the first B run is also the one
# that WRITES the texture cache the later B runs read. Measuring either would be
# measuring the priming.
echo "priming (2 unmeasured runs)..."
runone B prime-b >/dev/null || exit 1
runone A prime-a >/dev/null || exit 1
: > "$outdir/results.txt"

for i in $(seq 1 "$pairs"); do
  runone A "a$i" || exit 1
  runone B "b$i" || exit 1
done

echo
# Median AND spread, per the gate's wording — a mean over five runs on a box
# with other work on it is the number that lies.
awk '
{ arm=$1
  for (i=2;i<=NF;++i) { split($i,kv,"="); v[arm][kv[1]][n[arm][kv[1]]++]=kv[2]+0 } }
END {
  split("openMs firstFrameMs waitedMs slowFrames worstMs loadRequests", keys, " ")
  printf "%-14s %12s %12s %10s\n", "metric", "A (sync)", "B (batched)", "B/A"
  for (k=1;k<=6;++k) { key=keys[k]
    for (a=1;a<=2;++a) { arm=(a==1?"A":"B")
      cnt=n[arm][key]; if (!cnt) continue
      for (i=0;i<cnt;++i) s[i]=v[arm][key][i]
      for (i=0;i<cnt;++i) for (j=i+1;j<cnt;++j) if (s[j]<s[i]) { t=s[i]; s[i]=s[j]; s[j]=t }
      med[arm] = (cnt%2) ? s[int(cnt/2)] : (s[cnt/2-1]+s[cnt/2])/2
      lo[arm]=s[0]; hi[arm]=s[cnt-1]
    }
    ratio = med["A"] ? med["B"]/med["A"] : 0
    printf "%-14s %12.1f %12.1f %9.2fx\n", key, med["A"], med["B"], ratio
    printf "%-14s %12s %12s\n", "  (min..max)", sprintf("%.0f..%.0f",lo["A"],hi["A"]), sprintf("%.0f..%.0f",lo["B"],hi["B"])
  }
}' "$outdir/results.txt"

echo
echo "raw: $outdir/results.txt"
[ "$keep" = 1 ] && echo "scratch kept at $outdir"
exit 0
