#!/bin/bash
# gi.rest_oracle_lamp / gi.rest_oracle_mover (PHOTON-7 stage 8B, B1's acceptance: THE ORACLE) - A HELD PICTURE IS THE
# POSE's: after a change at rest (a lamp step; a mover slid 1.5 m over 60 frames) the gather's cells the change reached resume, the others
# keep their rest - and once every cell holds again, the picture equals a FRESH REST of the final scene: the rest
# restarted whole (the arm gather.restOff for one frame: every cell settles from K = 0) and held again, compared byte for
# byte, within the store's quantum (one code). A resume that never happens (a cell held over an input it read) holds the
# old picture and fails here; so does a rest index that followed the path (a cell's rest mean is its pose's).
# THE ROW's PROOF (its negative control, the arm gather.resumeOff - the cells' resume sees no input - committed, run once,
# reverted): photon-room's lamp RED at 518,400 px up to 5 codes (irisgl 4ac69734, 2026-10-29); photon-glass's SLID mover
# RED at 19,775 px over a code, up to 25 (irisgl 490e51e3, reverted 2026-10-29: the held picture kept the mover's old
# pose). (The glass's first control, 287,868 px, ran while `play` moved nothing: the view switch's and the rest's.)
# THE LAW, per scene:
#   (1) the gather holds whole at both shots (the status's gather.restFrames past the settle: every cell held);
#   (2) the two shots differ by at most one code on every pixel.
# The mover's scene is read in the PHOTON DIFFUSE view (the gather's picture: photon-glass's reflection is stage 11's /
# B2's and never holds there today); the lamp's scene in the shipped picture.
# usage: rest_oracle.sh <jahlab> <work dir> lamp|mover
set -u
LAB=${1:?jahlab}; WD=${2:?work dir}; WHICH=${3:?lamp|mover}
[ -n "${DISPLAY:-}" ] || { echo "FAIL: no DISPLAY (the rig's)"; exit 1; }
rm -rf "$WD"; mkdir -p "$WD"
case "$WHICH" in
  lamp)  ENV=photon-room;  CHANGE='lamp lamp intensity 12' ;;
  mover) ENV=photon-glass; CHANGE='photon diffuse
frames 300
slide mover -1.5,0,0 60' ;;
  *) echo "FAIL: lamp|mover"; exit 1 ;;
esac
if [ "$WHICH" = lamp ]; then PRE='frames 300'; else PRE=''; fi
cat > "$WD/o.lab" <<S
envcam 0
$PRE
$CHANGE
frames 300
status sa.json
shot a
arm gather.restOff 1
frames 1
arm gather.restOff 0
frames 300
status sb.json
shot b
quit
S
"$LAB" --headless --no-stdin --size 960x540 --data-root "$WD/data" --env "$ENV" --script "$WD/o.lab" > "$WD/o.log" 2>&1
rc=$?
[ $rc -eq 0 ] || { echo "FAIL: the oracle run exited $rc"; grep -E 'LAB (REFUSED|FAILED)|ERROR' "$WD/o.log" | head; tail -5 "$WD/o.log"; exit 1; }
python3 -I - "$WD/data" "$WHICH" <<'PY'
import json, os, sys, zlib, struct
d, which = sys.argv[1], sys.argv[2]
fails = []
def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok: fails.append(what)
def status(name):
    lines = open(os.path.join(d, name)).read().splitlines()
    return json.loads(lines[-1])["gi"]["gather"]
def png(path):
    # a minimal PNG reader (8-bit RGB/RGBA, filters 0-4): the shot's pixels, no third-party module
    data = open(path, "rb").read()
    pos, w, h, ct, idat = 8, 0, 0, 0, b""
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]; t = data[pos + 4:pos + 8]; c = data[pos + 8:pos + 8 + n]
        if t == b"IHDR": w, h, _, ct = struct.unpack(">IIBB", c[:10])
        elif t == b"IDAT": idat += c
        pos += 12 + n
    bpp = 4 if ct == 6 else 3
    raw, stride, out, prev = zlib.decompress(idat), w * bpp, bytearray(), bytearray(w * bpp)
    for y in range(h):
        f, row = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = row[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: row[i] = (row[i] + a) & 255
            elif f == 2: row[i] = (row[i] + b) & 255
            elif f == 3: row[i] = (row[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                row[i] = (row[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += row; prev = row
    return w, h, bpp, out
for s in ("sa.json", "sb.json"):
    g = status(s)
    check(g.get("restFrames", 0) > g.get("settleFrames", 16) and g.get("restFrames", 0) >= 1 << 20,
          "(1) %s: the gather holds whole (rest frames %s: every cell held)" % (s, g.get("restFrames")))
wa, ha, ba, a = png(os.path.join(d, "shots", "a.png"))
wb, hb, bb, b = png(os.path.join(d, "shots", "b.png"))
check((wa, ha, ba) == (wb, hb, bb), "the two shots share a shape (%dx%d)" % (wa, ha))
if (wa, ha, ba) == (wb, hb, bb):
    worst, over = 0, 0
    for i in range(0, len(a), ba):
        m = max(abs(a[i + k] - b[i + k]) for k in range(3))
        worst = max(worst, m)
        over += 1 if m > 1 else 0
    check(worst <= 1, "(2) %s: the held picture is the fresh rest's within one code (the largest %d, %d px over a code)"
          % (which, worst, over))
print("gi.rest_oracle_%s: %s" % (which, "FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
