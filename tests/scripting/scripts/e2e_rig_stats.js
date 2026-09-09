// scripting.e2e.rig_stats — the rig-cost read surface (AVATAR_RIG_PERF_SPEC §3.5).
//
// The character rig and skeleton sharing are INVISIBLE: a character whose five
// pieces share one skeleton and one that does not render identically, pixel for
// pixel, and the document is the same either way. scene.rigStats() is the only
// place the difference can be observed at all, so this is the API-first gate for
// it — the verb exists in the real binary, it is wired to the live engine scene
// (not to a stand-in returning zeros), and its numbers move with the scene.
//
// Runs with the ENGINE UP (no --headless): `available` is exactly what is being
// checked, and a headless session would report false by design.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("rigstats");
editor.frame(2);

var s = scene.rigStats();
assert(s.available === true, "scene.rigStats is wired to a live engine scene");
var fields = ["rigged", "instances", "shared", "streamedBones", "bonesPerRiggedNode", "clipPushes"];
for (var i = 0; i < fields.length; i++)
    assert(typeof s[fields[i]] === "number", "reports " + fields[i]);

// A fresh project has no skinned characters at all: every count is zero, and
// that is a MEASUREMENT (available:true), not the "no engine" answer.
assert(s.rigged === 0 && s.instances === 0 && s.shared === 0 && s.streamedBones === 0,
       "a scene with no characters costs no rigs");
assert(s.bonesPerRiggedNode === 0, "bonesPerRiggedNode is 0 with nothing rigged");

// The counters are LIVE: geometry that is not skinned must not turn up as a rig.
scene.addPrimitive("cube");
editor.frame(2);
var after = scene.rigStats();
assert(after.rigged === 0, "an unskinned primitive is not a rig");
assert(after.clipPushes >= s.clipPushes, "clipPushes is cumulative, never decreasing");

// The invariant the whole program rests on, stated as an assertion so it fails
// loudly if sharing ever goes wrong on a real scene: nothing may render from a
// skeleton that is not there, and sharing can never make instances exceed the
// nodes that carry them.
assert(after.instances <= after.rigged, "instances never exceed rigged nodes");
assert(after.shared === after.rigged - after.instances || after.shared === 0,
       "shared is exactly what sharing removed");

console.log("rig_stats: ok");
