// scripting.e2e.lattice_placement — THE PICTURE DOES NOT KNOW WHERE THE LATTICE
// SITS (lane LATTICE-1, 2026-09-21).
//
// THE RULE, stated as physics: the lit picture is a function of the SCENE. A
// Photon cascade is a box of voxels that FOLLOWS THE CAMERA in steps, and it
// re-centres only when the camera crosses a plane of its step lattice (2.578 m
// at cascade 0, Epic), so AT ONE CAMERA POSE the volume can be sitting anywhere
// within a step of where a fresh build would have put it — it depends on where
// the camera came FROM. Two viewers standing in the same place, having walked
// there from different directions, must see the same room.
//
// THE INSTRUMENT. Every arm ends with the camera at exactly the same pose and
// the scene never moves at all; only the volume's placement differs, and it is
// set by flying to P + u, letting the teleport guard build the whole chain
// there, and walking back to P — short of a step, a walk moves no volume. Each
// arm asserts what it actually got (`builtAt` is where the chain was placed,
// `atShot` where it still is), so an arm whose cascade happened to re-centre on
// the way back is REPORTED rather than silently measuring nothing: this suite
// is only worth its runtime while the volumes really are standing in different
// places, which is why the arms are checked for having moved at all.
//
// MEASURED ON THE LANE'S RIG (Xvfb 1920x1080, Epic, the dither off for the
// reading): over placements from -1.25 m to +0.625 m of the camera — sixteen
// cascade-0 cells — the whole 960x540 picture moved by at most 3/255 on any
// pixel, mean 0.15-0.33 of a code, and the cube's shaded face at most 3/255.
// The bar below is 5/255 on a probe, which is that measurement with room for
// the 8-bit dither (±1 code) and for a driver that rounds a tie differently.
//
// WHAT IT WOULD CATCH: a voxelisation that stopped being world-anchored (the
// cascade centre is quantised to the CELL, so the grid never slides under the
// geometry — remove that quantisation and a re-centre re-samples every surface
// at a new phase), a mip pyramid rebuilt against a moved origin, or a cone
// start bias that measures from the volume instead of from the surface.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var proj = project.create("Lattice Placement " + Date.now());
assert(proj.length > 10, "project.create");
assert(world.mode() === "epic", "a new scene is Epic — the tier whose chain has four cascades");

// ---- the fixture: a 4 m cube standing on the ground ------------------------
// The face the camera holds is in shadow, so what it reads is bounce and sky —
// the two terms a voxel volume decides — rather than direct sunlight, which no
// lattice can move.
var sun = world.sun().light;
node.transform(sun, { rotation: { x: -28, y: -35, z: 0 } });
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 }, onSurface: true,
                                        scale: { x: 2, y: 2, z: 2 } });
node.transform(cube, { position: { x: 0, y: 2, z: 0 } });
editor.setOverlays({ gameView: true });
editor.setCameraMode("free");

var CAM = { x: -1.0, y: 2.6, z: 9.5 }, AIM = { x: 0.6, y: 0.9, z: -1.0 };

// A grid over the shaded face plus three points of the floor around it. Screen
// coordinates, because the camera is the same in every arm by construction.
var PROBES = [];
for (var px = 0; px < 5; ++px)
    for (var py = 0; py < 5; ++py)
        PROBES.push({ x: 0.34 + px * 0.075, y: 0.10 + py * 0.14 });
PROBES.push({ x: 0.15, y: 0.85 });
PROBES.push({ x: 0.50, y: 0.92 });
PROBES.push({ x: 0.85, y: 0.85 });

function goTo(x, frames) {
    editor.setCamera({ position: { x: CAM.x + x, y: CAM.y, z: CAM.z },
                       lookAt:   { x: AIM.x + x, y: AIM.y, z: AIM.z } });
    editor.frame(frames, 1 / 60);
}
function centres() {
    var cs = world.giStatus().cascades, out = [];
    for (var i = 0; i < cs.length; ++i) out.push(cs[i].centre.x.toFixed(6));
    return out.join(",");
}

