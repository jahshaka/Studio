// Builds scenes/"Mirror Room.zip" — the reflections sample — from nothing, and
// shoots scenes/preview/mirrorroom.png.
//
// Run it, do not hand-edit the archive:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/make_mirror_room.js > /tmp/mirror.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/mirror.js
//
// (@TREE@ is substituted rather than derived — see resave_samples_v2.js for why
// a committed tool never hardcodes a tree path.)
//
// ENGINE-UP: the sample carries a GI configuration and the preview is a real
// frame, so this cannot run --headless.
//
// WHAT THE SAMPLE IS FOR. A small sealed room — one red wall, one blue wall, a
// near-mirror panel angled at both of them, a chrome sphere, a gold torus and a
// matte teapot for the mirrors to have something to say about. It is the shape
// the gi.pcc_mirror gate measures (a parallax-corrected probe has to put the
// RED wall in a mirror that faces it), dressed for eyes rather than for a
// pixel assertion, and it is the second sample built from primitives only, so
// it regenerates in seconds and reviews as a script.
//
// GEOMETRY: `scale` multiplies a primitive's HALF extents (unit sphere, torus
// of radius 1.4, cube of half-extent 1), and the floor slab's top surface is
// y = 0.25 — so everything that stands on the floor is placed at 0.25 + its
// half height. Placing by eye is what left the review build's sphere and torus
// half-buried.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/Mirror Room.zip";
var PREVIEW = TREE + "/scenes/preview/mirrorroom.png";

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("Mirror Room").length > 0, "created the project");

// The default scene ships lights this sealed room must not inherit (the
// 2026-09-07 re-stage stripped them from the archive by hand; removing them
// HERE means a regeneration can never bring them back).
["Directional Light", "Point Light"].forEach(function (n) {
    var stray = scene.find(n);
    if (stray) assert(node.remove(stray), "removed the default-scene '" + n + "'");
});

// ROOM SCALE (owner 2026-09-07): the room reviewed too small at 1x — every
// dimension below is authored in the original units and multiplied by S, which
// matches the shipped archive's in-place 1.75x resize (commit a799312a).
var S = 1.75;
function sv(v) { return { x: v.x * S, y: v.y * S, z: v.z * S }; }

function slab(name, pos, scale, color, rough, metal) {
    var id = scene.addPrimitive("cube", { position: sv(pos) });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: sv(scale) });
    material.set(id, { baseColor: color,
                       roughness: rough === undefined ? 0.85 : rough,
                       metallic: metal === undefined ? 0.0 : metal });
    return id;
}

var FLOOR_TOP = 0.25;      // floor slab: centre -0.25, half-height 0.5
var CEIL_BOTTOM = 3.75;    // ceiling slab: centre 4.25, half-height 0.5
var WALL_HALF = CEIL_BOTTOM / 2;

// ---- the room: 8.5 x 3.5 x 8.5 of interior, sealed -------------------------
slab("Floor",    { x: 0, y: -0.25, z: 0 },          { x: 9, y: 0.5, z: 9 },          "#b9b9b9", 0.55);
slab("Ceiling",  { x: 0, y: 4.25, z: 0 },           { x: 9, y: 0.5, z: 9 },          "#cccccc");
slab("RedWall",  { x: 0, y: WALL_HALF, z: 4.75 },   { x: 9, y: WALL_HALF, z: 0.5 },  "#e01010", 0.8);  // THE wall
slab("BlueWall", { x: 0, y: WALL_HALF, z: -4.75 },  { x: 9, y: WALL_HALF, z: 0.5 },  "#1040d8", 0.8);
slab("WallW",    { x: -4.75, y: WALL_HALF, z: 0 },  { x: 0.5, y: WALL_HALF, z: 9 },  "#c4c4c4");
slab("WallE",    { x: 4.75, y: WALL_HALF, z: 0 },   { x: 0.5, y: WALL_HALF, z: 9 },  "#c4c4c4");

// ---- the mirrors -----------------------------------------------------------
// A panel standing on the floor, angled so it faces the red wall: this is the
// surface the probe has to get right.
var MIRROR_HALF = 1.5;
var mirror = slab("MirrorPanel", { x: 0, y: FLOOR_TOP + MIRROR_HALF, z: -2.2 },
                  { x: 2.6, y: MIRROR_HALF, z: 0.12 }, "#ffffff", 0.02, 1.0);

