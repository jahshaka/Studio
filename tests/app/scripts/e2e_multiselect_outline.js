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
//
// MEASURED (Xvfb 1920x1080, RTX 4080, 2026-09-09 — the numbers the thresholds
// below are set from; no tuning was needed, the separation was clean first
// run): one selected -> left 8, right 0 · both selected -> left 8, right 9 ·
// nothing selected -> 0 / 0. The assertions ask for >= 1 on the positives and
// exactly 0 on the negatives, so an eight-fold margin covers frame-to-frame
// GI residue while a secondary that stopped drawing (right 9 -> 0) fails
// outright.

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

// ---- THE PRIMARY IS A DIFFERENT COLOUR (D4 b) -------------------------------
// The claim: in a MULTI-selection the primary's outline carries
// `outlinePrimaryColor` and every other member carries `outlineColor` — and
// with a SINGLE selection the primary colour is not used at all, so a one-node
// selection draws exactly as it did before the colour existed.
//
// THE PROBE TRICK HERE is NOT the style toggle: it is holding the selection
// AND the secondary colour fixed and changing ONLY `primaryColor`. That leaves
// the gizmo, the grid, the lighting and the secondary's band bit-for-bit
// identical between the two shots, so every differing probe is the PRIMARY's
// band and nothing else. Changing which node is primary would move the gizmo
// (it pivots on the primary) and poison exactly that.
//
// The two colours are opposite channels on purpose: "differs" then means a
// real colour change and not a rounding wobble in the post chain.
//
// TWO THINGS HAD TO CHANGE ABOUT THE MEASUREMENT, both learned on the rig
// (2026-09-09) after this section first read 0/0 against WORKING code:
//
//   * THE LATTICE ABOVE IS FAR TOO COARSE FOR A COLOUR CLAIM. 0.05 of a
//     640-wide frame is 32 pixels between probes, and the outline is a band
//     one or two pixels wide — so a pure recolour of the band moves NO probe
//     at all. (The style toggle above still works on it because hull ->
//     wireframe repaints the whole cube face, not just the band.) The colour
//     probes use a 0.008 lattice, ~5 px, which lands on the band repeatedly.
//   * AND THE BAND IS WIDENED FOR THE MEASUREMENT: outline width 30 instead
//     of the default 3 (the mirror's hull scale is 1 + width/150, so 1.2
//     instead of 1.02). Measuring a feature is allowed to make the feature
//     big; the width is a persisted preference this suite already owns, and
//     the numbers below are recorded at both widths so the margin is honest.
//
// MEASURED (Xvfb 1920x1080, RTX 4080, 2026-09-09), primary colour red ->
// green with the secondary held red, dense lattice:
//   width  3, two selected, primary = left  -> left  64, right   0
//   width 30, two selected, primary = left  -> left 200, right   0
//   width 30, two selected, primary = right -> left   0, right 192
//   width 30, ONE selected                  -> left   0, right   0
// The assertions ask for >= 8 on the positive and exactly 0 on the negatives,
// so the margin is 24-fold while a secondary that wrongly took the primary
// colour (0 -> ~200) fails outright. The last row is the byte-for-byte claim:
// with one node selected the primary colour is not used and moves NOTHING.
function denseLattice(x0, x1) {
    var probes = [];
    for (var px = x0; px <= x1 + 0.0001; px += 0.008)
        for (var py = 0.20; py <= 0.81; py += 0.008)
            probes.push({ x: px, y: py });
    return probes;
}
var denseLeft  = denseLattice(0.10, 0.45);
var denseRight = denseLattice(0.55, 0.90);
var denseAll   = denseLeft.concat(denseRight);

function denseColours(tag) {
    var r = editor.screenshot("shot_" + tag + ".png", 640, 480, denseAll);
    assert(r && r.probes && r.probes.length === denseAll.length, tag + " dense screenshot");
    return r.probes.map(function (p) { return p.r + "," + p.g + "," + p.b; });
}
function denseDiff(a, b) {
    var left = 0, right = 0;
    for (var i = 0; i < a.length; ++i) {
        if (a[i] === b[i]) continue;
        if (i < denseLeft.length) ++left; else ++right;
    }
    return { left: left, right: right };
}
function outlineDelta(tag, first, second) {
    editor.setOutline(first);  editor.frame(4);
    var a = denseColours(tag + "_a");
    editor.setOutline(second); editor.frame(4);
    var b = denseColours(tag + "_b");
    return denseDiff(a, b);
}

// A red secondary throughout; the primary flips red -> green. Width 30 so the
// band is thick enough to measure (see above).
var RED = { width: 30, color: "#ff0000", primaryColor: "#ff0000" };
var GRN = { width: 30, color: "#ff0000", primaryColor: "#00ff00" };

// primary = left (the first entry of the set).
editor.select([left, right]);
assert(editor.selectionSet().length === 2, "both cubes selected, primary = left");
var pl = outlineDelta("primary_left", RED, GRN);
console.log("primary=left, primaryColor red->green -> left " + pl.left + ", right " + pl.right);
assert(pl.left >= 8,
       "the PRIMARY's band takes the primary colour (" + pl.left + " probes changed)");
assert(pl.right === 0,
       "the SECONDARY's band is untouched by the primary colour (" + pl.right + " probes changed)");

// primary = right — the same set, the other way round. This is the half of the
// claim that a hard-coded "the first shell is brighter" would fail.
editor.select([right, left]);
assert(editor.selectionSet().length === 2, "both cubes selected, primary = right");
var pr = outlineDelta("primary_right", RED, GRN);
console.log("primary=right, primaryColor red->green -> left " + pr.left + ", right " + pr.right);
assert(pr.right >= 8,
       "the primary colour follows the PRIMARY, not the list order (" + pr.right + " probes)");
assert(pr.left === 0,
       "the other member stays on the secondary colour (" + pr.left + " probes)");

// A SINGLE selection ignores the primary colour entirely: byte-for-byte.
editor.select(left);
assert(editor.selectionSet().length === 1, "one cube selected");
var one2 = outlineDelta("single", RED, GRN);
console.log("single selection, primaryColor red->green -> left " + one2.left +
            ", right " + one2.right);
assert(one2.left === 0 && one2.right === 0,
       "with ONE node selected the primary colour changes no pixel at all (" +
       one2.left + "/" + one2.right + ") — there is nothing to contrast with");

// And the verb reports what it wrote, including the DERIVED primary.
editor.setOutline({ width: 3, color: "#3498db", primaryColor: null });
var st = editor.outline();
assert(st.primaryColorStored === false, "primaryColor: null clears the stored choice");
assert(st.color === "#3498db" && st.width === 3,
       "colour and width round-trip (" + st.color + ", " + st.width + ")");
assert(st.primaryColor !== st.color,
       "the derived primary is LIGHTER than the outline colour (" + st.primaryColor + ")");
assert(editor.overlays().outlinePrimaryColor === st.primaryColor,
       "editor.overlays() reports the same primary colour");

// ---- deselect: neither half moves -------------------------------------------
editor.selectNone();
var none = styleDelta("none");
console.log("none selected -> left " + none.left + ", right " + none.right);
assert(none.left === 0 && none.right === 0,
       "with nothing selected the style toggle changes nothing (" +
       none.left + "/" + none.right + ")");

console.log("multiselect_outline: PASS");
