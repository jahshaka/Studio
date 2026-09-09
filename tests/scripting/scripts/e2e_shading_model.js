// scripting.e2e.shading_model — HLMS_ADOPTION P4a: the Unlit shading model,
// driven through the SAME verbs the panel calls (API-first, SCRIPTING_SPEC §2.3).
//
// Unlit is not one more knob: it is the renderer's OTHER material family, so a
// switch destroys the backend material, rebuilds it in the other family and
// re-attaches every renderable — while the document's material identity, and
// every value the family cannot use, survive untouched.
//
// What this asserts, and why each one:
//   1. shadingModel is a declared enum row with its own vocabulary — the row IS
//      the API surface, so a missing row is a missing verb;
//   2. THE DEFAULT IS LIT, which is the "this feature moves no existing pixel"
//      claim expressed as a test;
//   3. it reaches PIXELS: an unlit cube renders its authored base colour, a lit
//      one does not. A perfect document proves nothing about what is drawn;
//   4. the switch is RECORDED as an undo step (editor.undoState().pushes — the
//      only honest answer inside a script's own open macro; ui.material_panel
//      drives the actual undo/redo on the real stack);
//   5. the values Unlit cannot use are KEPT, not destroyed, so going back to Lit
//      restores the material exactly;
//   6. all of it survives save / close / open.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, tol) { return Math.abs(a - b) <= (tol === undefined ? 1e-3 : tol); }
function show(tag, c) { return tag + " (" + c.r + "," + c.g + "," + c.b + ")"; }

var guid = project.create("Shading Model " + Date.now());
assert(guid.length > 10, "project.create");

// A BARE primitive, deliberately: its default material is an untextured
// PbrMaterial, so the pixel it draws is a function of the base colour and the
// lighting and of nothing else. A preset would bring three maps and a texture
// scale along and turn every colour assertion into an argument about brick.
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });

// ---- 1. the row exists, is an enum, and reports its vocabulary ----
var props = material.properties(cube);
assert(props.writableKeys.indexOf("shadingModel") >= 0, "writableKeys contains 'shadingModel'");
var row = null;
for (var i = 0; i < props.rows.length; i++)
    if (props.rows[i].name === "shadingModel") row = props.rows[i];
assert(row !== null, "the shadingModel row is declared");
assert(row.type === "enum", "shadingModel reports type 'enum' (not a bare int)");
// THREE models since POST_LOOKS_SPEC §5.2: Distortion joined the vocabulary.
assert(row.options && row.options.length === 3,
       "shadingModel reports 3 options, got " + (row.options ? row.options.length : "none"));
assert(row.options[0] === "Lit" && row.options[1] === "Unlit" &&
       row.options[2] === "Distortion",
       "the vocabulary is Lit, Unlit, Distortion — and the INDEX is the stored value");

// ---- 2. the default is Lit ----
assert(material.get(cube).shadingModel === 0,
       "shadingModel defaults to 0 = Lit (so no existing material changes family)");

// A colour the renderer cannot possibly produce by accident: pure, saturated,
// and far from anything the default scene lights or the clear colour give.
assert(material.set(cube, { baseColor: "#00cc22", metallic: 0, roughness: 0.6 }),
       "material.set an unmistakable base colour");
editor.frame(3);

