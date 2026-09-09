// scripting.e2e.workflows — MATERIAL_GAPS_SPEC GAP 1: the per-material
// SPECULAR / FRESNEL WORKFLOW, driven through the same declared-row/material.set
// path the panel uses (API-first, SCRIPTING_SPEC §2.3).
//
//   workflow          enum: Metallic (0, the default) | Specular | Specular (Fresnel)
//   specularColor     kS — live in EVERY workflow, white is inert
//   ior               1..3, the authoring front-end for F0 = ((1-ior)/(1+ior))²
//   fresnelColor      F0 directly, when useFresnelColor is on
//   useFresnelColor   which of the two supplies F0
//   separateFresnel   scalar F0 (cheap) vs per-channel (a shader permutation)
//
// What this asserts, and why each one:
//   1. every row is declared and writable — the row IS the API surface;
//   2. the enum reports its VOCABULARY and ACCEPTS it by name (a label that
//      silently coerced to 0 is the defect class this fixes);
//   3. THE DEFAULT IS METALLIC and every new value is inert there — the
//      byte-identical-pixels argument as an assertion. The renderer's OWN
//      default is Specular; Metallic is ours, so a moved default here would
//      move every existing scene;
//   4. the RENDERER actually switches: material.dumpDatablock reports the
//      workflow, and reports `metalness` in one and `fresnel` in the other.
//      This is the I-1 fence — metalness and F0 are the SAME FLOAT in the
//      datablock and our engine is built with NDEBUG, so a push that wrote
//      both would corrupt one with the other in complete silence;
//   5. pixels move when the workflow does, and a specular material lit by one
//      directional light is not the metallic one;
//   6. everything survives save / close / open, including on a metallic
//      material (values are kept across a workflow switch, like clear coat).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) < (eps === undefined ? 1e-3 : eps); }
function show(tag, c) { return tag + " rgb(" + c.r + "," + c.g + "," + c.b + ")"; }

var guid = project.create("Workflows " + Date.now());
assert(guid.length > 10, "project.create");

var ball = scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 0 } });
assert(material.apply(ball, "Brick PBR") === true, "material.apply gives it a PbrMaterial");

// ---- 1. the rows exist and are writable ----
var props = material.properties(ball);
var keys = props.writableKeys;
var wanted = ["workflow", "specularColor", "ior", "fresnelColor",
              "useFresnelColor", "separateFresnel"];
for (var i = 0; i < wanted.length; i++)
    assert(keys.indexOf(wanted[i]) >= 0, "writableKeys contains '" + wanted[i] + "'");

function row(name) {
    for (var i = 0; i < props.rows.length; i++)
        if (props.rows[i].name === name) return props.rows[i];
    return null;
}

// ---- 2. the enum reports AND accepts its vocabulary ----
var wfRow = row("workflow");
assert(wfRow !== null, "the workflow row is declared");
assert(wfRow.type === "enum", "workflow reports type 'enum' (not a bare int)");
assert(wfRow.options && wfRow.options.length === 3,
       "workflow reports 3 options, got " + (wfRow.options ? wfRow.options.length : "none"));
assert(wfRow.options[0] === "Metallic", "option 0 is Metallic");

// ---- 3. the DEFAULT is Metallic, and the new rows are inert there ----
var m0 = material.get(ball);
assert(m0.workflow === 0, "a fresh material is METALLIC (our default, not the renderer's)");
assert(near(m0.ior, 1.5), "ior defaults to 1.5 (window glass, F0 = 0.04)");
assert(m0.useFresnelColor === false, "F0 comes from the IOR by default");
assert(m0.separateFresnel === false, "the cheap scalar fresnel is the default");
// Colours come back as HEX from material.get (colorToJs), and go in as either
// hex or {r,g,b} — the registry's own convention.
assert(m0.specularColor.toLowerCase() === "#ffffff",
       "kS defaults to white — an inert multiplier (got " + m0.specularColor + ")");

editor.frame(3);

// ---- 4. THE I-1 FENCE: the datablock says which of the two floats is live ----
// metalness and F0 are the same float in the renderer (mFresnelR). NDEBUG
// compiles its workflow asserts out, so nothing but this test notices a push
// that writes both.
assert(material.set(ball, { metallic: 0.9, roughness: 0.25 }), "author a metallic value");
editor.frame(2);
var dumpMetallic = material.dumpDatablock(ball);
assert(dumpMetallic.indexOf("\"workflow\"") >= 0, "the datablock dump reports a workflow");
assert(dumpMetallic.indexOf("metallic") >= 0, "a METALLIC material reports the metallic workflow");
assert(dumpMetallic.indexOf("\"metalness\"") >= 0,
       "...and carries a `metalness` block (F0 is not written in this workflow)");
assert(dumpMetallic.indexOf("\"fresnel\"") < 0,
       "...and NO `fresnel` block — the two share one float");

// By NAME, not by ordinal: a label that silently became 0 is exactly the
// failure the enum-name coercion fixes.
assert(material.set(ball, { workflow: "Specular" }) === true,
       "material.set accepts the workflow by NAME");
assert(material.get(ball).workflow === 1, "...and stores the right index");
editor.frame(2);
var dumpSpec = material.dumpDatablock(ball);
assert(dumpSpec.indexOf("specular_ogre") >= 0 || dumpSpec.indexOf("specular_fresnel") >= 0,
       "the datablock is now in a SPECULAR workflow");
