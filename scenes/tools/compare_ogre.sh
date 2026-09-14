#!/usr/bin/env bash
# compare_ogre.sh — put OUR port of an Ogre-Next sample next to THEIR original,
# from one recorded camera pose, and write a labelled two-up contact sheet.
# (SPECS/OGRE_SAMPLES_TAB_SPEC.md §8. P4 of the ports program.)
#
#   scenes/tools/compare_ogre.sh <SampleName> [outdir] [--ours-only]
#
#   SampleName   one of the ports: PbsMaterials LocalCubemaps Refractions Hdr
#                AreaApproxLights IesProfiles ShadowMapFromCode
#                PlanarReflections ScreenSpaceReflections Tutorial_SSAO
#                Tutorial_SMAA Decals ImageVoxelizer
#   outdir       where the three PNGs go (default: a mktemp dir, printed)
#
# ENVIRONMENT (all optional):
#   TREE     the source tree                    (default: two levels up from here)
#   BUILD    the Studio build dir               (default: $TREE/build-linux)
#   DISPLAY  the X display to render on         — SET IT. Never :0: that is the
#            owner's session, and this script launches a second Vulkan
#            application. Use your own Xvfb at 1920x1080 (the rig law: framing
#            follows the window's aspect, so any other geometry invalidates the
#            comparison on OUR side).
#
# ---------------------------------------------------------------------------
# THE FOUR RULES THIS SCRIPT EXISTS TO ENFORCE (§8), because getting any of them
# wrong produces a picture that looks like a rendering difference and is not:
#
#  1. ONE POSE, BOTH SIDES. scenes/tools/poses/<name>.json is fed to their
#     binary as --ut_playback and to ours as editor.setCamera, from the same
#     numbers. Their playback format carries camera_pos and camera_rot (a
#     quaternion, w first) per frame; ours reads frame 20's, which is the frame
#     they screenshot.
#  2. 1920x1080 ON BOTH SIDES. Ogre's Frustum default is 45 degrees VERTICAL and
#     so is our lens, but only at 16:9 — below it our viewport re-derives the
#     vertical angle to hold the 16:9 horizontal extent (the 2026-09-08 lens
#     fix), and the two pictures stop being comparable.
#  3. EXPOSURE PINNED. Our post chain adapts; theirs mostly does not. The
#     generated script sets exposureMin == exposureMax so our grade is a
#     constant.
#  4. 1x MSAA ON BOTH SIDES. Our offscreen screenshots never raise the sample
#     count and their default FSAA is "1" — which matches, as long as nobody
#     compares an on-screen multisampled Jahshaka frame against either.
#
# Their side needs NO patch: UnitTest::runLoop calls setAlwaysAskForConfig(false)
# itself, so playback never shows the config dialog. The seeded ogre.cfg below
# is still written, because their Vulkan xcb support defaults FULL SCREEN TO
# "Yes" and Video Mode to the largest RandR mode — a sample that seized the
# whole display is exactly the "did the editor freeze?" report §9.2 warns about.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TREE="${TREE:-$(cd "$HERE/../.." && pwd)}"
BUILD="${BUILD:-$TREE/build-linux}"
NAME="${1:?usage: compare_ogre.sh <SampleName|--all> [outdir] [--ours-only]}"
OUT="${2:-$(mktemp -d /tmp/compare_ogre.XXXXXX)}"
OURS_ONLY=0
for a in "$@"; do [ "$a" = "--ours-only" ] && OURS_ONLY=1; done

ALL_PORTS="PbsMaterials LocalCubemaps Refractions Hdr AreaApproxLights IesProfiles
           ShadowMapFromCode PlanarReflections ScreenSpaceReflections Tutorial_SSAO
           Tutorial_SMAA Decals ImageVoxelizer"

# --all: every port, then ONE contact sheet of the lot (the thing to put in
# front of somebody). Each pair is still written on its own, full size.
if [ "$NAME" = "--all" ]; then
    mkdir -p "$OUT"
    for s in $ALL_PORTS; do
        "$0" "$s" "$OUT" ${OURS_ONLY:+$([ "$OURS_ONLY" = 1 ] && echo --ours-only)} ||             echo "   !! $s failed, continuing" >&2
    done
    python3 - "$OUT" "$HERE/poses/notes.txt" $ALL_PORTS <<'PYALL'
import sys, os
try:
    from PIL import Image, ImageDraw
except ImportError:
    print("sheet: skipped (no PIL)"); sys.exit(0)