// ---- 3. PIXELS: lit is shaded, unlit is the authored colour ----
// Aim at the cube, and DESELECT first: a selected node wears a gizmo whose
// origin marker sits on the exact centre pixel, so `center` would photograph
// the gizmo. The 5x5 probe average is what the sample gates use.
function faceColour(name) {
    editor.select(null);
    editor.frameNode(cube, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(3);
    return editor.screenshot(name, 128, 128, [{ x: 0.5, y: 0.5 }]).probes[0];
}
var lit = faceColour("shading_lit.png");
console.log("    " + show("LIT", lit));
assert(!(near(lit.r, 0, 12) && near(lit.g, 204, 12) && near(lit.b, 34, 12)),
       "the LIT control is SHADED, not the raw authored colour " + show("", lit));

var pushesBefore = editor.undoState().pushes;
assert(material.set(cube, { shadingModel: 1 }), "material.set shadingModel = 1 (Unlit)");
assert(material.get(cube).shadingModel === 1, "the model reads back as Unlit");

// ---- 4. the switch is a recorded undo step ----
assert(editor.undoState().pushes === pushesBefore + 1,
       "the family switch pushed exactly one undo command");

var unlit = faceColour("shading_unlit.png");
console.log("    " + show("UNLIT", unlit));
assert(near(unlit.g, 204, 10) && unlit.r < 40 && unlit.b < 70,
       "an unlit surface renders its AUTHORED colour " + show("", unlit));
assert(Math.abs(unlit.g - lit.g) > 10 || Math.abs(unlit.r - lit.r) > 10,
       "...and it is visibly not the shaded image");

// ---- 5. what Unlit cannot use is KEPT, not destroyed ----
// Author values on rows Unlit ignores. The document must still hold them: the
// panel greys those rows out rather than clearing them, so a model experiment
// has to be reversible.
assert(material.set(cube, { roughness: 0.17, metallic: 0.83, normalFactor: 1.4,
                            clearCoat: 0.62, brdf: 1, receiveShadows: false }),
       "material.set values on rows the Unlit model ignores");
var kept = material.get(cube);
assert(near(kept.roughness, 0.17) && near(kept.metallic, 0.83) && near(kept.normalFactor, 1.4) &&
       near(kept.clearCoat, 0.62) && kept.brdf === 1 && kept.receiveShadows === false,
       "every ignored value is stored verbatim while the model is Unlit");
var stillUnlit = faceColour("shading_unlit2.png");
assert(near(stillUnlit.g, 204, 10) && stillUnlit.r < 40,
       "...and none of them changed what an unlit surface draws " + show("", stillUnlit));

// ---- 6. save / close / open ----
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");

var ns = scene.nodes(), reopened = null;
for (var i = 0; i < ns.length; i++) if (ns[i].id === cube) reopened = ns[i].id;
assert(reopened !== null, "the cube survived the reopen");

var after = material.get(reopened);
assert(after.shadingModel === 1, "the Unlit model survived save/close/open");
assert(near(after.roughness, 0.17) && near(after.clearCoat, 0.62) && after.brdf === 1,
       "and so did every value the model was ignoring");

cube = reopened;
var reopenedShot = faceColour("shading_unlit3.png");
console.log("    " + show("UNLIT after reopen", reopenedShot));
assert(near(reopenedShot.g, 204, 12) && reopenedShot.r < 40,
       "a reopened project builds the material in the Unlit family DIRECTLY " +
       show("", reopenedShot));

// ---- back to Lit: the material is exactly what it was ----
assert(material.set(reopened, { shadingModel: 0 }), "material.set back to Lit");
var relit = faceColour("shading_relit.png");
console.log("    " + show("RELIT", relit));
assert(!(near(relit.g, 204, 10) && relit.r < 40),
       "back on Lit the surface is shaded again " + show("", relit));
var end = material.get(reopened);
assert(near(end.roughness, 0.17) && near(end.metallic, 0.83) && near(end.clearCoat, 0.62) &&
       end.brdf === 1 && end.receiveShadows === false,
       "the round trip through Unlit lost nothing");

// An out-of-range model index must not corrupt anything: the document stores
// what it was told and the boundary treats anything but 1 as Lit.
assert(material.set(reopened, { shadingModel: 7 }), "an out-of-range shadingModel is accepted");
assert(material.get(reopened).shadingModel === 7, "and stored verbatim");
editor.frame(2);
assert(material.set(reopened, { shadingModel: 0 }), "reset to Lit");

console.log("e2e_shading_model: ALL OK");
