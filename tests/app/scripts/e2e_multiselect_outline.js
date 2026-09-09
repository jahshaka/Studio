// app.multiselect_outline — EVERY MEMBER OF THE SELECTION IS OUTLINED
// (EDITOR_MULTISELECT_SPEC §2.3 / D4(a), gate table §5).
//
// The mirror's highlight walk was single-rooted: one node in, one pooled set of
// outline shells out. It now walks N roots, and the claim this suite makes is
// PIXELS: with two cubes selected, BOTH cubes' bands move — not just the
// primary's. A set whose secondaries quietly stopped drawing would still pass
// every verb test in the program, which is exactly why this one exists.
//
// THE PROBE TRICK is app.selection_outline's, for the same reason: the gizmo
// renders into screenshots too, so selected-vs-deselected deltas mix gizmo and
// highlight. Holding the selection and toggling only the highlight STYLE (hull
// outline <-> wireframe) leaves the gizmo identical in both shots, so every
// probe delta is pure highlight. The two cubes are placed left and right of
// the origin and probed in their OWN half of the frame, so "the second cube is
// outlined" cannot be satisfied by the first one's pixels.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function lattice(x0, x1) {
    var probes = [];
    for (var px = x0; px <= x1 + 0.001; px += 0.05)
        for (var py = 0.25; py <= 0.76; py += 0.05)
            probes.push({ x: px, y: py });
    return probes;
}
var leftProbes  = lattice(0.10, 0.45);
var rightProbes = lattice(0.55, 0.90);
var allProbes   = leftProbes.concat(rightProbes);

function probeColours(tag) {
    var r = editor.screenshot("shot_" + tag + ".png", 640, 480, allProbes);
    assert(r && r.probes && r.probes.length === allProbes.length, tag + " screenshot with probes");
    return r.probes.map(function (p) { return p.r + "," + p.g + "," + p.b; });
}
// Counts differing probes in the left half and the right half separately.
function countDiff(a, b) {
    var left = 0, right = 0;
    for (var i = 0; i < a.length; ++i) {
        if (a[i] === b[i]) continue;
        if (i < leftProbes.length) ++left; else ++right;
    }
    return { left: left, right: right };
}
/// Holds whatever is selected and toggles ONLY the outline style.
function styleDelta(tag) {
    editor.frame(2);
    editor.setOverlays({ selectionWireframe: false }); editor.frame(2);
    var hull = probeColours(tag + "_hull");
    editor.setOverlays({ selectionWireframe: true }); editor.frame(2);
    var wf = probeColours(tag + "_wf");
    editor.setOverlays({ selectionWireframe: false }); editor.frame(2);
    return countDiff(hull, wf);
}

project.create("multiselect-outline");

// Two cubes, well apart on X, at eye height. The editor camera looks down -Z
// from +Z, so -X lands in the LEFT half of the frame and +X in the right.
var left  = scene.addPrimitive("cube", { position: [-2.5, 1, 0] });
var right = scene.addPrimitive("cube", { position: [ 2.5, 1, 0] });
assert(left && right, "two cubes added");

// GI is progressive and the geometry just changed; let it settle before any
// pixel comparison (the same reason app.selection_outline waits).
editor.frame(40);

// ---- one selected: only ITS half moves -------------------------------------
editor.select(left);
var one = styleDelta("one");
console.log("one selected -> left " + one.left + ", right " + one.right);
assert(one.left >= 1, "the selected cube's own half is outlined (" + one.left + " probes)");
assert(one.right === 0,
       "the UNSELECTED cube's half draws no highlight (" + one.right + " probes)");

// ---- both selected: BOTH halves move ---------------------------------------
editor.select([left, right]);
assert(editor.selectionSet().length === 2, "both cubes are selected");
var both = styleDelta("both");
console.log("both selected -> left " + both.left + ", right " + both.right);
assert(both.left >= 1,
       "the PRIMARY is outlined (" + both.left + " probes in its half)");
assert(both.right >= 1,
       "the SECONDARY is outlined too — the mirror walks N roots (" +
       both.right + " probes in its half)");

// ---- deselect: neither half moves -------------------------------------------
editor.selectNone();
var none = styleDelta("none");
console.log("none selected -> left " + none.left + ", right " + none.right);
assert(none.left === 0 && none.right === 0,
       "with nothing selected the style toggle changes nothing (" +
       none.left + "/" + none.right + ")");

console.log("multiselect_outline: PASS");
