// scripting.e2e.cube_shade — THE TWO THINGS THE OWNER SAW ON A CUBE (review
// 2026-09-18, screenshots 10 and 11; lane CUBE-SHADE-1, 2026-09-21).
//
//   (11) a hard DIAGONAL across one flat face, corner to corner, with a
//        stippled grain over it — one flat, uniformly lit, single-material
//        quad shaded as two visibly different triangles.
//   (10) a dark gradient CLIMBING the sides of cubes that rest on the ground.
//
// Both were measured, and the measurement named two different causes, so this
// suite has two halves.
//
// HALF 1 — THE CONE BASIS (ogre-patch 0083). The VCT diffuse cones were traced
// in the MATERIAL's tangent space: a per-vertex attribute authored for
// texturing, interpolated from a different vertex triple inside each triangle
// of a quad. The cone set is not azimuthally symmetric against an anisotropic
// voxel field, so a different frame is a different answer — hence the kink
// along the quad's own diagonal, the per-pixel grain, and the fact that TURNING
// A CUBE ON ITS OWN AXIS CHANGED ITS SHADING. That last one is what this half
// asserts, because it is the whole defect stated as physics: the same solid, in
// the same place, under the same light, with the same face normal, must render
// the same picture. It does not need the diagonal to be located in the image,
// and it cannot be satisfied by a tangent frame of any kind.
//
//   fail-before, measured by running THIS SUITE with the one patched media file
//   swapped back to the stock one: 92.9 % of the face's pixels moved on the
//   90-degree turn, by up to 16/255 per pixel — 7.07 through these 5x5 probes,
//   and 4.07 on the 180-degree one. After: 0.72 and 0.00. And the fail-before
//   run's own rot0 row shows the defect directly: the five probe COLUMNS across
//   the face read 77.1 / 75.1 / 75.0 / 75.8 / 75.0 at one height, where after
//   the patch every column reads 81.8 — one face, one surface.
//
// HALF 2 — THE AO RADIUS. Screen-space AO is applied as a full-screen MULTIPLY
// of the final colour (SSAO_Apply_ps.glsl), so it darkens direct sunlight as
// well as the ambient it is a model of. That multiply is its own lane; what
// this half pins is how far the wrongness is allowed to REACH, which is the
// radius, and the radius is now a CONTACT scale (0.35 m) instead of the 2.0 m
// it shipped with. The assertion is a wall lit by the SUN ALONE — the Sky Light
// off, Photon off, no shadow on the face — where a point 1 m above the floor
// must not be darkened by an ambient-occlusion term at all.
//
//   fail-before: at 2.0 m that point lost 10/255 (and up to 35/255 lower down,
//   reaching 1.35 m up a 2 m wall). After: 0.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function lum(p) { return 0.2126 * p.r + 0.7152 * p.g + 0.0722 * p.b; }

var proj = project.create("Cube Shade " + Date.now());
assert(proj.length > 10, "project.create");
assert(world.mode() === "epic", "a new scene is Epic — the tier that has Photon and SSAO on");

// ---- 0. the default that half 2 is about -----------------------------------
var fx = world.postFx();
assert(Math.abs(fx.ssaoRadius - 0.35) < 1e-6,
       "the AO radius default is a CONTACT scale, 0.35 m (was 2.0 m until 2026-09-21): " +
       fx.ssaoRadius);

// ---- half 1: a cube turned on its own axis renders the same picture --------
// THE POSE IS THE ONE THE DEFECT WAS MEASURED IN (spikes/cube-shade-1): four
// 2 m cubes in a row on the default ground, a low sun, the camera close and
// slightly above, so the leftmost cube's face fills the left third of the
// frame. The probes are a 5x5 grid over that face, and they straddle its own
// triangulation diagonal, which is where the step was.
var sunId = world.sun().light;
var xs = [-3.4, -0.8, 1.8, 4.4], zs = [0.0, -0.6, 0.2, -0.4];
var cubes = [];
for (var i = 0; i < xs.length; i++)
    cubes.push(scene.addPrimitive("cube", { position: { x: xs[i], y: 0, z: zs[i] },
                                            onSurface: true, scale: { x: 2, y: 2, z: 2 } }));
node.transform(sunId, { rotation: { x: -28, y: -35, z: 0 } });
editor.setCamera({ position: { x: -1.0, y: 2.6, z: 9.5 },
                   lookAt:   { x: 0.6, y: 0.9, z: -1.0 } });
editor.setOverlays({ gameView: true });
editor.frame(240, 1 / 60);

var FACE = [];
for (var gx = 0; gx < 5; gx++)
    for (var gy = 0; gy < 5; gy++)
        FACE.push({ x: 0.06 + gx * 0.06, y: 0.15 + gy * 0.11 });

