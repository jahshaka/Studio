// scripting.e2e.material_preview — THE LIVE HOVER PREVIEW, for every material
// source (MATERIAL-PREVIEW-1).
//
// The owner found the preview working for a material made from an image in the
// asset manager and dead for the material TRAY and for materials made in the
// MATERIALS MODULE ("very very cool, we want this" — for all of them). It was
// never three behaviours: there is ONE runtime material class and all three
// sources send the same drag payload. The preview read the dragged material out
// of a QVariant on the AssetManager, and three of the registration sites filled
// that QVariant with `QSharedPointer<PbrMaterial>` through `auto` while the
// reader asked for `iris::MaterialPtr` — Qt has no converter between the two,
// so the read was null and the preview was silently skipped. The drop
// re-resolved the guid from the database and worked, which is exactly why it
// looked like a preview bug rather than a resolution one.
//
// Nothing resolves a material through the AssetManager any more:
// SceneEditService::resolveMaterial is the ONE way, and `material.preview` /
// `material.endPreview` are the verbs the viewport's drag handlers call. This
// suite drives those verbs for FOUR sources and asserts the contract:
//
//   * a TRAY PRESET guid, a LIBRARY material row the project does not own, a
//     GRAPH material created in-session and one RE-EDITED after creation all
//     preview;
//   * preview on A changes material.get(A); preview on B restores A exactly;
//   * endPreview restores;
//   * the undo count and the dirty flag never move;
//   * project.save during a preview writes the ORIGINAL;
//   * a LOCKED node refuses.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b) { return Math.abs(a - b) < 1e-3; }
/// A verb refusal, as a message. A refused verb THROWS in a script run, so
/// "this must be refused" is a catch, not a false.
function refused(fn) {
    try { fn(); } catch (e) { return String(e); }
    return "";
}
/// The fields that identify a material on a node, as one comparable string.
function fingerprint(id) {
    var m = material.get(id);
    return [m.baseColor, m.roughness, m.metallic, m.baseColorMap || "",
            m.emissiveColor || ""].join("|");
}

var guid = project.create("Material Preview " + Date.now());
assert(guid.length > 10, "project.create");

var cubeA = scene.addPrimitive("cube", { position: { x: -2, y: 1, z: 0 } });
var cubeB = scene.addPrimitive("cube", { position: { x: 2, y: 1, z: 0 } });
assert(cubeA && cubeB, "two meshes to hover over");

var plainA = fingerprint(cubeA);
var plainB = fingerprint(cubeB);

// ---------------------------------------------------------------- the sources
//
// 1. A TRAY PRESET. The presets tray's tiles carry the RESERVED GUID of a
//    shipped preset, which is what the drag payload holds; `materials.presets`
//    lists the same set.
var presets = materials.presets();
assert(presets.length > 1, "materials.presets lists the shipped presets");
var presetGuid = presets[0].guid;
var presetName = presets[0].name;
assert(presetGuid && presetGuid.length > 10,
       "the tray's payload is a reserved guid (" + presetName + ")");

// 2. A LIBRARY MATERIAL ROW. `materials.createFromImage`
//    against a library texture is the "created from an image in the asset
//    manager" path — the ONE source that used to preview.
var shippedTex = assets.list({ scope: "store", type: "texture" })[0];
assert(shippedTex, "the library has a texture to build a material from");
var imageMatGuid = materials.createFromImage(shippedTex.guid);
assert(imageMatGuid && imageMatGuid.length > 10,
       "materials.createFromImage -> a library material row");

// 3. A GRAPH MATERIAL CREATED IN-SESSION — the Materials module's own path, and
//    one of the two the owner found dead. Headlessly the module's own row is a
//    SHADER row carrying the baked PbrMaterial, which is exactly what the
//    drawer tile drags and what resolveMaterial reads through parseShaderAsPbr.
var graphGuid = materials.createFromImage(shippedTex.guid, { graph: true });
assert(graphGuid && graphGuid.length > 10,
       "materials.createFromImage({graph:true}) -> an effect graph asset");

// 4. THE SAME GRAPH, RE-BAKED after creation — the audit named the edit path a
//    separate registration site (effectspage's updateMaterialFromShader), so it
//    is a separate case.
assert(materials.regenerate(graphGuid) === true, "the graph re-bakes after creation");

var sources = [
    { what: "a TRAY PRESET", id: presetGuid },
    { what: "a LIBRARY material row (made from an image)", id: imageMatGuid },
    { what: "a GRAPH material (created in-session)", id: graphGuid },
    { what: "a GRAPH material (re-baked after creation)", id: graphGuid }
];

// -------------------------------------------------- the contract, per source
// THE UNDO STACK'S OWN COUNTER. `pushes` is the only honest "did that record an
// undo step?" from inside a script run (the run is one open macro, so `count`
// cannot move) — and the document's DIRTY flag is that stack's clean state, so
// a pushes count that never moves is a dirty flag that never moves.
var undo0 = editor.undoState().pushes;