// One arm: place the chain u metres away, walk back, settle, read.
function arm(u) {
    // Away, so the teleport guard rebuilds the whole chain wherever we land next.
    editor.setCamera({ position: { x: CAM.x + 600, y: CAM.y + 300, z: CAM.z + 600 },
                       lookAt:   { x: AIM.x + 600, y: AIM.y + 300, z: AIM.z + 600 } });
    editor.frame(60, 1 / 60);
    goTo(u, 180);
    var built = centres();
    goTo(0, 300);
    // The settle is counted in FRAMES, never in seconds: this engine has no wall
    // clock (the fixed 1/60 controller), so a wall-clock settle measures nothing.
    world.refreshGi();
    editor.frame(180, 1 / 60);
    var shot = editor.screenshot("lattice_placement_" + u.toFixed(3) + ".png",
                                 960, 540, PROBES, "scene");
    var now = centres();
    return { u: u, built: built, now: now, probes: shot.probes };
}

var base = arm(0.0);
assert(base.built === base.now, "arm 0: the chain stays where the camera built it");
console.log("arm 0 centresX = [" + base.built + "]");

var OFFSETS = [0.078125, 0.3125, 0.625, -0.3125];   // 1, 4, 8 and -4 cells of cascade 0
var BAR = 5;                                        // display codes; measured 3
var moved = 0;
var worst = 0, worstWhere = "";

for (var a = 0; a < OFFSETS.length; ++a) {
    var r = arm(OFFSETS[a]);
    var held = (r.built === r.now);
    var elsewhere = (r.built !== base.built);
    console.log("arm u=" + OFFSETS[a].toFixed(6) + " builtAt=[" + r.built + "] atShot=[" + r.now
                + "] " + (held ? "HELD" : "RE-CENTRED ON THE WAY BACK"));
    if (!(held && elsewhere)) continue;             // this arm measures nothing; say so, skip it
    ++moved;
    for (var p = 0; p < PROBES.length; ++p) {
        var d = Math.max(Math.abs(r.probes[p].r - base.probes[p].r),
                         Math.abs(r.probes[p].g - base.probes[p].g),
                         Math.abs(r.probes[p].b - base.probes[p].b));
        if (d > worst) { worst = d; worstWhere = "u=" + OFFSETS[a] + " probe " + p; }
    }
}

assert(moved >= 3, "at least three arms really stood the volume somewhere else (" + moved + ")");
console.log("worst probe difference over every placement: " + worst + "/255 (" + worstWhere + ")");
assert(worst <= BAR,
       "the picture does not depend on where the volume sits: worst " + worst + "/255 <= " + BAR
       + " over placements up to eight cascade-0 cells off the camera");

// ---------------------------------------------------------------------------
// THE SECOND HALF: THE PICTURE DOES NOT KNOW WHERE THE WORLD ORIGIN IS
// (lane CLIFF-32-1, 2026-09-21, from LATTICE-1's finding 2).
//
// The same rule, stated over a longer distance. Translating the WHOLE scene and
// the camera together by d metres is not a change of the scene: every surface,
// every light direction, every screen-space effect and the camera's view of all
// of them are identical. Choose d an exact multiple of 1.875 m and it is a whole
// number of cells of EVERY cascade (24 / 12 / 4 / 1 at Epic), so the scene sits
// in the same voxels, the volumes stand in the same place relative to it, and
// the mip pyramids pair exactly. The picture must be the same picture.
//
// IT WAS NOT. Beyond 32 m the cube's shaded face read 8-9/255 BRIGHTER, flat out
// to 180 m, with the light volume proven identical at every arm. The mechanism
// (CLIFF-32-1, patch 0084): the anisotropic escape estimate `min3` read the half
// of each axis volume chosen by the SIGN of that component of the cone's
// direction, and on an axis-aligned surface two of those components are zero in
// exact arithmetic and the last bits of the view matrix in the shader -- so the
// sign, and with it the sky's weight on the surface, was decided by how far the
// camera stood from the world origin. The escape now reads both halves.
//
// The bar is 3/255 on a probe. Measured on the lane's rig after the fix, over
// the whole 960x540 face: mean 0.10-0.11 of a code and no pixel above 3 at 60 m
// (nine of 108,675 at 30 and 120 m, every one of them on the cube's silhouette
// corner, where a sub-pixel rasterisation difference from the camera's own float
// rounding lives and no probe sits). Before the fix the same measurement read
// 8.82 and 7.59.
var TRANSLATIONS = [60.0, 120.0];       // 32 and 64 cells of the outermost cascade
var TBAR = 3;                           // display codes; measured 1

