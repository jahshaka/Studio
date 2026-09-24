// gi.gather_default — THE GATHER IS THE DIFFUSE BY TIER (PHOTON-GATHER-1d, the
// rule T-A). Runs inside the real app, on the DEFAULT scene (a new project's
// ground, sky and sun), because the claim is about what a user gets without
// touching a row: world.gi's `gather` left at "auto", the Photon tier decides.
//
//   1. THE RULE, through the app's own path (the tier table -> the document ->
//      the mirror -> the engine's table): High gathers at 64 rays a probe and a
//      probe per 16x16 px, Epic at a probe per 8x8, Medium at 36 rays, Low not
//      at all.
//   2. AT HIGH THE FLOOR'S IRRADIANCE IS THE GATHER'S: the default ground's
//      picture with the row on "auto" against the same shot with
//      world.gi({gather:false}) — the A/B moves it, and both numbers are printed.
//   3. BACK TO AUTO IS THE SAME PICTURE (the gather is deterministic in a fresh
//      shot view: the frame index counts from the view's birth).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var proj = project.create("Gather Default " + Date.now());
assert(proj.length > 10, "project.create");
assert(world.rayTracing("auto") === "auto", "the project's ray row is auto");

// ---- 1. the rule ----------------------------------------------------------
function gatherAt(tier) {
    world.photon({ enabled: true, tier: tier });
    editor.frame(8);
    return world.giStatus().gather;
}
var rt = world.giStatus().rayQuery;
if (!rt || !rt.available) {
    console.log("ok: no ray queries on this machine — gi.gather_default has nothing to measure");
} else {
    var low = gatherAt("low");
    assert(low.on === false && low.running === false, "Low: the gather is OFF by tier " + J(low));
    var med = gatherAt("medium");
    assert(med.on === true && med.running === true && med.raysPerProbe === 36 && med.stride === 16,
           "Medium: on at 36 rays a probe, a probe per 16 px " + J({ rays: med.raysPerProbe, stride: med.stride }));
    var epic = gatherAt("epic");
    assert(epic.on === true && epic.running === true && epic.raysPerProbe === 64 && epic.stride === 8,
           "Epic: on at 64 rays, a probe per 8 px (the TABLE's row, not the SSR row) " +
           J({ rays: epic.raysPerProbe, stride: epic.stride }));
    var high = gatherAt("high");
    assert(high.on === true && high.running === true && high.raysPerProbe === 64 && high.stride === 16,
           "High: on at 64 rays, a probe per 16 px " + J({ rays: high.raysPerProbe, stride: high.stride }));
    assert(world.get().gi.gather === "auto", "...and the document's row is still auto (the tier decides)");

    // ---- 2. the floor, gather on (auto) against gather off -----------------
    editor.setCamera({ position: { x: 0, y: 3.0, z: 6 }, lookAt: { x: 0, y: 0, z: 0 } });
    // AT REST, in frames (the one settle predicate — the field's refinements,
    // the chain's settle and now the gather's history all counted).
    function settle() {
        var n = 0;
        for (; n < 2000 && !world.giStatus().giAtRest; ++n) editor.frame(1);
        return n;
    }
    console.log("   at rest after " + settle() + " frames");
    var PROBES = [];
    for (var py = 0; py < 3; ++py)
        for (var px = 0; px < 5; ++px) PROBES.push({ x: 0.1 + 0.2 * px, y: 0.72 + 0.1 * py });
    function floorMean(tag) {
        var s = editor.screenshot("gather_default_" + tag + ".png", 640, 360, PROBES, "plain");
        var sum = 0;
        for (var i = 0; i < s.probes.length; ++i) sum += (s.probes[i].r + s.probes[i].g + s.probes[i].b) / 3;
        return sum / s.probes.length;
    }
    var stOn = world.giStatus();
    assert(stOn.gather.running && stOn.gather.settled && stOn.giAtRest,
           "the viewport's gather runs and is SETTLED at rest (history age " +
           stOn.gather.historyAge + ", lighting age " + stOn.gather.lightingAge + ", N " +
           stOn.gather.settleFrames + ")");
    // editor.screenshot waits for its own shot view's history (giAtRest's
    // settled term): the shot is the settled gather's, not a two-frame estimate.
    var on = floorMean("on");
    assert(world.gi({ gather: false }), "world.gi({gather:false})");
    editor.frame(4);
    settle();
    var off = floorMean("off");
    assert(world.giStatus().gather.on === false, "...the row is off");
    console.log("   the default ground's mean over 15 floor probes: gather " + on.toFixed(3) +
                ", gather off " + off.toFixed(3) + " (codes)");
    assert(Math.abs(on - off) > 0.05,
           "AT HIGH THE FLOOR'S IRRADIANCE IS THE GATHER'S: the A/B moves the ground (" +
           on.toFixed(3) + " against " + off.toFixed(3) + ")");

    // ---- 3. back to auto: the same picture ---------------------------------
    assert(world.gi({ gather: "auto" }), "world.gi({gather:'auto'})");
    editor.frame(4);
    settle();
    var again = floorMean("again");
    assert(Math.abs(again - on) < 0.05,
           "back to auto is the same ground (" + again.toFixed(3) + " against " + on.toFixed(3) + ")");
}
project.close();
