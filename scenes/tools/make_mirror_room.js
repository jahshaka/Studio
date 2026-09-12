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

// ROOM SCALE.
//
// 2026-09-07: the room reviewed too small at 1x and was resized 1.75x in place
// (commit a799312a), which made its interior 14.9 m x 14.9 m under a 6.1 m
// ceiling with a 5.25 m mirror ball, photographed through a 70-degree lens.
//
// 2026-09-09 (lane-samplescale, owner's SCENE-SCALE CONVENTION of 2026-09-08):
// back to S = 1 — 1 unit = 1 METRE, an 8.5 m x 8.5 m room under a 3.5 m
// ceiling, which is a room a person fits in, and the saved camera drops to the
// default explorer lens (45 degrees vertical). The two changes nearly cancel:
// tan(35) / tan(22.5) = 1.69 against the 1.75x shrink, so the same camera
// POSITION frames the smaller room almost exactly as the wide lens framed the
// big one. That is why the numbers below did not move.
//
// The 2026-09-07 "too small" reading was taken while the viewport re-derived
// every camera's vertical FOV against a fixed 95-degree cap (fixed 2026-09-08,
// CLAUDE.md free-camera framing): the room looked zoomed and its gizmo 1.7x
// too big on an ordinary monitor, and growing the room was the compensation.
var S = 1.0;
function sv(v) { return { x: v.x * S, y: v.y * S, z: v.z * S }; }

// RANGE SCALES WITH THE ROOM. The engine's point-light falloff is
// 1 / (0.5 + 0.5 * d^2 / r^2) (OgreScene.cpp: the authored range IS the range),
// invariant under a uniform scale only if the range scales with the distances —
// so the picture the 1.75x room shipped with (both lights at the default range
// 40) is reproduced here at 40 * S / 1.75. Intensity, by the same algebra, does
// not change.
var LIGHT_RANGE = 40 * S / 1.75;

function slab(name, pos, scale, color, rough, metal) {
    var id = scene.addPrimitive("cube", { position: sv(pos) });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: sv(scale) });
    material.set(id, { baseColor: color,
                       roughness: rough === undefined ? 0.85 : rough,
                       metallic: metal === undefined ? 0.0 : metal });
    return id;
}

// ONE FLOOR (owner, 2026-09-12): the room stands on the scene's DEFAULT FLOOR
// — the Ground every new scene is born with (its checker,
// services/defaultfloor.h) — not on a slab laid over it. Everything that stood
// on the slab's top now stands on y = 0.
var FLOOR_TOP = 0.0;
var CEIL_BOTTOM = 3.75;    // ceiling slab: centre 4.25, half-height 0.5
var WALL_HALF = CEIL_BOTTOM / 2;

// ---- the room: 8.5 x 3.75 x 8.5 of interior, sealed ------------------------
// The slab's satin finish is the default floor's OWN material in this room
// (roughness 0.55 on the checker); "Reset to Default Floor" (material.reset)
// returns it to the plain default.
var ground = scene.find("Ground");
assert(ground && node.property(ground, "defaultFloor") === true, "the room stands on the default floor");
assert(material.set(ground, { roughness: 0.55 }), "the floor's satin finish");
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
// is bounced colour. So this sample has NO SUN — which is a completely ordinary
// scene and never a warning (owner decision, SUN_AND_LIGHT_DEFAULTS_SPEC Q1c):
// the first directional light added to a scene is its sun, and a scene that has
// none is simply lit by its lamps and its sky.
//
// Both lamps CAST: every new light of every type is born casting soft shadows
// (owner decision 1). "FillLight" is a name, not a switch — a light that
// deliberately casts nothing is one whose Shadow Type is set to "Off (fill
// light)", and neither of these is.
var l1 = scene.addLight("point", { position: sv({ x: 0, y: 3.2, z: 0 }) });
node.setProperty(l1, "name", "KeyLight");
node.setProperty(l1, "intensity", 1.4);
node.setProperty(l1, "distance", LIGHT_RANGE);
var l2 = scene.addLight("point", { position: sv({ x: -2.5, y: 2.8, z: -2.5 }) });
node.setProperty(l2, "name", "FillLight");
node.setProperty(l2, "intensity", 1.1);
node.setProperty(l2, "distance", LIGHT_RANGE);

// ---- global illumination ---------------------------------------------------
// Bounds pinned to the ROOM: auto-fit spreads the probes over inflated bounds
// (reflections P4), and a mis-placed probe is exactly what this sample shows.
// THE TIER, not its columns (lane-rayontiers, 2026-09-09): naming mode /
// quality / bounces here PINNED them, so the archive opened as "Custom" the
// moment the Epic row moved. Epic IS the hybrid at high quality with the
// field, three bounces and two dynamic probes; bounds and the probe grid are
// not tier rows and stay explicit.
assert(world.rayon({ enabled: true, tier: "epic" }).tier === "epic", "GI: Rayon Epic");
assert(world.gi({ boundsMin: sv({ x: -4.6, y: -0.6, z: -4.6 }),
                  boundsMax: sv({ x: 4.6, y: 4.6, z: 4.6 }),
                  pccGrid: { x: 3, y: 2, z: 3 } }), "GI: room bounds + probe grid");
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

// THE CONVENTION LENS (45 degrees vertical = 72.7 horizontal at 16:9, already a
// 24 mm-equivalent wide angle), from the corner — pushed as far back and as
// high as a sealed 8.5 m room allows (0.25 m off two walls), because that is
// the whole budget a human-scale room gives a camera.
//
// THE FRAMING IS TIGHTER THAN THE 1.75x ROOM'S, and that is arithmetic rather
// than a mistake: the shipped shot stood 8 m from its subject inside a 14.9 m
// room through a 70-degree lens, and 8 m does not exist in an 8.5 m room. A
// wide lens is how a too-big room was made photographable; at the convention's
// lens the picture is what the room actually looks like from inside it. Content,
// materials, lights and the mirror behaviour are untouched.
editor.setCamera({ position: sv({ x: 4.0, y: 2.7, z: 4.0 }),
                   lookAt: sv({ x: -0.6, y: 1.1, z: -0.9 }), fov: 45 });
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
