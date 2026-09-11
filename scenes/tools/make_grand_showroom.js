// Builds a GRAND SHOWROOM sample archive — scenes/Showroom.zip or
// scenes/"Showroom 2.zip" — from nothing, and shoots its preview.
//
// ONE ROOM, TWO SIZES (owner, 2026-09-12: "scale the entire Grand Showroom 2x,
// it's too small to fly around in ... keep the layout of everything but make
// everything 2x larger; make Showroom 2 and keep the old one for reference").
// The room is authored ONCE, in its original numbers times the scale S; which
// sample a run builds is the one @SAMPLE@ substitution below, and the VARIANTS
// table is the only place the two differ. One run per archive:
//
//   TREE=<absolute path to the source tree>
//   sed -e "s|@TREE@|$TREE|" -e "s|@SAMPLE@|Showroom 2|" \
//       $TREE/scenes/tools/make_grand_showroom.js > /tmp/grand.js
//   cd <a scratch dir>
//   HOME=<a scratch home> JAHSHAKA_DATA_ROOT=<a scratch data root> DISPLAY=<your Xvfb> \
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
// hemispheres and as a sunken arch. Since lane L13 the floor is the scene's
// default Ground at y = 0, so a scaled sphere of radius r rests at y = r.
//
// SCENE-SCALE CONVENTION (owner 2026-09-08, applied by lane-samplescale
// 2026-09-09). 1 unit = 1 METRE, and every shipped sample is authored to it:
// human-scale rooms, the default explorer lens (45 degrees vertical) on the
// saved camera, so one fly speed feels the same in every sample. The hall was
// authored at twice life size — a 23.5 m x 23.5 m floor under a 6.75 m
// ceiling, with 3.4 m display spheres and a 3.9 m teapot — and photographed
// through a 75-degree lens to fit it in frame. Halving it (S = 0.5, the
// "Showroom" variant) and dropping to the convention lens very nearly cancel:
// the same camera distance frames a half-size room the same way, because
// tan(37.5) / tan(22.5) = 1.85 against a 2x shrink. The result is a 11.75 m
// gallery under a 3.375 m ceiling with 1.7 m spheres — the SAME picture at a
// size a person fits in.
//
// SHOWROOM 2 DEPARTS FROM THAT RULE, BY THE OWNER'S DECISION (2026-09-12: "it's
// too small to fly around in"). It is the same room at S = 1.0 — the hall's
// original 23.5 m design size, a 6.75 m ceiling, 3.4 m spheres — kept on the
// convention's 45-degree lens, with the saved camera scaled with the room so
// the composition is the Showroom's composition, twice as far away. It is the
// one shipped sample that is not human-scale; samples.cleanstart pins it as
// such (tests/samples/scripts/cleanstart.js.in, its own SCALE block).
//
// WHAT RIDES S — ALL OF IT (lane L14 audited every number below): positions
// and half-extents (sv), the teapot's scale, the point lights' RANGE (not their
// intensity — see the light section), the GI bounds, the saved camera, and the
// three per-metre settings the new-scene template hands the room (GROW below):
// the floor's checker tiling, the fog density and the ambient-occlusion
// radius. What does NOT ride S, on purpose: the PCC probe COUNT (4 x 2 x 4 — a
// fixed count over bounds that scale IS a spacing that scales), the lens, the
// materials, the light intensities and the Epic tier. The voxel volume and the
// irradiance field are resolution-relative (128^3 voxels and 8192 field probes
// over the bounds at either size), so they scale by themselves.
//
// MEASURED, NOT ASSUMED (lane L14, region means over a 4 x 3 grid of the saved
// camera's frame and of a matched interior view; evidence in
// ~/Developer/spikes/showroom2/): the S = 1 room against the S = 0.5 room built
// by this same script differs by at most 3.3 of 255 in any region. Leaving the
// template's AO radius unscaled put Showroom 2 up to 18 levels BRIGHTER in the
// graded frame; unscaled fog, 1-3 levels darker. A 2x-denser probe grid
// (8 x 4 x 8) moved no region closer to the S = 0.5 picture (it stayed within
// 4.4 levels) at 8x the probes — the 4 x 2 x 4 grid stays.

var TREE = "@TREE@";
var SAMPLE = "@SAMPLE@";