for (var i = 0; i < sources.length; ++i) {
    var src = sources[i];

    // EVERY SOURCE PREVIEWS. This is the owner's finding, as an assertion:
    // before the fix exactly one of the four did.
    assert(material.preview(cubeA, src.id) === true, src.what + " previews on A");
    var previewed = fingerprint(cubeA);
    assert(previewed !== plainA,
           src.what + ": the material on A really changed on screen");

    // MOVING TO THE NEXT OBJECT restores the first one EXACTLY — the drag's
    // "and it follows the cursor" half.
    assert(material.preview(cubeB, src.id) === true, src.what + " previews on B");
    assert(fingerprint(cubeA) === plainA,
           src.what + ": moving the hover to B restored A exactly");
    assert(fingerprint(cubeB) !== plainB, src.what + ": B is showing it now");

    // ENDING restores.
    assert(material.endPreview() === true, src.what + ": endPreview");
    assert(fingerprint(cubeB) === plainB, src.what + ": endPreview restored B exactly");
    assert(material.endPreview() === false, src.what + ": a second endPreview is a no-op");
}

// THE DOCUMENT NEVER MOVED. Sixteen previews and eight restores later, there is
// no undo step and no dirty flag to show for any of it.
assert(editor.undoState().pushes === undo0,
       "a preview pushes NO undo step and so does not dirty the document (pushes " +
       undo0 + " before and after)");

// ------------------------------------------- a save during a preview is honest
//
// The preview borrows the mesh's slot; a save taken mid-hover must write what
// the user chose, which is the ORIGINAL. (The save ends the preview to do it —
// ProjectService's pre-write hook — so the assertion is both "the file is
// right" and "the screen went back".)
assert(material.preview(cubeA, presetGuid) === true, "preview up for the save test");
assert(project.save() === true, "project.save during a preview");
assert(fingerprint(cubeA) === plainA, "the save ended the preview (A is itself again)");
var savedName = node.property(cubeA, "name");
project.close();
project.open(guid);
// The reopened document is new objects: find the same node by name.
cubeA = scene.nodes().filter(function (n) { return n.name === savedName; })[0].id;
cubeB = scene.nodes().filter(function (n) { return n.name !== savedName && n.type === "mesh"; })[0].id;
assert(fingerprint(cubeA) === plainA,
       "and the SAVED project holds the ORIGINAL material, not the previewed one");

// ------------------------------------------------------------ a locked node
//
// A locked node refuses the DROP by name, so it must refuse the PREVIEW too — a
// preview on a node whose release will be refused is a promise the gesture
// cannot keep.
var lockedCube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 3 } });
var lockedPlain = fingerprint(lockedCube);
assert(node.setProperty(lockedCube, "pickable", false) === true, "lock the cube");
var lockMsg = refused(function () { material.preview(lockedCube, presetGuid); });
assert(lockMsg.indexOf("is locked") > 0,
       "a LOCKED node refuses the preview, by name: " + lockMsg);
assert(fingerprint(lockedCube) === lockedPlain, "...and is left exactly as it was");
assert(node.setProperty(lockedCube, "pickable", true) === true, "unlock it again");
assert(material.preview(lockedCube, presetGuid) === true, "unlocked, it previews");
material.endPreview();

// ------------------------------------------------- an unresolvable payload
//
// The drag is ACCEPTED on this answer, so it has to be honest: a string that
// names no preset, no material row and no effect graph is refused, and nothing
// is left running.
var badMsg = refused(function () { material.preview(cubeA, "not-a-guid-at-all"); });
assert(badMsg.indexOf("no preset, material asset or effect graph") > 0,
       "a payload that resolves to no material is refused, by name: " + badMsg);
assert(fingerprint(cubeA) === plainA, "...and A was not touched");
assert(material.endPreview() === false, "...and no preview was left running");

// ------------------------------------------- the apply still commits, once
//
// The preview is not a substitute for the apply: the drop applies through the
// ONE apply path, as exactly one undo step, and the undo takes the node back to
// its TRUE original (not to a borrowed material).
var undoBefore = editor.undoState().pushes;
assert(material.preview(cubeA, presetGuid) === true, "hover before the drop");
assert(material.apply(cubeA, presetGuid) === true, "the drop applies");
assert(editor.undoState().pushes === undoBefore + 1,
       "the apply is exactly ONE undo command (pushes " + undoBefore + " -> " +
       editor.undoState().pushes + ")");
var applied = fingerprint(cubeA);
assert(applied !== plainA, "the apply really changed A");
// (That the UNDO of this step restores the node's TRUE original — and not the
// borrowed preview material — is asserted by ui.material_drag, which drives real
// drag events outside a script macro. A script run is ONE open macro, so
// editor.undo() cannot reach a step the run itself pushed.)

// ------------------------------------- ONE project row per preset, reused
//
// Applying a tray preset used to MINT a project material row every single time
// ("Gold PBR" three times over in the owner's library) and undo removed none of
// them, because the row is project bookkeeping and the undo step is the
// document's. One row per preset per project now, found and reused.
function presetRows() {
    return assets.list({ scope: "project", type: "material" }).filter(function (a) {
        return a.name === presetName;
    }).length;
}
assert(material.apply(cubeA, presetGuid) === true, "preset applied once");
var rows1 = presetRows();
assert(rows1 === 1, "one project material row for the preset (got " + rows1 + ")");
assert(material.apply(cubeB, presetGuid) === true, "the same preset applied again, elsewhere");
assert(material.apply(cubeA, presetGuid) === true, "and again, on the same node");
var rows3 = presetRows();
assert(rows3 === 1,
       "STILL one row after three applies (got " + rows3 + ") — the row is reused, not minted");

console.log("scripting.e2e.material_preview OK");
