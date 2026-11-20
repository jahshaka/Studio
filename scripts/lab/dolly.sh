#!/bin/bash
# scripts/lab/dolly.sh — THE DOLLY AS ONE COMMAND (LAB-1 stage 3; DOCS/LAB.md): jahlab, offscreen, on an Xvfb, under the
# card's lock, through a stops file; its output is THE TABLE the Atom specs quote (per stop: the cut's triangles, the
# hidden objects' triangles and share, the visible Atom pixels, the occluded objects) and, on an environment, each
# predicted quantity beside its measured value (jahlab `measure`).
#
# usage: dolly.sh [--tier low|medium|high|epic] [--size WxH] [--data-root DIR] [--seed N]
#                 <model file | --env NAME> <stops-file> <out-dir>
#
# THE STOPS FILE, one stop a line (`#` starts a comment; a trailing `# name` names the stop):
#   camera x,y,z,tx,ty,tz      a pose (a model or an environment)         -> settle, probe, shot, status
#   D                          an environment's dolly stop at D metres    -> measure D (settle + probe), shot, status
# THE OUT DIR receives: dolly.lab (the script it ran), dolly.log (jahlab's output), stop<N>.png, status.jsonl (one
# status per stop), measure.tsv (environments), table.tsv (the table) — and the table on stdout.
# THE DISPLAY: $DISPLAY when it names a rig (60-99), else the tree's rig (scripts/rig.sh up: reused, or started and brought
# down at the end); the run pinned to our CPUs (rig.sh cpus). THE LOCK: scripts/gpu-exclusive.sh (re-entrant under a
# holder). THE BAKE: a model is baked first, OFF the card (jahlab --bake-only into the data root; a cached bake costs a
# hash) - a run on a model never bakes under the lock (jahlab refuses a model whose bake is not cached, exit 5; the
# urgent list's #30). The binary: $JAHLAB, else <this tree>/build-linux/bin/jahlab. Exit: jahlab's (0 = every stop
# measured; 4 = the bake failed).
set -u
here="$(cd "$(dirname "$0")" && pwd)"
tree="$(cd "$here/../.." && pwd)"
lab="${JAHLAB:-$tree/build-linux/bin/jahlab}"
tier=epic size=1920x1080 dataroot="${HOME}/.local/share/jahlab" seed=1 env="" file=""
usage() { echo "usage: dolly.sh [--tier T] [--size WxH] [--data-root DIR] [--seed N] <file | --env NAME> <stops-file> <out-dir>" >&2; exit 64; }
pos=()
while [ "$#" -gt 0 ]; do
    case "$1" in
        --tier) tier="${2:?}"; shift 2 ;;
        --size) size="${2:?}"; shift 2 ;;
        --data-root) dataroot="${2:?}"; shift 2 ;;
        --seed) seed="${2:?}"; shift 2 ;;
        --env) env="${2:?}"; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
        -*) usage ;;
        *) pos+=("$1"); shift ;;
    esac
done
if [ -n "$env" ]; then [ "${#pos[@]}" -eq 2 ] || usage; stops="${pos[0]}"; out="${pos[1]}"
else [ "${#pos[@]}" -eq 3 ] || usage; file="${pos[0]}"; stops="${pos[1]}"; out="${pos[2]}"; fi
[ -x "$lab" ] || { echo "dolly.sh: no jahlab at $lab (build the tree, or set JAHLAB)" >&2; exit 2; }
[ -f "$stops" ] || { echo "dolly.sh: no stops file $stops" >&2; exit 2; }
mkdir -p "$out" || exit 2
out="$(cd "$out" && pwd)"
rm -f "$out/status.jsonl" "$out/measure.tsv" "$out/table.tsv" "$out"/stop*.png

# ---- the display: scripts/rig.sh (WORKFLOW-STREAMLINE-1 5.8) ------------------------------------------------------
# $DISPLAY when it names a rig (60-99), else the tree's rig (`rig.sh up --env`: reused when up, else started - and then
# brought down at the end, by its recorded pid); our CPUs from the one setting (rig.sh cpus)
rig="$tree/scripts/rig.sh" rig_started="" display=""
case "${DISPLAY:-}" in
    :[6-9][0-9] | :[6-9][0-9].*) display="$DISPLAY" ;;
    *) [ -n "${DISPLAY:-}" ] && echo "dolly: DISPLAY=$DISPLAY is not a rig (60-99) - the tree's rig is used instead"
       e=$("$rig" up --env) || { echo "dolly.sh: no rig (scripts/rig.sh up failed)" >&2; exit 2; }
       eval "$e"; display=$DISPLAY; [ "$RIG_STARTED" = 1 ] && rig_started=1 ;;
esac
cpus=$("$rig" cpus) || exit 2
cleanup() { [ -z "$rig_started" ] || "$rig" down > /dev/null; }
trap cleanup EXIT