// THE ONLY TWO LINES THE VARIANTS DIFFER BY. `S` is the room scale (see the
// header); the rest is where the run writes and what the project is called.
//
// THE SHIPPED Showroom.zip PREDATES LANE L14 and is deliberately NOT rebuilt:
// it still carries the new-scene template's two lights and the GI top inside
// the ceiling slab, and it is the owner's reference for Showroom 2 until the owner
// calls its removal (2026-09-12). Re-running the "Showroom" variant CHANGES it
// — mostly a darker floor, which the template Point Light lit through the roof.
var VARIANTS = {
    "Showroom":   { S: 0.5, archive: "Showroom.zip",   preview: "showroom.png",  project: "Grand Showroom" },
    "Showroom 2": { S: 1.0, archive: "Showroom 2.zip", preview: "showroom2.png", project: "Grand Showroom 2" }
};
var V = VARIANTS[SAMPLE];
if (!V) throw new Error("make_grand_showroom: unknown @SAMPLE@ '" + SAMPLE + "' (one of " +
                        Object.keys(VARIANTS).join(", ") + ")");
var ARCHIVE = TREE + "/scenes/" + V.archive;
var PREVIEW = TREE + "/scenes/preview/" + V.preview;

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create(V.project).length > 0, "created the project '" + V.project + "'");

// The new-scene template ships two lights this SEALED room must not inherit
// (lane L14, 2026-09-12; the Mirror Room has stripped them since 2026-09-07):
// the default Directional Light cannot reach inside a closed box — it only
// costs its three PSSM passes in every shadow-node execution — and the default
// Point Light at (-4, 4, 0) is a fixed-position, unscaled template light: at
// S = 0.5 it hung 12.5 cm above the roof (the white hotspot on it, lighting the
// interior THROUGH the roof because it casts no shadow); at S = 1 it would hang
// inside the hall. The room is lit by its own Light0-2 below and nothing else.
["Directional Light", "Point Light"].forEach(function (n) {
    var stray = scene.find(n);
    if (stray) assert(node.remove(stray), "removed the new-scene template's '" + n + "'");
});

// ROOM SCALE. Every dimension below is authored in the ORIGINAL (2x life size)
// numbers and multiplied by S — the same shape as make_mirror_room.js, so the
// diff against the reviewed build is one constant rather than sixty edited
// literals. S comes from the VARIANTS table at the top.
var S = V.S;
function sv(v) { return { x: v.x * S, y: v.y * S, z: v.z * S }; }

// GROW: how much bigger this room is than the Showroom that shipped at S = 0.5.
// The three settings the new-scene TEMPLATE hands this room (the floor's
// checker tiling, the fog, the ambient-occlusion reach) are all per-metre
// numbers tuned for, and shipped with, the S = 0.5 room; the ones that shape
// the picture scale by GROW so a larger room reads the same (1 at S = 0.5,
// which leaves the Showroom exactly as the template made it).
var GROW = S / 0.5;

// ---- the room --------------------------------------------------------------
// slab(): a cube whose `scale` is its HALF extent on each axis.
function slab(name, pos, scale, color, rough, metal) {
    var id = scene.addPrimitive("cube", { position: sv(pos) });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: sv(scale) });
    material.set(id, { baseColor: color,
                       roughness: rough === undefined ? 0.85 : rough,
                       metallic: metal === undefined ? 0.0 : metal });
    return id;
}

// ONE FLOOR (owner, 2026-09-12: "we have two floors in the Showroom — a floor
// under the items and a large floor under everything"). The gallery stands on
// the scene's DEFAULT FLOOR — the Ground every new scene is born with (its
// checker, services/defaultfloor.h) — so the floor slab this room used to lay
// on top of it is gone, and everything that stood on the slab's top now
// stands on y = 0.
var FLOOR_TOP = 0.0;
var CEIL_BOTTOM = 6.75;  // ceiling slab: centre y 7.25, half-height 0.5
var WALL_HALF = (CEIL_BOTTOM - 0.0) / 2;   // walls span the floor line to the ceiling

// A POLISHED floor: the roughness ladder above it is only legible because the
// floor itself reflects. The slab's polish is the default floor's OWN material
// in this room — roughness 0.32, metallic 0.05 on the checker — and "Reset to
// Default Floor" (material.reset) returns it to the plain default.
// The CHECKER scales with the room: textureScale is a repeat count over the
// fixed 100 m ground, so a checker cell twice as big is half the repeats (the
// default 4 = 2 m cells in the S = 0.5 room, 2 = 4 m cells at S = 1).
var ground = scene.find("Ground");
assert(ground && node.property(ground, "defaultFloor") === true, "the room stands on the default floor");
var CHECKER = material.get(ground).textureScale / GROW;
assert(material.set(ground, { roughness: 0.32, metallic: 0.05, textureScale: CHECKER }),
       "the floor's polish, checker at textureScale " + CHECKER);