function faceRead(tag) {
    editor.frame(120, 1 / 60);
    var s = editor.screenshot("cube_shade_" + tag + ".png", 1920, 1080, FACE, "scene");
    var v = s.probes.map(function (p) { return lum(p); });
    console.log(tag + ": " + J(v.map(function (x) { return Math.round(x * 10) / 10; })));
    return v;
}
function worstMove(a, b) {
    var w = 0;
    for (var i = 0; i < a.length; i++) w = Math.max(w, Math.abs(a[i] - b[i]));
    return w;
}

var at0 = faceRead("rot0");
// Every probe must be ON the cube: the sky above it is far brighter and the
// floor behind it far darker, so a probe that fell off the face would make the
// comparison meaningless.
var loP = Math.min.apply(null, at0), hiP = Math.max.apply(null, at0);
assert(loP > 20 && hiP < 110, "every probe is on the cube's face (" + Math.round(loP) + ".." +
                              Math.round(hiP) + ")");

node.transform(cubes[0], { rotation: { x: 0, y: 0, z: 90 } });
var at90 = faceRead("rotz90");
node.transform(cubes[0], { rotation: { x: 0, y: 0, z: 180 } });
var at180 = faceRead("rotz180");
node.transform(cubes[0], { rotation: { x: 0, y: 0, z: 0 } });
var back = faceRead("rot0b");

var m90 = worstMove(at0, at90), m180 = worstMove(at0, at180), mback = worstMove(at0, back);
console.log("worst probe move: 90deg " + m90.toFixed(2) + ", 180deg " + m180.toFixed(2) +
            ", back " + mback.toFixed(2));
// The bound is 1.5/255 against a measured 0.72: the headroom is the dither and
// the GI's own re-settle after the node moved. The defect this replaces
// measured 7.07 through these very probes (16/255 per pixel) — the suite was
// run against the stock media on this very pose and REDS there, which is the
// only way a bound like this is worth anything.
assert(m90 <= 1.5,
       "turning the cube 90 degrees about its own axis does not change its shading (" +
       m90.toFixed(2) + "/255, was 7.07)");
assert(m180 <= 1.5,
       "...nor 180 degrees (" + m180.toFixed(2) + "/255, was 4.07)");
assert(mback <= 1.5, "...and turning it back restores the picture (" + mback.toFixed(2) + ")");

// ---- half 2: an AO term does not darken direct sunlight --------------------
// The sun alone: the Sky Light off, Photon off, the sun almost horizontal so it
// lands on the +Z face at 20 degrees off its normal and nothing shadows it.
var skyLight = world.skyLight().light;
assert(!!skyLight, "the scene has a Sky Light to switch off");
node.setProperty(skyLight, "intensity", 0.0);
node.setProperty(sunId, "intensity", 1.0);
node.transform(sunId, { rotation: { x: 70, y: 0, z: 0 } });
assert(world.photon({ enabled: false }).enabled === false, "Photon off: no indirect light at all");
// Head-on at the THIRD cube (x = 1.8, z = 0.2), a metre up its two-metre face.
editor.setCamera({ position: { x: 1.8, y: 1.0, z: 6.2 }, lookAt: { x: 1.8, y: 1.0, z: 0.2 } });
editor.frame(240, 1 / 60);

// One metre up a two-metre wall, in the middle of the sunlit face.
var WALL = [ { x: 0.5, y: 0.5 }, { x: 0.42, y: 0.5 }, { x: 0.58, y: 0.5 } ];
function wallRead(tag) {
    editor.frame(120, 1 / 60);
    var s = editor.screenshot("cube_shade_" + tag + ".png", 1280, 720, WALL, "scene");
    var v = s.probes.map(lum);
    console.log(tag + ": " + J(v.map(function (x) { return Math.round(x * 10) / 10; })));
    return v;
}
world.override({ id: "ssao", value: 0 });
var noAo = wallRead("sun_only_ao_off");
assert(Math.min.apply(null, noAo) > 20, "the wall is lit by the sun (" +
       Math.round(Math.min.apply(null, noAo)) + ")");
world.clearOverride({ id: "ssao" });
var withAo = wallRead("sun_only_ao_on");
var cut = worstMove(noAo, withAo);
console.log("AO's cut of pure sunlight at 1 m: " + cut.toFixed(2) + "/255");
assert(cut <= 1.0,
       "ambient occlusion does not darken DIRECT sunlight a metre above the floor (" +
       cut.toFixed(2) + "/255, was 10 at the old 2.0 m radius)");

// And the radius is what decides that: put it back to 2 m and the same probes
// lose light again — which is the fail-before, measured inside the suite so the
// bound above can never pass by the AO having quietly stopped working.
world.postFx({ ssaoRadius: 2.0 });
var wide = wallRead("sun_only_ao_2m");
var wideCut = worstMove(noAo, wide);
console.log("...at the old 2.0 m radius: " + wideCut.toFixed(2) + "/255");
assert(wideCut >= 4.0,
       "the AO march is alive and the radius is what confines it (2.0 m cuts " +
       wideCut.toFixed(2) + "/255 of the same sunlight)");
world.postFx({ ssaoRadius: 0.35 });

console.log("cube_shade: done");