out, notes_path = sys.argv[1], sys.argv[2]
names = sys.argv[3:]
notes = {}
if os.path.exists(notes_path):
    for line in open(notes_path):
        if "\t" in line and not line.startswith("#"):
            k, v = line.split("\t", 1); notes[k] = v.strip()
rows = [n for n in names if os.path.exists(os.path.join(out, n + "_jahshaka.png"))]
W, H, PAD, TXT = 420, 236, 6, 34
sheet = Image.new("RGB", (W * 2 + PAD * 3, (H + TXT + PAD) * len(rows) + 40), (16, 16, 16))
d = ImageDraw.Draw(sheet)
d.text((PAD, 8), "OGRE-NEXT SAMPLE PORTS — ours (left) against their original (right), "
                 "one pose each, 1920x1080, 45 deg vertical, exposure pinned", fill=(240, 240, 240))
d.text((PAD, 22), "A blank right-hand tile means that sample's binary was not built in this tree.",
       fill=(150, 150, 150))
y = 40
for n in rows:
    d.text((PAD, y + 2), n, fill=(235, 235, 235))
    note = notes.get(n, "")
    if note:
        d.text((PAD + 170, y + 2), note[:150], fill=(255, 200, 120))
    for i, suffix in enumerate(("_jahshaka.png", "_ogre.png")):
        path = os.path.join(out, n + suffix)
        if os.path.exists(path):
            im = Image.open(path).convert("RGB").resize((W, H), Image.LANCZOS)
            sheet.paste(im, (PAD + i * (W + PAD), y + TXT - 16))
    y += H + TXT + PAD
sheet.save(os.path.join(out, "ogre_ports_contact_sheet.png"))
print("CONTACT SHEET: " + os.path.join(out, "ogre_ports_contact_sheet.png"))
PYALL
    exit 0
fi

POSE="$HERE/poses/$NAME.json"
ARCHIVE="$TREE/scenes/ogre/$NAME.zip"
SAMPLES="$TREE/irisgl/thirdparty/ogre-next/build/bin"
APP="$BUILD/bin/Jahshaka"

[ -f "$POSE" ]    || { echo "no pose file: $POSE" >&2; exit 1; }
[ -f "$ARCHIVE" ] || { echo "no port archive: $ARCHIVE" >&2; exit 1; }
[ -x "$APP" ]     || { echo "no Jahshaka at $APP (set BUILD=)" >&2; exit 1; }
[ -n "${DISPLAY:-}" ] || { echo "DISPLAY is unset — use your own Xvfb, never :0" >&2; exit 1; }
[ "${DISPLAY}" = ":0" ] && { echo "refusing DISPLAY=:0 — that is the owner's session" >&2; exit 1; }

mkdir -p "$OUT"
SCRATCH="$(mktemp -d "$OUT/scratch.XXXXXX")"
echo "== $NAME  ->  $OUT   (display $DISPLAY)"

# ---------------------------------------------------------------------------
# THEIR SIDE: the playback harness, 25 frames, PNG on frame 20.
# ---------------------------------------------------------------------------
THEIRS="$OUT/${NAME}_ogre.png"
if [ "$OURS_ONLY" = 0 ] && [ -x "$SAMPLES/Sample_$NAME" ]; then
    cat > "$SAMPLES/ogre.cfg" <<CFG
Render System=Vulkan Rendering Subsystem

[Vulkan Rendering Subsystem]
Device=(default)
Interface=xcb
FSAA=1
sRGB Gamma Conversion=Yes
Full Screen=No
Video Mode=1920 x 1080
VSync=Yes
CFG
    theirdir="$SCRATCH/ogre"
    mkdir -p "$theirdir"
    # cwd is load-bearing: resources2.cfg, plugins.cfg and ogre.cfg all resolve
    # from it, and GraphicsSystem picks the first write-access folder for Ogre.log.
    ( cd "$SAMPLES" && nice -n 19 ionice -c 3 \
        "./Sample_$NAME" "--ut_playback=$POSE" "--ut_output=$theirdir" >"$SCRATCH/ogre.log" 2>&1 ) || true
    shot="$(find "$theirdir" -name '*_colour.png' | sort | tail -1 || true)"
    if [ -n "$shot" ]; then
        cp "$shot" "$THEIRS"
        echo "   theirs: $THEIRS  ($(python3 -c "from PIL import Image;im=Image.open('$THEIRS');print('%dx%d'%im.size)" 2>/dev/null || echo '?'))"
    else
        echo "   theirs: FAILED — see $SCRATCH/ogre.log" >&2
        THEIRS=""
    fi
else
    [ "$OURS_ONLY" = 0 ] && echo "   theirs: not built (OGRE_SAMPLES=1 ./irisgl/scripts/build-ogre.sh)"
    THEIRS=""