slab("Ceiling", { x: 0, y: 7.25, z: 0 },      { x: 24, y: 0.5, z: 24 }, "#b5b5b5");
slab("WallN",   { x: 0, y: WALL_HALF, z: 12.25 },  { x: 24, y: WALL_HALF, z: 0.5 }, "#c8452a", 0.7);  // warm
slab("WallS",   { x: 0, y: WALL_HALF, z: -12.25 }, { x: 24, y: WALL_HALF, z: 0.5 }, "#2a56c8", 0.7);  // cold
slab("WallW",   { x: -12.25, y: WALL_HALF, z: 0 }, { x: 0.5, y: WALL_HALF, z: 24 }, "#b0b3b6");
slab("WallE",   { x: 12.25, y: WALL_HALF, z: 0 },  { x: 0.5, y: WALL_HALF, z: 24 }, "#b0b3b6");

// Columns: a unit cylinder is 2 units tall, so half the span is the y scale.
var COL_HALF = (CEIL_BOTTOM - FLOOR_TOP) / 2;
[[-8, -8], [8, -8], [-8, 8], [8, 8]].forEach(function (p, i) {
    var c = scene.addPrimitive("cylinder", { position: sv({ x: p[0], y: FLOOR_TOP + COL_HALF, z: p[1] }) });
    node.setProperty(c, "name", "Column" + i);
    node.transform(c, { scale: sv({ x: 1.2, y: COL_HALF, z: 1.2 }) });
    material.set(c, { baseColor: "#e8e2d4", roughness: 0.6 });
});

// ---- the exhibit: a metal roughness ladder ---------------------------------
// Four chrome spheres from mirror to satin, the point of the whole room.
var SPHERE_SCALE = 1.7;
// THE ONE COMPOSITION CONSEQUENCE of the scene-scale convention, recorded here
// as the convention requires. The ladder used to stand 5 units apart (2.5 m
// after S) and be photographed through a 75-degree lens; at the convention's 45
// degrees the widest thing a camera standing against this room's wall can frame
// is 8.5 m, and a 2.5 m-spaced row of 1.7 m spheres is 9.2 m wide — the outer
// two fall out of the hero frame (measured, then seen). Spacing 4.2 units
// (2.1 m) puts all four back in with ~8% margin at 16:9, which is the picture
// the sample has always shipped. The spheres, their sizes, their materials and
// their order are untouched.
[[-6.3, 0.0], [-2.1, 0.08], [2.1, 0.25], [6.3, 0.5]].forEach(function (s) {
    var b = scene.addPrimitive("sphere", {
        position: sv({ x: s[0], y: FLOOR_TOP + SPHERE_SCALE, z: 0 }) });
    node.setProperty(b, "name", "Sphere_r" + s[1]);
    node.transform(b, { scale: sv({ x: SPHERE_SCALE, y: SPHERE_SCALE, z: SPHERE_SCALE }) });
    material.set(b, { baseColor: "#ffffff", roughness: s[1], metallic: 1.0 });
});

// Reflection CONTENT: a mirror with nothing to reflect is a grey ball.
var TORUS_SCALE = 2.0;            // the torus primitive has radius 1.4
var t = scene.addPrimitive("torus", {
    position: sv({ x: -4.5, y: FLOOR_TOP + 1.4 * TORUS_SCALE, z: 7 }) });
node.setProperty(t, "name", "GoldTorus");
node.transform(t, { scale: sv({ x: TORUS_SCALE, y: TORUS_SCALE, z: TORUS_SCALE }) });
material.set(t, { baseColor: "#d9a520", roughness: 0.2, metallic: 1.0 });

// The teapot model sits ON its base already (y 0 .. 1.89).
// Deep centre, in the gap between the two middle spheres: from the saved
// camera anything at x +-4 hides behind a sphere.
// The teapot PRIMITIVE is 3.86 x 1.89 x 2.4 units as authored, so it needs the
// room's scale like everything else — unscaled it would be a 3.9 m teapot in
// the S = 0.5 room's 3.375 m hall, which is exactly the "things go weird" the
// convention is about.
var tp = scene.addPrimitive("teapot", { position: sv({ x: 0.5, y: FLOOR_TOP, z: 9 }) });
node.setProperty(tp, "name", "GreenTeapot");
node.transform(tp, { scale: { x: S, y: S, z: S } });
material.set(tp, { baseColor: "#20a040", roughness: 0.35 });

