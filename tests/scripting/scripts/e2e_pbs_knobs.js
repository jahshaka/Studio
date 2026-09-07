// scripting.e2e.pbs_knobs — HLMS_ADOPTION P1: the five cheap PBS knobs, driven
// through the SAME verbs the panel calls (API-first, SCRIPTING_SPEC §2.3).
//
//   clearCoat, clearCoatRoughness   two floats, both INERT at 0
//   brdf                            the enum row (6 curated names)
//   receiveShadows                  bool, true by default
//   emissiveAsLightmap              bool, false by default
//
// What this asserts, and why each one:
//   1. every knob is a writable key and a declared row — the row IS the API
//      surface, so a missing row is a missing verb;
//   2. the BRDF row reports its VOCABULARY (`options`), because an enum that
//      reports only "3" is the surface defect that made every enum a special
//      case in the first place;
//   3. THE DEFAULTS ARE THE RENDERER'S OWN DEFAULTS — this is the byte-identical
//      argument as an assertion, not as prose: if a default ever moves, every
//      existing scene's shader permutation moves with it;
//   4. all five survive save / close / open;
//   5. the clear-coat/BRDF constraint: a non-Default BRDF cannot carry a coat,
//      the document KEEPS the authored coat anyway, and switching back to a
//      Default-family BRDF restores it (the decided behaviour: the panel
//      disables the coat rows rather than clearing them).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b) { return Math.abs(a - b) < 1e-3; }

var guid = project.create("PBS Knobs " + Date.now());
assert(guid.length > 10, "project.create");

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(material.apply(cube, "Brick PBR") === true, "material.apply gives the cube a PbrMaterial");

// ---- 1. the rows exist and are writable ----
var props = material.properties(cube);
var keys = props.writableKeys;
var wanted = ["clearCoat", "clearCoatRoughness", "brdf", "receiveShadows", "emissiveAsLightmap"];
for (var i = 0; i < wanted.length; i++)
    assert(keys.indexOf(wanted[i]) >= 0, "writableKeys contains '" + wanted[i] + "'");

function row(name) {
    for (var i = 0; i < props.rows.length; i++)
        if (props.rows[i].name === name) return props.rows[i];
    return null;
}

// ---- 2. the enum rows report their vocabulary ----
var brdfRow = row("brdf");
assert(brdfRow !== null, "the brdf row is declared");
assert(brdfRow.type === "enum", "brdf reports type 'enum' (not a bare int)");
assert(brdfRow.options && brdfRow.options.length === 6,
       "brdf reports 6 options, got " + (brdfRow.options ? brdfRow.options.length : "none"));
assert(brdfRow.options[0] === "Default", "option 0 is Default");
// alphaMode rode the same generic mechanism (it used to be a hardcoded branch
// in the panel). Its vocabulary must cover its WHOLE range — the blank-combo
// defect (PUBLISH_AUDIT #4) was exactly a label list that did not.
var alphaRow = row("alphaMode");
assert(alphaRow.type === "enum", "alphaMode is an enum row too");
assert(alphaRow.options.length === 7, "alphaMode reports all seven modes");
assert(alphaRow.options[6] === "Refractive", "the last alpha mode is labelled");

var coatRow = row("clearCoat");
assert(coatRow.type === "float" && coatRow.min === 0 && coatRow.max === 1,
       "clearCoat is a 0..1 float row");
assert(row("receiveShadows").type === "bool", "receiveShadows is a bool row");

// ---- 3. the defaults are the renderer's defaults (the inertness contract) ----
var m = material.get(cube);
assert(m.clearCoat === 0, "clearCoat defaults to 0 (the coat shader path is ABSENT at 0)");
assert(m.clearCoatRoughness === 0, "clearCoatRoughness defaults to 0");
assert(m.brdf === 0, "brdf defaults to 0 = Default");
assert(m.receiveShadows === true, "receiveShadows defaults to true");
assert(m.emissiveAsLightmap === false, "emissiveAsLightmap defaults to false");

// ---- 4. set / read back / round-trip ----
assert(material.set(cube, { clearCoat: 0.85, clearCoatRoughness: 0.05,
                            receiveShadows: false, emissiveAsLightmap: true }),
       "material.set on the four scalar/bool knobs");
m = material.get(cube);
assert(near(m.clearCoat, 0.85), "clearCoat read back");
assert(near(m.clearCoatRoughness, 0.05), "clearCoatRoughness read back");
assert(m.receiveShadows === false, "receiveShadows read back");
assert(m.emissiveAsLightmap === true, "emissiveAsLightmap read back");

// The three Default-family BRDFs can carry the coat; Cook-Torrance cannot.
assert(material.set(cube, { brdf: 3 }), "material.set brdf = 3 (Default, diffuse fresnel)");
assert(material.get(cube).brdf === 3, "brdf read back");

var before = material.get(cube);
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");

var ns = scene.nodes(), reopened = null;
for (var i = 0; i < ns.length; i++) if (ns[i].id === cube) reopened = ns[i].id;
assert(reopened !== null, "the cube survived the reopen");

var after = material.get(reopened);
for (var i = 0; i < wanted.length; i++) {
    var k = wanted[i];
    assert(JSON.stringify(before[k]) === JSON.stringify(after[k]),
           k + " survived save/close/open (" + JSON.stringify(after[k]) + ")");
}

// ---- 5. the clear-coat / BRDF constraint, and what it does NOT do ----
//
// THE DOCUMENTED OUTCOME (HLMS_ADOPTION P1, decision D-P1b): the BRDF wins and
// the coat is simply not rendered on a non-Default family — but the authored
// coat is NOT destroyed. The panel greys the two coat rows out with a reason
// instead of clearing them, so a BRDF experiment is reversible. The engine
// applies the same rule (it must: the renderer's setClearCoat is only legal on
// the Default family, and the assert that says so is compiled out of our
// RelWithDebInfo Ogre — a silent no-op is the failure mode this checks against).
assert(material.set(reopened, { brdf: 1, clearCoat: 0.7 }),
       "material.set Cook-Torrance WITH a coat > 0 is accepted");
var m2 = material.get(reopened);
assert(m2.brdf === 1, "the BRDF pick stands — it is NOT silently forced back to Default");
assert(near(m2.clearCoat, 0.7), "the authored coat is KEPT on the document, not zeroed");

assert(material.set(reopened, { brdf: 0 }), "back to the Default BRDF");
assert(near(material.get(reopened).clearCoat, 0.7), "the coat comes back with the BRDF");

// An index outside the vocabulary must not corrupt anything: the document
// stores what it was told, and the engine boundary falls back to "Default".
assert(material.set(reopened, { brdf: 99 }), "an out-of-range brdf index is accepted by the document");
assert(material.get(reopened).brdf === 99, "and stored verbatim (the engine falls back to Default)");
assert(material.set(reopened, { brdf: 0 }), "reset to Default");

console.log("e2e_pbs_knobs: ALL OK");