var rootId = null;
var allNodes = scene.nodes();
for (var n = 0; n < allNodes.length; ++n)
    if (allNodes[n].name === "World") rootId = allNodes[n].id;
assert(rootId !== null, "the scene's root node is there to translate");

function translatedArm(d) {
    node.transform(rootId, { position: { x: d, y: 0, z: 0 } });
    // Away first, so the teleport guard builds the whole chain at this arm's pose
    // rather than walking it there; then back, and settle in FRAMES.
    editor.setCamera({ position: { x: CAM.x + d + 600, y: CAM.y + 300, z: CAM.z + 600 },
                       lookAt:   { x: AIM.x + d + 600, y: AIM.y + 300, z: AIM.z + 600 } });
    editor.frame(60, 1 / 60);
    editor.setCamera({ position: { x: CAM.x + d, y: CAM.y, z: CAM.z },
                       lookAt:   { x: AIM.x + d, y: AIM.y, z: AIM.z } });
    editor.frame(240, 1 / 60);
    world.refreshGi();
    editor.frame(240, 1 / 60);
    var shot = editor.screenshot("lattice_translate_" + d.toFixed(0) + ".png",
                                 960, 540, PROBES, "scene");
    var st = world.giStatus(), cs = [];
    for (var c = 0; c < st.cascades.length; ++c) cs.push(st.cascades[c].centre.x.toFixed(6));
    return { probes: shot.probes, centres: cs.join(","), lit: world.giVoxelStats({ cascade: 0 }) };
}

var home = translatedArm(0.0);
console.log("translation 0 m: centresX=[" + home.centres + "] voxelsLit=" + home.lit.voxelsLit);

var tWorst = 0, tWhere = "";
for (var t = 0; t < TRANSLATIONS.length; ++t) {
    var td = TRANSLATIONS[t];
    var r = translatedArm(td);
    // The volumes must really be standing somewhere else, and they must hold the
    // same light: an arm where the voxelisation moved is measuring two things.
    assert(r.centres !== home.centres, "translation " + td + " m: the volumes moved with the scene");
    assert(r.lit.voxelsLit === home.lit.voxelsLit,
           "translation " + td + " m: the light volume holds the same lit voxels ("
           + r.lit.voxelsLit + " vs " + home.lit.voxelsLit + ")");
    var worst = 0, at = 0;
    for (var p = 0; p < PROBES.length; ++p) {
        var dd = Math.max(Math.abs(r.probes[p].r - home.probes[p].r),
                          Math.abs(r.probes[p].g - home.probes[p].g),
                          Math.abs(r.probes[p].b - home.probes[p].b));
        if (dd > worst) { worst = dd; at = p; }
    }
    console.log("translation " + td + " m: centresX=[" + r.centres + "] worst probe "
                + worst + "/255 at probe " + at);
    if (worst > tWorst) { tWorst = worst; tWhere = td + " m, probe " + at; }
}
node.transform(rootId, { position: { x: 0, y: 0, z: 0 } });

assert(tWorst <= TBAR,
       "the picture does not know where the world origin is: worst " + tWorst + "/255 <= " + TBAR
       + " over translations of 60 and 120 m (" + tWhere + ")");

console.log("DONE");
