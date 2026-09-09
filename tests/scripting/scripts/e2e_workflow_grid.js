// scripting.e2e.workflow_grid — MATERIAL_GAPS_SPEC GAP 1 ACCEPTANCE (§2.7).
//
// Ogre's own PbsMaterials showcase builds an 8x8 grid of spheres, roughness
// across one axis and FRESNEL (F0) across the other, on the ctor-default
// Specular workflow (Samples/2.0/Showcase/PbsMaterials/PbsMaterialsGameState.cpp:140-186).
// This reproduces it FROM OUR DOCUMENT MODEL, through the verbs — which is what
// proves the whole chain (document row -> material.set -> mirror -> PbrParams ->
// applyPbr -> datablock -> shader) rather than any one layer of it.
//
// DETERMINISM (the §9 caveat, answered by construction):
//   - one directional light, no sky, GI OFF, and `grade: "raw"` on every shot,
//     which is a readback with NO post chain at all (no SSAO, no bloom, no
//     adaptive exposure — the three things that make the Showroom sample swing
//     +/-80 between two identical opens, MESH_BAKE facts);
//   - a FIXED camera (editor.setCamera with an explicit position and lookAt),
//     never a framing helper whose answer depends on bounds;
//   - the assertions are MONOTONIC RELATIONS between probes in ONE frame, plus
//     a re-render equality, not absolute colours. A grid that is right for the
//     wrong reason cannot satisfy both.
//
// What must be true, and why:
//   1. F0 CLIMBS ALONG ITS AXIS. At fixed roughness, a higher fresnel is a
//      brighter surface — that is the whole meaning of F0. This is the
//      assertion the metallic workflow could not make at all: fresnel is
//      unreachable there (it is the metalness float), which is why
//      HLMS_ADOPTION rejected an IOR knob without the workflow switch.
//   2. ROUGHNESS SPREADS THE HIGHLIGHT. At fixed high F0, a mirror-smooth
//      sphere and a fully rough one are different pixels.
//   3. THE GRID IS REPRODUCIBLE: rendering it twice gives the same probes.
//   4. THE SAME GRID IN THE METALLIC WORKFLOW IS FLAT ALONG THE F0 AXIS —
//      the control that proves the axis is the workflow's doing and not the
//      material.set call's.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function show(tag, c) { return tag + " rgb(" + c.r + "," + c.g + "," + c.b + ")"; }
function luma(c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

var guid = project.create("Workflow Grid " + Date.now());
assert(guid.length > 10, "project.create");

// ---- a deterministic world ---------------------------------------------
// No sky (nothing to reflect but the one light), no GI, ambient near black so
// the only thing shading a sphere is its own BRDF.
assert(world.setSky("color", { color: "#000000" }) === true, "a flat black sky");
assert(world.gi({ mode: "off" }) === true, "GI OFF (no bounce, nothing to converge)");
world.setAmbient("#050505");
assert(world.setShadows({ enabled: false }) === true, "no shadows (nothing casts onto anything)");

var sun = scene.addLight("directional", { position: { x: 0, y: 8, z: 8 } });
assert(sun.length > 10, "one directional light");
node.setProperty(sun, "intensity", 3.0);
node.setProperty(sun, "lightColor", "#ffffff");
// Pointing down and toward the camera side: the document's lights point -Y and
// the rotation turns that into the grid's key light.
node.transform(sun, { rotation: { x: -50, y: 0, z: 0 } });

// ---- the 8x8 grid -------------------------------------------------------
// Ogre's own numbers: roughness = max(0.02, x/7), fresnel = z/7.
var N = 8;
var SPACING = 1.6;
var ids = [];
for (var z = 0; z < N; z++) {
    for (var x = 0; x < N; x++) {
        var id = scene.addPrimitive("sphere", {
            position: { x: (x - (N - 1) / 2) * SPACING,
                        y: (z - (N - 1) / 2) * SPACING,
                        z: 0 },
            scale: { x: 0.7, y: 0.7, z: 0.7 }
        });
        assert(id.length > 10, "sphere " + x + "," + z);
        var f = z / (N - 1);
        var v = Math.round(f * 255);
        // The renderer's F0 is authored DIRECTLY here rather than through the
        // IOR: the sample's grid is a linear F0 sweep, and an IOR sweep would
        // be the same numbers through a nonlinear map.
        assert(material.set(id, {
            workflow: "Specular",
            baseColor: "#cccccc",
            roughness: Math.max(0.02, x / (N - 1)),
            specularColor: "#ffffff",
            useFresnelColor: true,
            fresnelColor: { r: v, g: v, b: v },
            separateFresnel: false
        }), "material.set on sphere " + x + "," + z);
        ids.push(id);
    }
}
console.log("built " + ids.length + " grid materials");

// ---- a FIXED camera, never a framing helper -----------------------------
editor.select(null);
var cam = editor.setCamera({
    position: { x: 0, y: 0, z: 16.5 },
    lookAt: { x: 0, y: 0, z: 0 },
    fov: 45
});
assert(cam && cam.position, "editor.setCamera pinned the viewpoint");
editor.setOverlays({ grid: false, lightWires: false, selectionWireframe: false, stats: false });
editor.frame(6);

// Probe the CENTRE of each sphere. The grid spans SPACING*(N-1) = 11.2 world
// units at a fixed camera, so the normalized probe positions are fixed too:
// column x sits at (x + 0.5)/N across, row z at 1 - (z + 0.5)/N down (screen y
// grows downward, world y upward).
function probesForRow(z) {
    var p = [];
    for (var x = 0; x < N; x++)
        p.push({ x: (x + 0.5) / N, y: 1.0 - (z + 0.5) / N });
    return p;
}
function probesForColumn(x) {
    var p = [];
    for (var z = 0; z < N; z++)
        p.push({ x: (x + 0.5) / N, y: 1.0 - (z + 0.5) / N });
    return p;
}

// ---- 1. F0 climbs along its axis ---------------------------------------
// A middling-roughness column: at low roughness the highlight is a point and
// the probe can miss it; at 1.0 the surface is nearly Lambertian. Column 3
// (roughness 3/7 = 0.43) is where F0 is a broad, probe-able change.
var col = 3;
var shot = editor.screenshot("grid_specular.png", 512, 512, probesForColumn(col));
assert(shot.probes.length === N, "the shot returned one probe per row");
var lumas = [];
for (var i = 0; i < N; i++) {
    lumas.push(luma(shot.probes[i]));
    console.log("    F0 " + (i / (N - 1)).toFixed(2) + ": " + show("", shot.probes[i]) +
                " luma " + lumas[i].toFixed(1));
}
assert(lumas[N - 1] > lumas[0] + 6,
       "F0 CLIMBS along its axis: F0=1 is brighter than F0=0 (" +
       lumas[0].toFixed(1) + " -> " + lumas[N - 1].toFixed(1) + ")");
// Monotone in the large: every step of two must not go backwards. (Adjacent
// steps can tie at 8 samples across a smooth curve; a REVERSAL over two steps
// would mean the axis is not the fresnel term.)
for (var i = 0; i + 2 < N; i++)
    assert(lumas[i + 2] >= lumas[i] - 1.5,
           "F0 does not go backwards between step " + i + " and " + (i + 2));

// ---- 2. roughness spreads the highlight --------------------------------
var rowShot = editor.screenshot("grid_roughness.png", 512, 512, probesForRow(N - 1));
var smooth = rowShot.probes[0];        // roughness 0.02
var rough  = rowShot.probes[N - 1];    // roughness 1.0
console.log("    " + show("smooth", smooth) + "  " + show("rough", rough));
assert(Math.abs(luma(smooth) - luma(rough)) > 4,
       "roughness changes the surface at fixed F0 (" + luma(smooth).toFixed(1) +
       " vs " + luma(rough).toFixed(1) + ")");

// ---- 3. the grid is REPRODUCIBLE ---------------------------------------
editor.frame(3);
var again = editor.screenshot("grid_specular2.png", 512, 512, probesForColumn(col));
for (var i = 0; i < N; i++) {
    var a = shot.probes[i], b = again.probes[i];
    assert(Math.abs(a.r - b.r) <= 2 && Math.abs(a.g - b.g) <= 2 && Math.abs(a.b - b.b) <= 2,
           "probe " + i + " is reproducible across renders " + show("", a) + " vs " + show("", b));
}

// ---- 4. THE CONTROL: the same grid is FLAT in the metallic workflow -----
// F0 is unreachable in the metallic workflow — that float is metalness — so
// the same fresnelColor sweep must change nothing there. Without this, the
// climb above could be any side effect of calling material.set 64 times.
for (var i = 0; i < ids.length; i++)
    material.set(ids[i], { workflow: "Metallic", metallic: 0.0 });
editor.frame(4);
var flat = editor.screenshot("grid_metallic.png", 512, 512, probesForColumn(col));
var flatLumas = [];
for (var i = 0; i < N; i++) flatLumas.push(luma(flat.probes[i]));
console.log("    metallic column: " + flatLumas.map(function (v) { return v.toFixed(1); }).join(", "));
var flatSpread = Math.max.apply(null, flatLumas) - Math.min.apply(null, flatLumas);
var specSpread = Math.max.apply(null, lumas) - Math.min.apply(null, lumas);
assert(flatSpread < specSpread * 0.5,
       "the SAME fresnel sweep is flat in the metallic workflow (spread " +
       flatSpread.toFixed(1) + " vs " + specSpread.toFixed(1) + ") — the axis is the WORKFLOW's");

// ---- 5. and it all survives a save/reopen ------------------------------
for (var i = 0; i < ids.length; i++)
    material.set(ids[i], { workflow: "Specular" });
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
var reopened = material.get(ids[ids.length - 1]);
assert(reopened.workflow === 1, "the grid's workflow survived the reopen");
assert(reopened.useFresnelColor === true, "and its F0 authoring mode");
assert(reopened.fresnelColor.toLowerCase() === "#ffffff",
       "and its F0 value (got " + reopened.fresnelColor + ")");

console.log("workflow_grid: ALL OK");
