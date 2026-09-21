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
//   fail-before: at 2.0 m the lower of the two probes lost 16/255, and the cut
//   reached 1.55 m up the 4 m wall, worst 35/255 at the base. After: 0.

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
// 4 m cubes in a row on the default ground, a low sun, the camera close and
// slightly above, so the leftmost cube's face fills the left third of the
// frame. The probes are a 5x5 grid over that face, and they straddle its own
// triangulation diagonal, which is where the step was.
var sunId = world.sun().light;
var xs = [-3.4, -0.8, 1.8, 4.4], zs = [0.0, -0.6, 0.2, -0.4];
var cubes = [];
for (var i = 0; i < xs.length; i++)
    cubes.push(scene.addPrimitive("cube", { position: { x: xs[i], y: 0, z: zs[i] },
                                            onSurface: true, scale: { x: 2, y: 2, z: 2 } }));
// (the cube primitive is 2 m across, so scale 2 is a FOUR-metre cube standing on
//  the floor: scene.bounds() reads y 0..4. They are 2.6 m apart and therefore
//  interpenetrate, which is deliberate -- it is one solid lump of geometry, and
//  a cube turned about its own axis occupies exactly the same volume, which is
//  the whole point of the assertion below.)
node.transform(sunId, { rotation: { x: -28, y: -35, z: 0 } });
editor.setCamera({ position: { x: -1.0, y: 2.6, z: 9.5 },
                   lookAt:   { x: 0.6, y: 0.9, z: -1.0 } });
editor.setOverlays({ gameView: true });
editor.frame(240, 1 / 60);

var FACE = [];
for (var gx = 0; gx < 5; gx++)
    for (var gy = 0; gy < 5; gy++)
        FACE.push({ x: 0.06 + gx * 0.06, y: 0.15 + gy * 0.11 });

