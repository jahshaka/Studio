// app.selection_outline — the isBuiltIn-suppression regression gate.
//
// From 2026-08-31 to 2026-09-06 every Add-menu primitive silently lost its
// selection outline: the "no outline for the built-in GROUND" rule was
// implemented as `highlight->isBuiltIn`, which addBuiltinPrimitive sets on
// every cube/sphere/capsule (and everything sample scenes are made of).
// Selection, gizmo and properties kept working, so the loss read as "clicks
// are broken" and cost a day of misattributed reports.
//
// The gate: with a BUILTIN-flagged primitive selected, at least one probe of
// the screenshot changes versus deselected (the outline shell is a scene node
// and renders in the screenshot view). The ground keeps NO outline — that
// exclusion is the surviving owner ask.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// A probe lattice across the frame; the outline band lands on several of
// these wherever default framing puts the cube.
var probes = [];
for (var px = 0.2; px <= 0.81; px += 0.1)
    for (var py = 0.2; py <= 0.81; py += 0.1)
        probes.push({ x: px, y: py });

function probeColours(tag) {
    var r = editor.screenshot("shot_" + tag + ".png", 512, 512, probes);
    assert(r && r.probes && r.probes.length === probes.length, tag + " screenshot with probes");
    return r.probes.map(function (p) { return p.r + "," + p.g + "," + p.b; });
}
function countDiff(a, b) {
    var n = 0;
    for (var i = 0; i < a.length; ++i) if (a[i] !== b[i]) ++n;
    return n;
}

// The GIZMO renders in screenshots too, so selected-vs-deselected deltas mix
// gizmo and highlight. The airtight probe: HOLD the selection and toggle only
// the highlight STYLE (hull outline <-> wireframe). The gizmo is identical in
// both shots and cancels; any probe delta is pure highlight. Under the
// isBuiltIn bug (highlight suppressed upstream) both styles drew nothing and
// the delta was zero.
function styleDelta(nodeId, tag) {
    editor.select(nodeId); editor.frame(2);           // shell syncs on the tick
    editor.setOverlays({ selectionWireframe: false }); editor.frame(2);
    var hull = probeColours(tag + "_hull");
    editor.setOverlays({ selectionWireframe: true }); editor.frame(2);
    var wf = probeColours(tag + "_wf");
    editor.setOverlays({ selectionWireframe: false }); editor.frame(2);
    return countDiff(hull, wf);
}

project.create("outline-gate");
var cube = scene.addPrimitive("cube", { position: [0, 1, 0] });

var d = styleDelta(cube, "cube");
assert(d >= 1, "a builtin primitive's highlight draws (style toggle moved " + d + " probes)");

// The surviving owner ask: the GROUND gets no highlight in either style, so
// the style toggle must change nothing (its gizmo is in both shots).
var ground = scene.nodes().filter(function (n) { return n.name === "Ground"; })[0];
assert(ground, "default scene has a Ground");
var gd = styleDelta(ground.id, "ground");
assert(gd === 0, "ground selection draws no highlight in either style (moved " + gd + ")");

console.log("selection_outline: PASS");
