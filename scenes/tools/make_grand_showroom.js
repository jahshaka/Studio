// Builds scenes/Showroom.zip — the GRAND SHOWROOM sample — from nothing, and
// re-shoots scenes/preview/showroom.png.
//
// Run it, do not hand-edit the archive:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/make_grand_showroom.js > /tmp/grand.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb> \
//       <build>/bin/Jahshaka --script /tmp/grand.js
//
// (@TREE@ is substituted rather than derived: a --script run has no argv and no
// notion of where the tree is, and a hardcoded path in a committed tool is how
// it silently rewrites somebody else's archives. Same convention as
// resave_samples_v2.js and reauthor_particles.js.)
//
// ENGINE-UP, deliberately: the sample carries a baked GI configuration and the
// preview is a real frame, so this cannot run --headless.
//
// WHAT THE SAMPLE IS FOR. It replaced the old 241-node Showroom (2026-09-07):
// a sealed hall of polished slabs whose whole job is to show what the
// reflections stack does — a metal-roughness ladder under VCT + parallax
// corrected cubemap probes, with a warm wall on one side and a cold one on the
// other so every mirror surface has something to say. It is built from
// PRIMITIVES ONLY, which is what makes it regenerable in a few seconds and
// diff-able as a script instead of as an 8 MB binary.
//
// IDEMPOTENT: it always builds a fresh project, so running it twice produces
// the same scene (the guids differ — they are minted per run — which is why
// the archive bytes are not expected to be identical between runs).
//
// GEOMETRY NOTE (the one deliberate difference from the review build): every
// object RESTS on the floor. `scale` multiplies a primitive's half-extents
// (the sphere is a unit sphere, the torus has radius 1.4, the cylinder is 2
// units tall), and the review build placed the spheres, the torus and the
// columns by eye — half of each one was under the floor slab, which shows as
// hemispheres and as a sunken arch. The floor's TOP surface is y = 0.25
// (centre -0.25, half-height 0.5), so a scaled sphere rests at 0.25 + scale.

var TREE = "@TREE@";
var ARCHIVE = TREE + "/scenes/Showroom.zip";
var PREVIEW = TREE + "/scenes/preview/showroom.png";

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("Grand Showroom").length > 0, "created the project");

// ---- the room --------------------------------------------------------------
// slab(): a cube whose `scale` is its HALF extent on each axis.
function slab(name, pos, scale, color, rough, metal) {
    var id = scene.addPrimitive("cube", { position: pos });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: scale });
    material.set(id, { baseColor: color,
                       roughness: rough === undefined ? 0.85 : rough,
                       metallic: metal === undefined ? 0.0 : metal });
    return id;
}

var FLOOR_TOP = 0.25;    // floor slab: centre y -0.25, half-height 0.5
var CEIL_BOTTOM = 6.75;  // ceiling slab: centre y 7.25, half-height 0.5
var WALL_HALF = (CEIL_BOTTOM - 0.0) / 2;   // walls span the floor line to the ceiling

// A polished floor: the roughness ladder above it is only legible because the
// floor itself reflects.
slab("Floor",   { x: 0, y: -0.25, z: 0 },     { x: 24, y: 0.5, z: 24 }, "#5c6166", 0.32, 0.05);
slab("Ceiling", { x: 0, y: 7.25, z: 0 },      { x: 24, y: 0.5, z: 24 }, "#b5b5b5");
slab("WallN",   { x: 0, y: WALL_HALF, z: 12.25 },  { x: 24, y: WALL_HALF, z: 0.5 }, "#c8452a", 0.7);  // warm
slab("WallS",   { x: 0, y: WALL_HALF, z: -12.25 }, { x: 24, y: WALL_HALF, z: 0.5 }, "#2a56c8", 0.7);  // cold
slab("WallW",   { x: -12.25, y: WALL_HALF, z: 0 }, { x: 0.5, y: WALL_HALF, z: 24 }, "#b0b3b6");
slab("WallE",   { x: 12.25, y: WALL_HALF, z: 0 },  { x: 0.5, y: WALL_HALF, z: 24 }, "#b0b3b6");

// Columns: a unit cylinder is 2 units tall, so half the span is the y scale.
var COL_HALF = (CEIL_BOTTOM - FLOOR_TOP) / 2;
[[-8, -8], [8, -8], [-8, 8], [8, 8]].forEach(function (p, i) {
    var c = scene.addPrimitive("cylinder", { position: { x: p[0], y: FLOOR_TOP + COL_HALF, z: p[1] } });
    node.setProperty(c, "name", "Column" + i);
    node.transform(c, { scale: { x: 1.2, y: COL_HALF, z: 1.2 } });
    material.set(c, { baseColor: "#e8e2d4", roughness: 0.6 });
});