assert(dumpSpec.indexOf("\"fresnel\"") >= 0,
       "a specular material carries a `fresnel` block");
assert(dumpSpec.indexOf("\"metalness\"") < 0,
       "...and NO `metalness` block — exactly one of the two is ever written (I-1)");

var badName = false;
try { material.set(ball, { workflow: "Shiny" }); } catch (e) { badName = true; }
assert(badName, "an unknown workflow NAME is refused, not coerced to 0");
// An out-of-range ORDINAL is deliberately NOT refused: enum rows are ints on
// disk and a document written by a newer build must still open, so the document
// stores what it was told and the engine falls back to the safe value. That is
// the rule scripting.e2e.pbs_knobs states for `brdf`, and it holds here too —
// a bad NAME is refused because a label can only come from a human.
assert(material.set(ball, { workflow: 7 }), "an out-of-range ORDINAL is stored verbatim");
assert(material.get(ball).workflow === 7, "...and read back");
assert(material.set(ball, { workflow: "Specular" }), "back to Specular");
assert(material.get(ball).workflow === 1, "a refused NAME changed nothing earlier");

// ---- 5. IOR vs an explicit F0, and the per-channel permutation ----
assert(material.set(ball, { ior: 2.4 }), "material.set ior (diamond-ish)");
editor.frame(2);
// F0 = ((1-2.4)/(1+2.4))^2 = 0.1696
assert(material.dumpDatablock(ball).indexOf("0.169") >= 0,
       "the renderer computed F0 from the IOR with its own formula");

assert(material.set(ball, { useFresnelColor: true,
                            fresnelColor: { r: 255, g: 0, b: 0 },
                            separateFresnel: true }),
       "material.set an explicit per-channel F0");
editor.frame(2);
var dumpF0 = material.dumpDatablock(ball);
assert(dumpF0.indexOf("0.169") < 0, "an explicit F0 REPLACES the IOR-derived one");
assert(dumpF0.indexOf("\"fresnel\"") >= 0, "the fresnel block is still there");

// ---- 6. kS is live in EVERY workflow, metallic included ----
assert(material.set(ball, { workflow: "Metallic", specularColor: { r: 0, g: 0, b: 255 } }),
       "kS on a METALLIC material");
editor.frame(2);
var dumpKs = material.dumpDatablock(ball);
assert(dumpKs.indexOf("\"metalness\"") >= 0, "back to metallic");
assert(dumpKs.indexOf("\"specular\"") >= 0,
       "...and kS is still written — it works in every workflow");

// ---- 7. PIXELS move with the workflow ----
// One directional light, no GI/SSAO churn: the fixture is a shipped preset on
// a primitive, framed from a fixed angle, so the two probes differ only in the
// shading model the workflow selects.
function ballColour(name) {
    editor.select(null);
    editor.frameNode(ball, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(3);
    return editor.screenshot(name, 128, 128, [{ x: 0.5, y: 0.5 }]).probes[0];
}
assert(material.set(ball, { workflow: "Metallic", metallic: 1.0, roughness: 0.15,
                            specularColor: { r: 255, g: 255, b: 255 },
                            useFresnelColor: false }),
       "author a full metal");
var metalPx = ballColour("workflow_metallic.png");
console.log("    " + show("METALLIC", metalPx));

assert(material.set(ball, { workflow: "Specular", ior: 1.5 }), "same surface, specular workflow");
var specPx = ballColour("workflow_specular.png");
console.log("    " + show("SPECULAR", specPx));
assert(Math.abs(specPx.r - metalPx.r) > 6 || Math.abs(specPx.g - metalPx.g) > 6 ||
       Math.abs(specPx.b - metalPx.b) > 6,
       "the workflow reaches PIXELS: a full metal and a dielectric are different images " +
       show("metal", metalPx) + " vs " + show("spec", specPx));

// ---- 8. save / close / open, values kept across a workflow switch ----
assert(material.set(ball, { workflow: "Specular (Fresnel)", ior: 1.8,
                            useFresnelColor: false, separateFresnel: true,
                            specularColor: { r: 12, g: 34, b: 56 } }),
       "author the third workflow");
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");

var ns = scene.nodes(), reopened = null;
for (var i = 0; i < ns.length; i++) if (ns[i].id === ball) reopened = ns[i].id;
assert(reopened !== null, "the sphere survived the reopen");
var after = material.get(reopened);
assert(after.workflow === 2, "the workflow survived save/close/open");
assert(near(after.ior, 1.8), "and the IOR");
assert(after.separateFresnel === true, "and the fresnel permutation");
assert(after.specularColor.toLowerCase() === "#0c2238",
       "and kS (got " + after.specularColor + ")");

// A METALLIC material keeps its fresnel values, the same "values survive the
// switch" rule clear coat follows — the panel greys those rows, it never
// clears them.
assert(material.set(reopened, { workflow: "Metallic" }), "switch back to Metallic");
var kept = material.get(reopened);
assert(near(kept.ior, 1.8) && kept.separateFresnel === true,
       "the fresnel values are KEPT on a metallic material, not destroyed");
assert(project.save() === true, "project.save (metallic, with fresnel values)");
assert(project.close() === true && project.open(guid) === true, "reopen");
var kept2 = material.get(ball);
assert(kept2.workflow === 0 && near(kept2.ior, 1.8),
       "...and they are serialized even while inert, so the switch back restores them");

console.log("workflows: ALL OK");
