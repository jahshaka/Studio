// pool.gi_verbs_b — gather_hits_verbs (FAR-SUN-1): THE TWO INSTRUMENT VERBS FROM A SCRIPT, the
// API-first law's test for world.gatherHits and world.giVoxelRead (photon.view drove them until
// TESTING-SPEED-B1 deleted it as a whole-scene proof; the gi.far_sun_* engine rows measure the
// instrument underneath). On the Basic template at the High tier:
//   1. world.gatherHits answers NOT READY without the arm "gather.hitsReadback", naming why;
//   2. with the arm, it answers READY with its fields, rays > 0 and probes > 0 where the scene has
//      rays, every counted ray named by one source (the shares add to one), {rays: N} returns N
//      samples with their fields (a normal of three components), and off again it is not ready;
//   3. world.giVoxelRead answers cascade 0's cell, origin and multiplier, and at the camera's
//      point the texel's index and fields; a point outside the volume answers no index;
//      a cascade that does not exist answers available: false.
// Frames, never time: the waits count editor.frame calls.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

assert(project.create("Gather Hits Verbs " + Date.now(), { template: "basic" }).length > 10,
       "project.create({template: \"basic\"})");
world.gi({ tier: "high" });
editor.frame(6);
var gs = world.giStatus();
if (!gs.gather || !gs.gather.on) {
    // a machine with no ray queries: the verbs still answer, never ready (the gather is off)
    var off = world.gatherHits();
    assert(off.ready === false, "no gather on this machine: world.gatherHits is not ready (" + off.reason + ")");
    "ok";
} else {
    // ---- 1. not ready without the arm --------------------------------------------------
    var g0 = world.gatherHits();
    assert(g0.ready === false && typeof g0.reason === "string" && g0.reason.length > 0,
           "without the arm world.gatherHits is not ready, naming why (" + g0.reason + ")");

    // ---- 2. ready with the arm, every field ---------------------------------------------
    assert(engine.arm("gather.hitsReadback", 1) === 1, "the arm gather.hitsReadback is set");
    var g = null;
    for (var i = 0; i < 40 && !(g && g.ready); ++i) { editor.frame(2); g = world.gatherHits({ rays: 8 }); }
    assert(g && g.ready === true, "with the arm world.gatherHits is ready");
    ["frame", "stride", "octRes", "probes", "rays", "sources", "sunlitUp", "shadedUp", "other", "samples"].forEach(
        function (k) { assert(g[k] !== undefined, "world.gatherHits answers '" + k + "'"); });
    assert(g.rays > 0 && g.probes > 0, "the Basic scene's probes trace rays (" + g.rays + " rays, " + g.probes + " probes)");
    var share = 0, count = 0;
    ["card", "voxel", "sky", "decode", "decodeFar", "dropped"].forEach(function (k) {
        assert(g.sources[k] && typeof g.sources[k].count === "number", "the source '" + k + "' is answered");
        share += g.sources[k].share; count += g.sources[k].count;
    });
    assert(Math.abs(share - 1) < 1e-6 && count === g.rays,
           "every counted ray names one source (" + count + " of " + g.rays + ", shares " + share.toFixed(6) + ")");
    assert(g.samples.length === Math.min(8, g.rays), "{rays: 8} returns eight rays one by one");
    var s = g.samples[0];
    ["source", "cascade", "cell", "radiance", "distance", "hit", "normal", "sun"].forEach(
        function (k) { assert(s[k] !== undefined, "a sample answers '" + k + "'"); });
    assert(s.normal.length === 3 && s.hit.length === 3 && s.radiance.length === 3,
           "a sample's normal, hit and radiance have three components (" + J(s.normal) + ")");
    engine.arm("gather.hitsReadback", 0);
    editor.frame(4);
    assert(world.gatherHits().ready === false, "the arm off: world.gatherHits is not ready again");

    // ---- 3. the voxel read --------------------------------------------------------------
    var cam = editor.camera().position;
    var vr = world.giVoxelRead({ cascade: 0, points: [cam, { x: 1e6, y: 1e6, z: 1e6 }] });
    assert(vr.available === true && vr.cell > 0 && vr.origin.length === 3 && vr.multiplier > 0,
           "world.giVoxelRead answers cascade 0's cell, origin and multiplier (cell " + vr.cell + ")");
    assert(vr.points.length === 2, "one answer per point");
    var p0 = vr.points[0];
    assert(p0.index && p0.index.length === 3, "the camera's point is in cascade 0 (index " + J(p0.index) + ")");
    ["light", "albedo", "normal", "coverageP", "coverageN"].forEach(
        function (k) { assert(p0[k] !== undefined, "the texel answers '" + k + "'"); });
    assert(vr.points[1].index === null || vr.points[1].index === undefined,
           "a point outside the volume answers no index");
    var none = world.giVoxelRead({ cascade: 99, points: [cam] });
    assert(none.available === false, "a cascade that does not exist answers available: false");
}
"ok";