var BALL_SCALE = 1.5;
var ball = scene.addPrimitive("sphere", {
    position: sv({ x: 2.4, y: FLOOR_TOP + BALL_SCALE, z: -1.2 }) });
node.setProperty(ball, "name", "MirrorSphere");
node.transform(ball, { scale: sv({ x: BALL_SCALE, y: BALL_SCALE, z: BALL_SCALE }) });
material.set(ball, { baseColor: "#ffffff", roughness: 0.03, metallic: 1.0 });

// ---- what the mirrors look at ----------------------------------------------
var t = scene.addPrimitive("torus", { position: sv({ x: -1.9, y: FLOOR_TOP + 1.4, z: 1.4 }) });
node.setProperty(t, "name", "GoldTorus");
node.transform(t, { scale: { x: S, y: S, z: S } });
material.set(t, { baseColor: "#d9a520", roughness: 0.25, metallic: 1.0 });

var tp = scene.addPrimitive("teapot", { position: sv({ x: 1.4, y: FLOOR_TOP, z: 1.8 }) });
node.setProperty(tp, "name", "GreenTeapot");
node.transform(tp, { scale: { x: 0.55 * S, y: 0.55 * S, z: 0.55 * S } });
material.set(tp, { baseColor: "#20a040", roughness: 0.4, metallic: 0.0 });

// ---- light -----------------------------------------------------------------
// POINT lights, deliberately: a directional light injects NOTHING into VCT in a
// sealed room (the upstream light-injection march), and this room's whole point
// is bounced colour.
var l1 = scene.addLight("point", { position: sv({ x: 0, y: 3.2, z: 0 }) });
node.setProperty(l1, "name", "KeyLight");
node.setProperty(l1, "intensity", 1.4);
var l2 = scene.addLight("point", { position: sv({ x: -2.5, y: 2.8, z: -2.5 }) });
node.setProperty(l2, "name", "FillLight");
node.setProperty(l2, "intensity", 1.1);

// ---- global illumination ---------------------------------------------------
// Bounds pinned to the ROOM: auto-fit spreads the probes over inflated bounds
// (reflections P4), and a mis-placed probe is exactly what this sample shows.
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "high", bounces: 2,
                  boundsMin: sv({ x: -4.6, y: -0.6, z: -4.6 }),
                  boundsMax: sv({ x: 4.6, y: 4.6, z: 4.6 }),
                  pccGrid: { x: 3, y: 2, z: 3 } }), "GI: VCT + probes, room bounds");
editor.frame(10);
console.log("giStatus: " + JSON.stringify(world.giStatus()));

// The angle is applied after the GI build on purpose: it is a document edit
// like any other, and the sample must survive one.
node.transform(mirror, { rotation: { x: 0, y: 28, z: 0 } });

// ---- REALTIME (owner 2026-09-07: "realtime is realtime") -------------------
// The panel is a true PLANAR REFLECTOR: a whole extra scene render per frame,
// so anything that moves — an object, an avatar, the light — is in the mirror
// NEXT FRAME, no probe cadence involved. The probes keep serving every other
// glossy surface under the update budget (giUpdateBudget, default 1/frame).
assert(node.setPlanarReflector(mirror, true), "the MirrorPanel is a live planar reflector");
console.log("planar: " + JSON.stringify(world.planarReflections()));

editor.setCamera({ position: sv({ x: 3.6, y: 2.4, z: 3.6 }),
                   lookAt: sv({ x: -1.4, y: 1.3, z: -2.6 }), fov: 70 });
editor.select(null);
editor.setOverlays({ lightWires: false });
editor.frame(20);

assert(project.save(), "saved");

// ---- the shipped preview ---------------------------------------------------
editor.gameView(true);
editor.frame(60);
var shot = editor.screenshot(PREVIEW, 1280, 720, [{ x: 0.5, y: 0.5 }], true);
console.log("preview centre: " + JSON.stringify(shot.center));
editor.gameView(false);

// ---- the archive -----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + JSON.stringify(out) + ")");
assert(project.close(), "closed");
console.log("make_mirror_room: PASS");
