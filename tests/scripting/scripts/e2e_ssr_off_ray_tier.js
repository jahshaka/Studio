// gi_verbs.ssr_off_ray_tier — THE SSR ROW AT A RAY TIER (D4-PHOTON-TIERS fix round, F2/F3).
//
// The rule: a document's SSR OFF is honoured at every tier — off means off, no trace and no
// march — and where the scene's reflections are traced the row offers exactly two states,
// "Off" and one DISABLED "Traced" (the march's lq/hq means nothing there). Held here:
//   1. at High with rays, the SSR row ON: the reflection trace runs (giStatus().rayQuery.reflect,
//      rays > 0) — the control that makes 2 mean something;
//   2. the row OFF at the same tier: the trace does NOT run (reflect false, 0 rays);
//   3. world.modeTable()'s SSR row — the entries the World panels offer, read from the same
//      worldmodes::comboItems — lists exactly two: "off" and a disabled "traced".
// On a machine with no ray query nothing is traced and the row keeps its three march entries:
// then 1 and 3 reduce to that, and 2 still holds.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var proj = project.create("SSR off at a ray tier " + Date.now());
assert(proj.length > 10, "project.create");
// A mirror in view, so a trace has something to answer.
var mirror = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: -3 }, scale: { x: 3, y: 2, z: 0.2 } });
assert(material.set(mirror, { baseColor: "#ffffff", metallic: 1.0, roughness: 0.0 }), "the mirror");
world.photon({ enabled: true, tier: "high" });
assert(world.rayTracing("auto") === "auto", "world.rayTracing(auto)");
editor.setCamera({ position: { x: 0, y: 1.5, z: 4 }, lookAt: { x: 0, y: 1, z: -3 } });

function ssrRow() {
    var rows = world.modeTable().rows;
    for (var i = 0; i < rows.length; ++i) if (rows[i].id === "ssr") return rows[i];
    return null;
}

world.override({ id: "ssr", value: "hq" });
editor.frame(60);
var rq = world.giStatus().rayQuery;
var traces = rq.available && rq.enabled && world.giStatus().probeGridByRays;
console.log("rayQuery with the row ON: " + J(rq) + ", traced tier " + traces);
if (traces)
    assert(rq.reflect === true && rq.reflectRays > 0,
           "CONTROL: at High with rays and the SSR row on, the reflection trace runs (" + rq.reflectRays + " rays)");

var row = ssrRow();
assert(row && row.options, "world.modeTable() lists the SSR row's entries");
console.log("SSR row entries: " + J(row.options));
if (traces) {
    assert(row.options.length === 2, "at a ray tier the SSR row offers exactly two entries (" + row.options.length + ")");
    assert(row.options[0].id === "off" && row.options[0].label === "Off" && row.options[0].enabled !== false,
           "the first is 'Off', and it can be chosen");
    assert(row.options[1].id === "traced" && row.options[1].label === "Traced" && row.options[1].enabled === false,
           "the second is ONE disabled 'Traced' (it names the state, it cannot be chosen)");
} else {
    assert(row.options.length === 3, "no traced reflections here: the three march entries");
}

world.override({ id: "ssr", value: "off" });
assert(world.settings().ssr.valueId === "off", "the SSR row is OFF");
editor.frame(60);
rq = world.giStatus().rayQuery;
console.log("rayQuery with the row OFF: " + J(rq));
assert(rq.reflect === false && rq.reflectRays === 0,
       "OFF IS HONOURED AT A RAY TIER: no reflection trace runs (reflect " + rq.reflect + ", " + rq.reflectRays + " rays)");
"ok";