fi

# ---------------------------------------------------------------------------
# OUR SIDE: import the port, take their pose, pin the exposure, shoot.
# ---------------------------------------------------------------------------
OURS="$OUT/${NAME}_jahshaka.png"
HOME_DIR="$SCRATCH/home"; mkdir -p "$HOME_DIR/run"
python3 - "$POSE" "$ARCHIVE" "$OURS" > "$SCRATCH/ours.js" <<'PY'
import json, sys
pose, archive, out = sys.argv[1], sys.argv[2], sys.argv[3]
d = json.load(open(pose))
# The frame they screenshot is the frame we photograph.
shot = [f for f in d["frame_activity"] if f.get("screenshot_render_window")]
f = shot[-1] if shot else d["frame_activity"][-1]
p, q = f["camera_pos"], f.get("camera_rot", [1, 0, 0, 0])
fov = f.get("fov_y_degrees", 45.0)
print(f'''// GENERATED by compare_ogre.sh — do not commit, do not edit.
function assert(c, m) {{ if (!c) throw new Error("compare: " + m); }}
var imported = project.importArchive({json.dumps(archive)});
assert(imported && imported.guid, "importArchive");
assert(project.open(imported.guid), "open");
// THEIR POSE, their quaternion (w first in the JSON, `scalar` here).
editor.setCamera({{ position: {{ x: {p[0]}, y: {p[1]}, z: {p[2]} }},
                   rotation: {{ scalar: {q[0]}, x: {q[1]}, y: {q[2]}, z: {q[3]} }},
                   fov: {fov} }});
// PIN THE EXPOSURE (rig rule 3): equal min and max stops the chain following
// the content, so the grade is a constant on our side as it is on theirs.
var fx = world.postFx();
world.postFx({{ exposureMin: fx.exposure, exposureMax: fx.exposure }});
editor.select(null);
editor.gameView(true);
// FRAMES, NEVER A WALL-CLOCK SETTLE: this engine has no wall clock, so a
// "wait a second" would measure nothing (CLAUDE.md, cameras.exposure).
editor.frame(90, 1.0 / 60.0);
var shot = editor.screenshot({json.dumps(out)}, 1920, 1080, [{{x:0.5,y:0.5}}], "viewport");
console.log("COMPARE ours centre " + JSON.stringify(shot.center));
project.close();''')
PY
( cd "$HOME_DIR/run" && HOME="$HOME_DIR" nice -n 19 ionice -c 3 \
    "$APP" --script "$SCRATCH/ours.js" --data-root "$HOME_DIR/data" \
    >"$SCRATCH/ours.log" 2>&1 ) || true
if [ -f "$OURS" ]; then
    echo "   ours:   $OURS"
else
    echo "   ours:   FAILED — see $SCRATCH/ours.log" >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# THE CONTACT SHEET. EVERY TILE CARRIES ITS LABEL — a picture with no caption is
# how a known, accepted divergence becomes a bug report (§8).
# ---------------------------------------------------------------------------
python3 - "$NAME" "$OURS" "$THEIRS" "$OUT/${NAME}_compare.png" "$HERE/poses/notes.txt" <<'PY'
import sys, os
try:
    from PIL import Image, ImageDraw
except ImportError:
    print("   sheet:  skipped (no PIL: pip install pillow)"); sys.exit(0)
name, ours, theirs, out, notes_path = sys.argv[1:6]
note = ""
if os.path.exists(notes_path):
    for line in open(notes_path):
        if line.startswith(name + "\t"):
            note = line.split("\t", 1)[1].strip()
W, H, BAR = 960, 540, 64
imgs = [("Jahshaka port", ours)]
if theirs and os.path.exists(theirs):
    imgs.append(("Ogre-Next original", theirs))
sheet = Image.new("RGB", (W * len(imgs), H + BAR * 2), (18, 18, 18))
d = ImageDraw.Draw(sheet)
d.text((10, 8), "%s   —   one pose, 1920x1080, 45 deg vertical, exposure pinned" % name,
       fill=(235, 235, 235))
if note:
    d.text((10, 26), "WHAT DIFFERS BY DESIGN: " + note, fill=(255, 200, 120))
for i, (label, path) in enumerate(imgs):
    im = Image.open(path).convert("RGB").resize((W, H), Image.LANCZOS)
    sheet.paste(im, (i * W, BAR))
    d.text((i * W + 10, BAR + H + 10), label, fill=(235, 235, 235))
sheet.save(out)
print("   sheet:  " + out)
PY
rm -rf "$SCRATCH"