// ---- light -----------------------------------------------------------------
// POINT lights, deliberately: a directional light injects NOTHING into VCT in a
// sealed room (the upstream light-injection march), so the bounce lighting this
// sample is here to show would come out black.
//
// RANGE SCALES WITH THE ROOM. The engine's point-light falloff is
// 1 / (0.5 + 0.5 * d^2 / r^2) (OgreScene.cpp: the authored range IS the range),
// which is invariant under a uniform scale ONLY if r scales with d — halve the
// room and keep r, and every corner gets brighter. Intensity, by the same
// algebra, must NOT change. LIGHT_RANGE is the shipped 40 at the shipped size.
var LIGHT_RANGE = 40 * S;
[[0, 6.2, 0, 0.65], [-8, 5, -8, 0.5], [8, 5, 8, 0.5]].forEach(function (L, i) {
    var l = scene.addLight("point", { position: sv({ x: L[0], y: L[1], z: L[2] }) });
    node.setProperty(l, "name", "Light" + i);
    node.setProperty(l, "intensity", L[3]);
    node.setProperty(l, "distance", LIGHT_RANGE);
});

// ---- global illumination ---------------------------------------------------
// The hybrid at high quality (HDR + shadowed probes ride 'high'), with bounds
// pinned to the ROOM: auto-fit spreads probes over inflated bounds, and
// explicit bounds are the current correct move (reflections P4).
// THE TIER, not its columns (lane-rayontiers, 2026-09-09): naming mode /
// quality / bounces here PINNED them, so the archive opened as "Custom" the
// moment the Epic row moved. Epic IS the hybrid at high quality with the
// field, three bounces and two dynamic probes; bounds and the probe grid are
// not tier rows and stay explicit.
//
// THE BOUNDS TOP CLEARS THE CEILING SLAB (lane L14, 2026-09-12). The slab
// spans y 6.75 .. 7.75 (original units); the top used to be 7.6, INSIDE it, so
// the voxel volume and the irradiance field ended mid-slab and the field's
// last probe layer sat in solid ceiling — one of the two ingredients of the
// interior colour bleeding out onto the roof (the other, the field's cage never
// clamping at the volume edge, is an engine fix: ENGINE_CACHE_POLICY_SPEC P9).
// 8.0 puts the whole slab inside the volume with a quarter-unit margin.
//
// THE PROBE GRID IS A COUNT, NOT A SPACING: 4 x 2 x 4 over bounds that ride S
// is a spacing that rides S, so both variants place their 32 probes at the
// same RELATIVE positions. Lane L14 measured a 2x-denser grid (8 x 4 x 8) in
// the S = 1 room against this one — see its report for the numbers.
assert(world.rayon({ enabled: true, tier: "epic" }).tier === "epic", "GI: Rayon Epic");
assert(world.gi({ boundsMin: sv({ x: -12, y: -0.6, z: -12 }),
                  boundsMax: sv({ x: 12, y: 8.0, z: 12 }),
                  pccGrid: { x: 4, y: 2, z: 4 } }), "GI: room bounds + probe grid");

// The template's per-metre WORLD settings, scaled with the room (GROW, above).
// FOG is exponential per world unit, so a room twice as deep is twice as foggy
// at its far wall unless the density halves. AMBIENT OCCLUSION looks a fixed
// number of metres around each pixel, so the same reach in a room twice as big
// darkens half as much of every crease.
var fog0 = world.get().fog;
assert(world.fog({ density: fog0.density / GROW }), "fog density " + fog0.density + " / " + GROW);
var fx0 = world.postFx();
world.postFx({ ssaoRadius: fx0.ssaoRadius * GROW });
assert(Math.abs(world.postFx().ssaoRadius - fx0.ssaoRadius * GROW) < 1e-4,
       "AO radius " + fx0.ssaoRadius + " x " + GROW);
editor.frame(12);
console.log("giStatus: " + JSON.stringify(world.giStatus()));

// ---- the saved camera ------------------------------------------------------
// THE CONVENTION LENS, from the cold wall. 45 degrees vertical is the default
// explorer lens and is already wide (72.7 degrees horizontal at 16:9, a 24 mm
// equivalent), which is what makes a human-scale interior photographable
// without the 75-degree ultra-wide the 2x room needed. At S = 0.5 the eye is at
// 2.45 m — standing height plus a little, inside a 3.375 m room (4.9 m in
// Showroom 2's 6.75 m hall) — and the position rides S with everything else,
// so both variants photograph the same composition.
// Centred and one unit deeper than the shipped pose (which sat slightly off
// axis under a lens wide enough not to care): at 45 degrees the ladder needs
// the room's full depth and the symmetry.
editor.setCamera({ position: sv({ x: 0.0, y: 4.9, z: -11.5 }),
                   lookAt: sv({ x: 0.0, y: 1.8, z: 2.4 }), fov: 45 });
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
