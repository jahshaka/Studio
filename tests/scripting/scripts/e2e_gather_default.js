// gi.gather_default — THE GATHER IS THE DIFFUSE BY TIER (PHOTON-GATHER-1d, the
// rule T-A). Runs inside the real app, on the DEFAULT scene (a new project's
// ground, sky and sun), because the claim is about what a user gets without
// touching a row: world.gi's `gather` left at "auto", the Photon tier decides.
//
//   1. THE RULE, through the app's own path (the tier table -> the document ->
//      the mirror -> the engine's table): High gathers at 64 rays a probe and a
//      probe per 16x16 px, Epic at a probe per 8x8, Medium at 36 rays, Low not
//      at all.
//   2. AT HIGH THE FLOOR'S IRRADIANCE IS THE GATHER'S: the default ground,
//      beside a sunlit white wall, with the row on "auto" against the same shot
//      with world.gi({gather:false}) — the A/B moves it, and both numbers are
//      printed.
//   3. BACK TO AUTO IS THE SAME PICTURE (the gather is deterministic in a fresh
//      shot view: the frame index counts from the view's birth).
//
// THE FIXTURE HAS A BOUNCE SURFACE (FOG-ATMO-1 fix round). On the OPEN default
// floor the gather and the irradiance field agree: the floor sees sky and sun
// and nothing near, so the field's probes already hold its irradiance —
// measured in float radiance, gather on/off 44.9965 against 44.9918 codes
// (0.005), on base and tip alike; "the A/B moves the ground" was never true
// there, and the 8-bit bytes passed on rounding. What the gather is FOR is the
// light a near surface throws on the floor at a scale the field's probes cannot
// hold: a sunlit white wall standing just behind the probe patch moves the
// patch by ~1.5 codes (44.53 against 42.96), thirty times the bar. That is the
// fixture.
//
// THE INSTRUMENT IS THE FLOAT RADIANCE (FOG-ATMO-1 fix round). The bar is 0.05
// of a code and the plain bytes are whole codes, mean-truncated per probe: the
// gather's effect on this floor is a fraction of a code, so the 8-bit A/B
// measured the ROUNDING (base 0.066 codes apart, one sky-colour change later
// 0.000). The probes now read editor.screenshot's {radiance: true} — the
// linear value before the 8-bit store — expressed in the same code units
// (x 255), so the bar is unchanged and sits on the gather's own values.

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
    // The bounce surface (see the header): a sunlit white wall behind the
    // probe patch, facing the camera, 8 m wide and 3 m tall.
    var wall = scene.addPrimitive("cube", { position: { x: 0, y: 1.5, z: -0.5 },
                                            scale: { x: 8, y: 3, z: 0.2 } });
    assert(!!wall && material.set(wall, { baseColor: "#ffffff", roughness: 0.9 }),
           "the bounce wall stands behind the probe patch");
    editor.selectNone();
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
        var s = editor.screenshot("gather_default_" + tag + ".png", 640, 360, PROBES, "plain",
                                  { radiance: true });
        var sum = 0, bytes = 0;
        for (var i = 0; i < s.probes.length; ++i) {
            var q = s.probes[i].radiance;
            sum += 255 * (q.r + q.g + q.b) / 3;
            bytes += (s.probes[i].r + s.probes[i].g + s.probes[i].b) / 3;
        }
        console.log("   " + tag + ": radiance " + (sum / s.probes.length).toFixed(4) +
                    " codes, the 8-bit bytes " + (bytes / s.probes.length).toFixed(3));
        return sum / s.probes.length;
    }
    var stOn = world.giStatus();
    assert(stOn.gather.running && stOn.gather.settled && stOn.giAtRest,
           "the viewport's gather runs and is SETTLED at rest (history age " +
           stOn.gather.historyAge + ", rest frames " + stOn.gather.restFrames + ", N " +
           stOn.gather.settleFrames + ")");
    // editor.screenshot waits for its own shot view's history (giAtRest's
    // settled term): the shot is the settled gather's, not a two-frame estimate.
    var on = floorMean("on");
    assert(world.gi({ gather: false }), "world.gi({gather:false})");
    editor.frame(4);
    settle();
    var off = floorMean("off");
    assert(world.giStatus().gather.on === false, "...the row is off");
    console.log("   the default ground's mean over 15 floor probes: gather " + on.toFixed(4) +
                ", gather off " + off.toFixed(4) + " (codes of float radiance)");
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
