// scripting.e2e.lamps_unshadowed (PHOTON-7 10-4 fix round, the one read's F7) — A LAMP NOTHING SHADOWS IS NAMED.
//
// A point or spot light the shadow maps do not hold is shadowed by a trace - a ray at the ray tier, the GI voxels'
// march where the GI is on. With GI off and no ray tracing nothing traces it, and its light passes through walls: the
// `lamps.unshadowed` scene issue names each such lamp (SceneIssues::scan, from the view's unmapped list -
// world.shadowStatus().unmapped). THE LAW: the World's ray tracing off, GI off, the shadow-map budget at 2 and six
// point lamps: one issue per unmapped lamp, naming exactly those lamps; then GI on: none (the voxels shadow them).
// RED BEFORE F7: no such issue existed. GUI, engine up (the unmapped list is the live view's).
function assert(c, m) { if (!c) throw new Error("assert failed: " + m); console.log("ok: " + m); }
function J(v) { return JSON.stringify(v); }

world.rayTracing("off");
world.gi({ mode: "off" });
world.shadows({ mapBudget: 2 });
var lamps = [];
for (var k = 0; k < 6; ++k)
    lamps.push(scene.addLight("point", { position: [4.0 * Math.cos(k), 2.0, 4.0 * Math.sin(k)] }));
editor.frame(8, 1 / 60);
world.shadowStatus();          // (arms the counters)
editor.frame(4, 1 / 60);
var unmapped = world.shadowStatus().unmapped;
console.log("unmapped: " + J(unmapped));
assert(unmapped.length >= 3, "six lamps over a budget of two leave lamps without a map (" + unmapped.length + ")");
editor.checkScene();
var issues = editor.issues().filter(function (i) { return i.kind === "lamps.unshadowed"; });
console.log("issues: " + J(issues.map(function (i) { return i.nodeName + " — " + i.message; })));
var named = issues.map(function (i) { return i.node; }).sort();
assert(J(named) === J(unmapped.slice().sort()),
       "GI off, no rays: one lamps.unshadowed issue per unmapped lamp, naming exactly those (" + issues.length + ")");
world.gi({ mode: "vct" });
editor.frame(8, 1 / 60);
editor.checkScene();
var after = editor.issues().filter(function (i) { return i.kind === "lamps.unshadowed"; });
assert(after.length === 0, "GI on: the voxels shadow every lamp, no issue (" + after.length + ")");
console.log("PASS");
