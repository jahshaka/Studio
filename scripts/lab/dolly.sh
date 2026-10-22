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
# THE DISPLAY: $DISPLAY when it names a rig (never :0 — refused), else a private Xvfb on a free :60-:99 at 1920x1080x24,
# its pid recorded in <out-dir>/xvfb.pid and killed at the end. THE LOCK: scripts/gpu-exclusive.sh (re-entrant under a
# holder). The binary: $JAHLAB, else <this tree>/build-linux/bin/jahlab. Exit: jahlab's (0 = every stop measured).
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

# ---- the display ------------------------------------------------------------------------------------------------
xpid=""
case "${DISPLAY:-}" in
    "" | :0 | :0.*) display=""; [ -n "${DISPLAY:-}" ] && echo "dolly: DISPLAY=$DISPLAY is the owner's display — a private Xvfb is used instead" ;;
    *) display="$DISPLAY" ;;
esac
if [ -n "$display" ]; then
    :
else
    display=""
    for n in $(seq 60 99); do
        [ -e "/tmp/.X$n-lock" ] || [ -e "/tmp/.X11-unix/X$n" ] && continue
        Xvfb ":$n" -screen 0 1920x1080x24 -nolisten tcp > "$out/xvfb.log" 2>&1 &
        xpid=$!
        sleep 1
        if kill -0 "$xpid" 2>/dev/null && [ -e "/tmp/.X11-unix/X$n" ]; then display=":$n"; echo "$xpid" > "$out/xvfb.pid"; break; fi
        xpid=""
    done
    [ -n "$display" ] || { echo "dolly.sh: no free display in :60-:99 (and \$DISPLAY names no rig)" >&2; exit 2; }
fi
cleanup() { [ -n "$xpid" ] && kill "$xpid" 2>/dev/null && rm -f "$out/xvfb.pid"; }
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

# ---- the run ----------------------------------------------------------------------------------------------------
subject=()
if [ -n "$env" ]; then subject=(--env "$env" --seed "$seed"); else subject=("$file"); fi
"$tree/scripts/gpu-exclusive.sh" --label "dolly.sh" env DISPLAY="$display" nice -n 19 "$lab" --headless --no-stdin \
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