// DRAINED, like the camera arm below and for the same reason: a fixed frame
// count is not a settle. Read until the face's MEAN stops moving, then keep
// that read's probes.
function faceRead(tag) {
    var prev = null, v = null;
    for (var i = 0; i < 12; i++) {
        editor.frame(150, 1 / 60);
        var s = editor.screenshot("cube_shade_" + tag + ".png", 1920, 1080, FACE, "scene");
        v = s.probes.map(function (p) { return lum(p); });
        var mean = v.reduce(function (a, b) { return a + b; }, 0) / v.length;
        if (prev !== null && Math.abs(mean - prev) < 0.05) {
            console.log(tag + " settled after " + ((i + 1) * 150) + " frames: " +
                        J(v.map(function (x) { return Math.round(x * 10) / 10; })));
            return v;
        }
        prev = mean;
    }
    console.log(tag + " DID NOT SETTLE: " + J(v.map(function (x) { return Math.round(x * 10) / 10; })));
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

// THE FACE IS ONE SURFACE, which is the owner's picture stated directly and
// without any rotation: at each of the five heights the five probe COLUMNS
// across the face must agree. A frame that is a property of the triangle puts a
// step between the columns that straddle the quad's diagonal.
//   stock:    2.1 / 5.8 / 8.1 / 4.9 / 3.8   (worst 8.1)
//   this:     0.0 / 1.8 / 3.0 / 1.1 / 1.1   (worst 3.0, and the same voxeliser
//                                            residual as above)
var worstSpread = 0;
for (var gy = 0; gy < 5; gy++) {
    var row = [];
    for (var gx = 0; gx < 5; gx++) row.push(at0[gx * 5 + gy]);
    var sp = Math.max.apply(null, row) - Math.min.apply(null, row);
    worstSpread = Math.max(worstSpread, sp);
    console.log("  height " + gy + ": " + J(row.map(function (x) { return Math.round(x * 10) / 10; })) +
                " spread " + sp.toFixed(2));
}
assert(worstSpread <= 4.0,
       "one flat face reads as ONE surface across its own diagonal (" +
       worstSpread.toFixed(2) + "/255, was 8.1)");

var m90 = worstMove(at0, at90), m180 = worstMove(at0, at180), mback = worstMove(at0, back);
console.log("worst probe move: 90deg " + m90.toFixed(2) + ", 180deg " + m180.toFixed(2) +
            ", back " + mback.toFixed(2));
// THE BOUND IS 4.0 AND IT IS NOT AT THE DITHER, WHICH IS A FINDING RATHER THAN
// A CONCESSION. The cone FRAME is a pure function of the world normal now, and
// the probed face's normal does not move when the cube is turned about its own
// axis, so the frame contributes nothing to what is left. What is left —
// 3.07/255, drained, reproducible, against 7.07 on the stock frame — is the
// VOXELISER: a 90-degree turn maps the cube's faces onto each other but not its
// TRIANGULATION, and a voxel that straddles a face's diagonal takes a different
// set of triangles before and after. That is a second defect, in a different
// place, and it is handed to LATTICE-1 with this number rather than absorbed
// into a bound that would hide it. (Two cross-checks that it is not the frame:
// the turned orientation's five probe columns read IDENTICALLY at every height,
// which a turning frame could not produce; and the same arm on a frame built
// about a body diagonal reads 0.00 — a frame that samples the voxels more
// blurrily hides the voxeliser's residual instead of fixing it, at the cost of
// forty per cent of the scene's bounce.)
assert(m90 <= 4.0,
       "turning the cube 90 degrees about its own axis barely changes its shading (" +
       m90.toFixed(2) + "/255, was 7.07 on the stock frame; the residual is the voxeliser)");
assert(m180 <= 4.0,
       "...nor 180 degrees (" + m180.toFixed(2) + "/255, was 4.07)");
assert(mback <= 1.0, "...and turning it back restores the picture EXACTLY, which is what\n       makes the numbers above a property of the scene and not of the clock (" +
       mback.toFixed(2) + ")");

// ---- half 1b: and the frame is a property of the WORLD, not of the head -----
// THERE IS NO PIXEL ARM FOR THIS ONE, AND THAT IS A MEASUREMENT RESULT.
// It matters — a frame built in view space turns about the normal with the
// camera, which is a swim while orbiting and a moving picture on every head
// turn in VR — and the patch guarantees it by construction: the frame is built
// from the normal taken to `passBuf.invViewMatCubemap`'s camera-independent
// space and composed back, so the cone directions are fixed in the world.
// Three designs were built and measured to assert it anyway, and all three are
// swamped:
//   * an ORBIT about the probed point moves the camera POSITION, so the GI
//     cascade re-centres with it, and that term is larger than the frame's
//     (measured +-30 degrees: stock 5.79, this patch 4.00, same-pose floor 2.00).
//   * a ROLL about the view axis leaves the position alone, but the scene's own
//     convergence still drifts (51.37 -> 53.37 -> 55.37 at one fixed pose).
//   * taking the shots BACK TO BACK with no frames between them does not fix
//     it, because editor.screenshot ITSELF advances the scene's GI: the same
//     pose, shot four times with nothing stepped, walked 51.37 -> 55.37. That
//     is a finding of its own and is reported rather than worked around here.
// WHAT DOES ASSERT IT is the selftest's second pose — a YAWED camera, settled,
// compared as 65,536 exact pixels — and this patch leaves it byte-identical to
// the build before it (`2aadbc10…` / `0f084cec…`, both poses). A frame that
// turned with the camera could not do that, and the first cut of this patch,
// which built the frame in view space, moved 63.8 % of that pose's pixels.

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
// The camera is 4 m from the face with a 45-degree vertical lens over 720 px,
// so the image centre is exactly 1.0 m up the wall and 0.62 is 0.6 m up. The
// LOW one is the assertion that matters: it is where a 2 m radius takes the
// most sunlight (-16/255 measured) and where a contact radius takes none.
var WALL = [ { x: 0.5, y: 0.5 }, { x: 0.5, y: 0.62 }, { x: 0.42, y: 0.62 } ];
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
       "ambient occlusion does not darken DIRECT sunlight 0.6 to 1.0 m above the floor (" +
       cut.toFixed(2) + "/255)");

// And the radius is what decides that: put it back to 2 m and the same probes
// lose light again — which is the fail-before, measured inside the suite so the
// bound above can never pass by the AO having quietly stopped working.
world.postFx({ ssaoRadius: 2.0 });
var wide = wallRead("sun_only_ao_2m");
var wideCut = worstMove(noAo, wide);
console.log("...at the old 2.0 m radius: " + wideCut.toFixed(2) + "/255");
assert(wideCut >= 10.0,
       "the AO march is alive and the radius is what confines it (2.0 m cuts " +
       wideCut.toFixed(2) + "/255 of the same sunlight)");
world.postFx({ ssaoRadius: 0.35 });

console.log("cube_shade: done");
