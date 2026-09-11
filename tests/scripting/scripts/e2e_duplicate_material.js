// scripting.e2e.duplicate_material — A DUPLICATE RENDERS LIKE ITS ORIGINAL
// (RENDER_PIPELINE_AUDIT 3.2, smoke round lane L12).
//
// Every duplicate path funnels through SceneEditService::duplicateNode ->
// SceneNode::duplicate -> MeshNode::createDuplicate, which handed the copy
// `material->duplicate()` — and the base Material's duplicate() returned a
// BLANK base Material. The mirror renders PbrMaterials only, so every copy
// fell back to the neutral grey: measured (86,2,2) original vs (70,70,70)
// copy, `material.get(copy)` = {}. This pins the fix where the user sees it —
// the copy's PIXELS — through both scripted funnels (node.duplicate, and
// editor.duplicateSelection, which is Ctrl+D's and Alt+drag's), plus the
// half a still image cannot show: the copy's material is its OWN, so
// repainting the copy leaves the original red.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function show(tag, c) { return tag + " (" + c.r + "," + c.g + "," + c.b + ")"; }
function dist(a, b) {
    return Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b);
}
function redDominant(c) { return c.r > c.g + 40 && c.r > c.b + 40; }
function blueDominant(c) { return c.b > c.r + 40 && c.b > c.g + 40; }

var guid = project.create("Duplicate Material " + Date.now());
assert(guid.length > 10, "project.create");

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(material.set(cube, { baseColor: "#c81414", metallic: 0, roughness: 0.6 }) === true,
       "the original is authored red");

// One pose per node: the same yaw/pitch/distance around each node's own
// centre, so two equal materials under the same light read the same pixel.
function probe(id, name) {
    editor.select(null);   // a selected node wears a gizmo over the centre pixel
    editor.frameNode(id, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(3);
    return editor.screenshot(name, 128, 128, [{ x: 0.5, y: 0.5 }]).probes[0];
}

var original = probe(cube, "dup_original.png");
console.log("    " + show("ORIGINAL", original));
assert(redDominant(original), "the original renders red " + show("", original));

// ---- 1. node.duplicate ----------------------------------------------------
var copy = node.duplicate(cube);
assert(copy && copy !== cube, "node.duplicate made a copy");
node.setProperty(copy, "position", { x: 4, y: 1, z: 0 });
var matA = material.get(cube), matB = material.get(copy);
assert(material.properties(copy)["class"] === "PbrMaterial",
       "the copy's material is a PbrMaterial (was the blank base class)");
assert(matB.baseColor === matA.baseColor && Math.abs(matB.roughness - matA.roughness) < 1e-6 &&
       Math.abs(matB.metallic - matA.metallic) < 1e-6,
       "the copy's material values equal the original's (" + matB.baseColor + ")");
var copyPx = probe(copy, "dup_copy.png");
console.log("    " + show("COPY", copyPx));
assert(redDominant(copyPx), "the copy renders red, not the grey fallback " + show("", copyPx));
assert(dist(copyPx, original) < 18,
       "the copy renders the original's colour " + show("", copyPx) + " vs " + show("", original));

// ---- 2. editor.duplicateSelection (the Ctrl+D / Alt+drag funnel) -----------
editor.select(cube);
var dups = editor.duplicateSelection();
assert(dups.length === 1 && dups[0] !== cube, "duplicateSelection made one copy");
node.setProperty(dups[0], "position", { x: -4, y: 1, z: 0 });
var selPx = probe(dups[0], "dup_selection.png");
console.log("    " + show("SELECTION COPY", selPx));
assert(redDominant(selPx) && dist(selPx, original) < 18,
       "a Ctrl+D copy renders the original's colour " + show("", selPx));

// ---- 3. the copy's material is its OWN --------------------------------------
assert(material.set(copy, { baseColor: "#1414c8" }) === true, "repaint the first copy blue");
assert(material.get(cube).baseColor === matA.baseColor,
       "the original's material is untouched (" + material.get(cube).baseColor + ")");
var blueCopy = probe(copy, "dup_copy_blue.png");
var stillRed = probe(cube, "dup_original_after.png");
console.log("    " + show("COPY BLUE", blueCopy) + "   " + show("ORIGINAL AFTER", stillRed));
assert(blueDominant(blueCopy), "the repainted copy draws blue " + show("", blueCopy));
// Red, with no blue in it. Not pixel-equal to the first probe: two red cubes
// and a blue one now stand beside it and bounce light onto it at Epic.
assert(redDominant(stillRed) && stillRed.b * 3 < stillRed.r,
       "the original still draws its red " + show("", stillRed));

console.log("e2e duplicate material: all assertions passed");