// ---- the exhibit: a metal roughness ladder ---------------------------------
// Four chrome spheres from mirror to satin, the point of the whole room.
var SPHERE_SCALE = 1.7;
[[-7.5, 0.0], [-2.5, 0.08], [2.5, 0.25], [7.5, 0.5]].forEach(function (s) {
    var b = scene.addPrimitive("sphere", {
        position: { x: s[0], y: FLOOR_TOP + SPHERE_SCALE, z: 0 } });
    node.setProperty(b, "name", "Sphere_r" + s[1]);
    node.transform(b, { scale: { x: SPHERE_SCALE, y: SPHERE_SCALE, z: SPHERE_SCALE } });
    material.set(b, { baseColor: "#ffffff", roughness: s[1], metallic: 1.0 });
});

// Reflection CONTENT: a mirror with nothing to reflect is a grey ball.
var TORUS_SCALE = 2.0;            // the torus primitive has radius 1.4
var t = scene.addPrimitive("torus", {
    position: { x: -4.5, y: FLOOR_TOP + 1.4 * TORUS_SCALE, z: 7 } });
node.setProperty(t, "name", "GoldTorus");
node.transform(t, { scale: { x: TORUS_SCALE, y: TORUS_SCALE, z: TORUS_SCALE } });
material.set(t, { baseColor: "#d9a520", roughness: 0.2, metallic: 1.0 });

// The teapot model sits ON its base already (y 0 .. 1.89).
// Deep centre, in the gap between the two middle spheres: from the saved
// camera anything at x +-4 hides behind a sphere.
var tp = scene.addPrimitive("teapot", { position: { x: 0.5, y: FLOOR_TOP, z: 9 } });
node.setProperty(tp, "name", "GreenTeapot");
material.set(tp, { baseColor: "#20a040", roughness: 0.35 });

// ---- light -----------------------------------------------------------------
// POINT lights, deliberately: a directional light injects NOTHING into VCT in a
// sealed room (the upstream light-injection march), so the bounce lighting this
// sample is here to show would come out black.
[[0, 6.2, 0, 0.65], [-8, 5, -8, 0.5], [8, 5, 8, 0.5]].forEach(function (L, i) {
    var l = scene.addLight("point", { position: { x: L[0], y: L[1], z: L[2] } });
    node.setProperty(l, "name", "Light" + i);
    node.setProperty(l, "intensity", L[3]);
});

// ---- global illumination ---------------------------------------------------
// The hybrid at high quality (HDR + shadowed probes ride 'high'), with bounds
// pinned to the ROOM: auto-fit spreads probes over inflated bounds, and
// explicit bounds are the current correct move (reflections P4).
assert(world.gi({ mode: "vct_pcc_hybrid", quality: "high", bounces: 2,
                  boundsMin: { x: -12, y: -0.6, z: -12 },
                  boundsMax: { x: 12, y: 7.6, z: 12 },
                  pccGrid: { x: 4, y: 2, z: 4 } }), "GI: VCT + probes, room bounds");
editor.frame(12);
console.log("giStatus: " + JSON.stringify(world.giStatus()));

// ---- the saved camera ------------------------------------------------------
// A WIDE lens from the cold wall: the hall is only 23.5 units across, so the
// hero shot needs field of view rather than distance (a corner camera lands
// behind a column, and a close one photographs one sphere).
editor.setCamera({ position: { x: 1.5, y: 5.0, z: -11.0 },
                   lookAt: { x: 0.0, y: 1.6, z: 2.0 }, fov: 75 });
editor.select(null);
editor.setOverlays({ lightWires: false });
editor.frame(25);

assert(project.save(), "saved");

// ---- the shipped preview ---------------------------------------------------
// Game View: the thumbnail is the SCENE, not the editor (no grid, no gizmo, no
// selection outline). 1280x720 is the size the previous showroom.png shipped at.
editor.gameView(true);
editor.frame(60);
var shot = editor.screenshot(PREVIEW, 1280, 720, [{ x: 0.5, y: 0.5 }], true);
console.log("preview centre: " + JSON.stringify(shot.center));
editor.gameView(false);

// ---- the archive -----------------------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0, "exported " + ARCHIVE + " (" + JSON.stringify(out) + ")");
assert(project.close(), "closed");
console.log("make_grand_showroom: PASS");
