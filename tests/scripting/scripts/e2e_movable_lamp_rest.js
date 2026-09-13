// scripting.e2e.movable_lamp_rest — A MOVABLE LAMP THAT STOPS MOVING GETS ITS
// BOUNCES BACK (round-2 review F1 of lane ENGINE-4; REALTIME_REFLECTIONS_SPEC
// §3.3 O2 is the path).
//
// A lamp the author marked Movable does NOT arm the settle: the mirror
// re-injects its light into the voxels on a cadence and nothing ever re-solves,
// which is the whole point of O2 (a torch swinging for ever must not cost a
// re-solve). The engine's cheap injection runs ONE bounce and the coarse ray
// march while the thing is moving — and the frame the user is left looking at
// when it stops has to be the one the full count would have produced, so the
// mirror owes exactly one `inMotion = false` tick after the motion ends.
// Without it a three-bounce room stayed lit at one bounce indefinitely.
//
// Measured end to end, through the real mirror: the same lamp pose reached two
// ways — jumped there and fully re-solved (the reference), and TRAVELLED there
// over 60 frames and left to rest — must give the same picture.
//
// VCT without probes, over one sealed room that never changes shape, so the
// only thing that can differ between the two runs is the injection itself.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("Movable Lamp Rest " + Date.now());

// A closed white room: every extra bounce is another round of light off its
// walls, which is what makes the two pictures distinguishable at all.
function slab(name, px, py, pz, sx, sy, sz) {
    var id = scene.addPrimitive("cube", { position: { x: px, y: py, z: pz } });
    node.setProperty(id, "name", name);
    node.transform(id, { scale: { x: sx / 2, y: sy / 2, z: sz / 2 } });
    return id;
}
var ids = scene.nodes();
for (var i = 0; i < ids.length; ++i)
    if (ids[i].name === "Ground") node.setProperty(ids[i].id, "visible", false);
slab("Floor",   0, -0.25, 0, 12, 0.5, 12);
slab("Roof",    0,  6.25, 0, 12, 0.5, 12);
slab("WallW",  -6,  3,    0, 0.5, 6,  12);
slab("WallE",   6,  3,    0, 0.5, 6,  12);
slab("WallS",   0,  3,   -6, 12,  6,  0.5);
slab("WallN",   0,  3,    6, 12,  6,  0.5);

var lamp = scene.addLight("point", { position: { x: -3, y: 4.5, z: 0 } });
node.setProperty(lamp, "name", "Torch");
node.setProperty(lamp, "distance", 24);
node.setProperty(lamp, "intensity", 0.15);
assert(node.setProperty(lamp, "mobility", "movable") === true, "the lamp is marked Movable");
assert(node.mobility(lamp).resolved === "movable", "...and the renderer holds it as a mover");

// BOUNCE LIGHT OR NOTHING, and it now takes two statements instead of one
// (SKY_LIGHT_SPEC.md §2). Ambient is the SKY LIGHT, so "no ambient" is "no Sky
// Light in the scene"; and a colour sky is a REAL sky now, with an environment
// cubemap every glossy surface in this sealed room would reflect — which is a
// second light source this suite's 1/255 determinism check cannot afford (it
// measured 9/255 of drift with one). A BLACK sky is the old "a colour sky is
// not an environment" behaviour, said out loud.
world.sky("color", { color: "#000000" });
// ...and the ROOM IS LIT BY THE TORCH AND NOTHING ELSE. The default scene's own
// lights were never this suite's subject; they were simply left in place, and
// what they happened to be changed under it (the template's Point Light became
// a Sky Light, §5). Say it outright instead: every light but the Torch goes.
scene.nodes().forEach(function (n) {
    if (node.info(n.id).type !== "light") return;
    if (n.id === lamp) return;
    node.remove(n.id);
});
assert(world.gi({ mode: "vct", quality: "medium", bounces: 4 }) === true,
       "VCT at the full bounce count, over the renderer's fit to this sealed room");
editor.setCamera({ position: { x: 0, y: 3, z: 5 }, lookAt: { x: 0, y: 1, z: -1 } });
editor.frame(90);

var probes = [{x:0.5,y:0.75},{x:0.3,y:0.6},{x:0.7,y:0.6},{x:0.5,y:0.35},{x:0.2,y:0.8},{x:0.8,y:0.8}];
function shot(name) { return editor.screenshot(name + ".png", 256, 256, probes, "plain"); }

// ---- THE REFERENCE: the lamp jumped to its final pose and fully re-solved ---
node.transform(lamp, { position: { x: 3, y: 4.5, z: 0 } });
world.refreshGi();
editor.frame(30);
var reference = shot("lamp_rest_reference");
var refSolves = editor.mirrorStats().giRefreshes;

// ---- AND THE SAME POSE, TRAVELLED TO --------------------------------------
node.transform(lamp, { position: { x: -3, y: 4.5, z: 0 } });
world.refreshGi();
editor.frame(30);
var before = editor.mirrorStats();
for (var f = 1; f <= 60; ++f) {                 // 60 frames of travel
    node.transform(lamp, { position: { x: -3 + 0.1 * f, y: 4.5, z: 0 } });
    editor.frame(1);
}
editor.frame(40);                               // ...and let it rest
var after = editor.mirrorStats();
var rested = shot("lamp_rest_travelled");

console.log("light-only re-injects during and after the travel: " +
            (after.giLightRefreshes - before.giLightRefreshes) +
            " (of them AT REST: " + (after.giLightRefreshesAtRest - before.giLightRefreshesAtRest) +
            "), full re-solves: " + (after.giRefreshes - before.giRefreshes));
assert(after.giLightRefreshes - before.giLightRefreshes >= 2,
       "the mirror re-injected on its cadence while the lamp moved");
assert(after.giRefreshes === before.giRefreshes,
       "and NOT ONE full re-solve happened — that is O2, and it still holds");
// THE FIX ITSELF. Every tick taken while the lamp was moving injected ONE
// bounce; exactly one tick after it stopped must have run the scene's full
// count, and nothing else in this path ever will. This is the assertion that
// fails on the old code (measured: 0 at-rest ticks), and the pixel comparison
// below is its second reading — deliberately, because the two pictures differ
// by about 1/255 in a room like this and a probe test alone could not carry it.
assert(after.giLightRefreshesAtRest - before.giLightRefreshesAtRest === 1,
       "and exactly ONE of those injections was the at-rest one, at the full bounce count");

var worst = 0, worstProbe = 0;
for (var p = 0; p < probes.length; ++p) {
    var a = reference.probes[p], b = rested.probes[p];
    var d = Math.max(Math.abs(a.r - b.r), Math.abs(a.g - b.g), Math.abs(a.b - b.b));
    if (d > worst) { worst = d; worstProbe = p; }
}
console.log("travelled vs re-solved: worst probe " + worstProbe + " differs by " + worst + "/255");
assert(worst <= 1,
       "a lamp that travelled and came to rest leaves the full-count picture (within 1/255)");
true