# ---- the script -------------------------------------------------------------------------------------------------
python3 -I - "$stops" "$out" > "$out/dolly.lab" <<'PY' || exit 2
import re, sys
stops, out = sys.argv[1], sys.argv[2]
n = 0
names = []
for raw in open(stops):
    line, _, comment = raw.partition("#")
    line = line.strip()
    if not line:
        continue
    n += 1
    name = comment.strip() or ("stop%d" % n)
    names.append(name)
    print("echo stop %d %s" % (n, name))
    if line.startswith("camera "):
        print(line)
        print("settle")
        print("probe")
    elif re.fullmatch(r"[0-9]+(\.[0-9]+)?", line):
        print("measure %s %s/measure.tsv" % (line, out))
    else:
        sys.exit("dolly.sh: stop %d is neither `camera x,y,z,tx,ty,tz` nor a distance: %s" % (n, line))
    print("shot %s/stop%d.png" % (out, n))
    print("status %s/status.jsonl" % out)
print("quit")
open(out + "/stops.names", "w").write("\n".join(names) + "\n")
PY

# ---- the bake (a model: off the card) ---------------------------------------------------------------------------
if [ -n "$file" ]; then
    nice -n 19 "$lab" --bake-only "$file" --data-root "$dataroot" > "$out/bake.log" 2>&1 ||
        { echo "dolly.sh: the bake failed (the log's tail:)" >&2; tail -5 "$out/bake.log" >&2; exit 4; }
    grep -E '^LAB BAKE-ONLY' "$out/bake.log" | cut -c1-220
fi

# ---- the run ----------------------------------------------------------------------------------------------------
subject=()
if [ -n "$env" ]; then subject=(--env "$env" --seed "$seed"); else subject=("$file"); fi
"$tree/scripts/gpu-exclusive.sh" --label "dolly.sh" env DISPLAY="$display" taskset -c "$cpus" nice -n 19 "$lab" --headless --no-stdin \
    --size "$size" --tier "$tier" --data-root "$dataroot" --script "$out/dolly.lab" "${subject[@]}" > "$out/dolly.log" 2>&1
rc=$?
grep -E '^LAB (LOAD|ENV|FIRST|REFUSED|FAILED|END)|ENGINE ERROR' "$out/dolly.log" | cut -c1-220

# ---- the table --------------------------------------------------------------------------------------------------
python3 -I - "$out" "${env:-$file}" "$tier" "$size" <<'PY'
import csv, json, os, sys
out, subject, tier, size = sys.argv[1:5]
names = [l.strip() for l in open(os.path.join(out, "stops.names")) if l.strip()]
rows = [json.loads(l) for l in open(os.path.join(out, "status.jsonl"))] if os.path.exists(os.path.join(out, "status.jsonl")) else []
meas = list(csv.DictReader(open(os.path.join(out, "measure.tsv")), delimiter="\t")) if os.path.exists(os.path.join(out, "measure.tsv")) else []
cols = ["stop", "name", "camera", "cutTriangles", "cutClusters", "probeTriangles", "hiddenTriangles", "hiddenShare",
        "atomPixels", "occludedObjects", "cutOverflow", "cutMissing", "frame"]
lines = ["\t".join(cols)]
print("THE DOLLY: %s, %s, %s (%d stops)" % (subject, tier, size, len(rows)))
for i, r in enumerate(rows):
    a, p, cam = r["atom"], r.get("probe") or {}, r["lab"]["camera"]["position"]
    hid, drawn = p.get("hiddenTriangles", 0), p.get("drawnTriangles", 0)
    v = [str(i + 1), names[i] if i < len(names) else "", "%.2f,%.2f,%.2f" % tuple(cam), str(a["cutTriangles"]),
         str(a["cutClusters"]), str(drawn), str(hid), "%.4f" % (hid / drawn if drawn else 0.0), str(p.get("atomPixels", 0)),
         str(a["occluded"]), str(a["cutOverflow"]), str(a["cutMissing"]), str(r["lab"]["frame"])]
    lines.append("\t".join(v))
    print("  %-8s cut %9s tris (%s clusters) | hidden %8s (%5.1f %%) | %8s Atom px | %4s occluded | overflow %s missing %s"
          % (v[1], v[3], v[4], v[6], 100.0 * float(v[7]), v[8], v[9], v[10], v[11]))
for m in meas:
    print("  predicted vs measured: %s m %-15s %10s  %10s  (x%s)" % (m["stop_m"], m["quantity"], m["predicted"], m["measured"],
                                                                       m["measured_over_predicted"]))
open(os.path.join(out, "table.tsv"), "w").write("\n".join(lines) + "\n")
if len(rows) != len(names):
    print("dolly.sh: %d of %d stops measured" % (len(rows), len(names)))
    sys.exit(1)
PY
trc=$?
[ $rc -eq 0 ] && exit $trc
exit $rc
