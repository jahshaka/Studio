// scripting.e2e.introspection — the discovery half of the scripting surface.
//
// Runs headless (--headless): every verb here is a document verb.
//
// Lane 0 (AI_SURFACE_PROGRAM_SPEC §2.0) — the contract gate's second half:
//   app.apiProblems() is empty, which is the first time ApiRegistry::validate()
//   has ever run over the REAL module set. The unit test that used to call it
//   (tests/scripting/test_script_engine.cpp) links only the scripting core, so
//   the "real module set" it validated was its own fake module. A verb
//   advertised with no invokable method behind it, a missing doc string or a
//   duplicate name in any of the shipped modules was invisible until now.
//   (The api.contract ctest is the other half: it proves docs/SCRIPTING.md is
//   still what this registry generates.)
//
// Lane A (§3.A) — the four discovery items, all of which exist so an agent
//   stops guessing key names and burning turns:
//     #1  node.properties(id), and the two "unknown property" errors carrying
//         the key list. Includes the min/max rule: irisgl's Float/IntProperty
//         ctors left minValue/maxValue UNINITIALISED, so this asserts both that
//         a range is absent where none is declared and correct where one is.
//     #9  material.properties(nodeId), including the undeclared *Map slots that
//         material.set accepts but createProperties never lists.
//     #10 the nine world set* aliases moving the same field as their nouns.
//     #16 "ground" reachable from scene.addPrimitive; {count: N} -> [id].

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

/// Runs fn and returns the error message; fails if fn did NOT throw.
function refusal(fn, what) {
    try {
        fn();
    } catch (e) {
        console.log("ok: refused " + what + " — " + e.message);
        return String(e.message);
    }
    throw new Error("assert failed: " + what + " was accepted, expected a refusal");
}

function rowsByName(rows) {
    var byName = {};
    for (var i = 0; i < rows.length; ++i) byName[rows[i].name] = rows[i];
    return byName;
}

// ---- lane 0: the registry describes itself completely -----------------------
var problems = app.apiProblems();
assert(problems.length === 0,
       "app.apiProblems() is empty over the live module set" +
       (problems.length ? " — got: " + problems.join(" | ") : ""));

// The verb has to be looking at something, or "empty" is meaningless: the
// registry it validated is the one api.verbs() enumerates.
var modules = api.verbs();
assert(modules.length >= 13, "the registry holds the shipped modules: " + modules.length);
var verbCount = 0;
for (var m = 0; m < modules.length; ++m) verbCount += modules[m].verbs.length;
assert(verbCount >= 190, "…and their verbs: " + verbCount);

var guid = project.create("Introspection Test " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- #16: "ground" is a primitive verb, and {count} batches -----------------
var groundId = scene.addPrimitive("ground");
assert(typeof groundId === "string" && groundId.length > 10,
       "scene.addPrimitive('ground') -> one id string");
assert(node.info(groundId).name === "Ground",
       "…and the node it made is named Ground: " + node.info(groundId).name);

// Case and whitespace are normalised the same way every other primitive is.
var groundId2 = scene.addPrimitive("  GROUND ");
assert(node.info(groundId2).name === "Ground", "'  GROUND ' normalises to Ground too");

var before = scene.nodes().length;
editor.beginBatch();
var trio = scene.addPrimitive("cube", { count: 3, position: { x: 1, y: 2, z: 3 } });
editor.endBatch();
// NOTE the idiom: `Array.isArray` is FALSE for every list this API returns
// (scene.nodes(), api.verbs(), node.boneNames — all of them). QJSEngine wraps a
// QVariantList as a variant-array proxy: indexing, .length and JSON.stringify
// all behave, but the prototype is not Array's. That is surface-wide and
// pre-existing, not something {count} introduced; a script tests it the way
// this one does.
assert(typeof trio === "object" && trio.length === 3,
       "scene.addPrimitive('cube', {count:3}) -> three ids");
assert(JSON.stringify(trio).charAt(0) === "[", "…and it serialises as a JSON array");
assert(trio[0] !== trio[1] && trio[1] !== trio[2] && trio[0] !== trio[2],
       "…three DISTINCT nodes");
assert(scene.nodes().length === before + 3, "…and the scene grew by exactly three");
assert(node.info(trio[2]).position.y === 2, "…each copy got the shared position option");
// The batch is one undoable unit: a closed beginBatch/endBatch pair, which is
// as far as an in-script assertion can go (the run's own macro is still open —
// see scripting.e2e.undo_macro for why undo cannot be driven from inside).

assert(scene.addPrimitive("cube", { count: 1 }) !== undefined,
       "count:1 is legal and returns a bare id");
assert(typeof scene.addPrimitive("cube", { count: 1 }) === "string",
       "…a STRING, not a one-element array — the default shape never changes");

refusal(function () { scene.addPrimitive("cube", { count: 0 }); }, "count: 0");
refusal(function () { scene.addPrimitive("cube", { count: 2.5 }); }, "count: 2.5");
refusal(function () { scene.addPrimitive("cube", { count: 9999 }); }, "count: 9999");
var unknownPrim = refusal(function () { scene.addPrimitive("dodecahedron"); },
                          "an unknown primitive");
assert(unknownPrim.indexOf("ground") >= 0, "…and the list it prints now names ground");

// ---- #1: node.properties -----------------------------------------------------
var cubeId = trio[0];
var cubeRows = node.properties(cubeId);
assert(cubeRows.length > 8, "node.properties(mesh) returns rows: " + cubeRows.length);

for (var r = 0; r < cubeRows.length; ++r) {
    var row = cubeRows[r];
    assert(typeof row.name === "string" && row.name.length > 0, "row " + r + " has a name");
    assert(typeof row.displayName === "string" && row.displayName.length > 0,
           "row " + row.name + " has a displayName");
    assert(typeof row.type === "string" && row.type !== "unknown",
           "row " + row.name + " has a known type: " + row.type);
    assert(row.hasOwnProperty("value"), "row " + row.name + " carries its value");
    assert(typeof row.writable === "boolean", "row " + row.name + " says whether it is writable");
    // The min/max contract: never one without the other, and never a degenerate
    // pair. THIS is what the uninitialised-memory fix buys.
    assert(row.hasOwnProperty("min") === row.hasOwnProperty("max"),
           "row " + row.name + " has min and max together or neither");
    if (row.hasOwnProperty("min"))
        assert(row.max > row.min, "row " + row.name + " declares a real range");
}

var cube = rowsByName(cubeRows);
assert(cube.position.type === "vec3" && cube.position.value.x === 1,
       "the transform rows are vec3 and carry live values");
assert(cube.name.type === "string" && cube.name.value === "Cube",
       "the name row reads as a string: " + cube.name.value);
assert(cube.visible.type === "bool" && cube.visible.value === true, "the visible row is a bool");
assert(cube.faceCullingMode.type === "int", "a mesh adds faceCullingMode as an int");
assert(!cube.position.hasOwnProperty("min"),
       "position declares no range (absent, NOT reported as 0..0)");
assert(cube.meshPath.writable === false && cube.meshIndex.writable === false,
       "a mesh's geometry rows report writable:false");
assert(cube.position.writable === true && cube.name.writable === true,
       "…and the ordinary rows report writable:true");

// A read-only row is refused by setProperty, and says so rather than lying.
var roMsg = refusal(function () { node.setProperty(cubeId, "meshPath", "/tmp/x.obj"); },
                    "writing a read-only row");
assert(roMsg.indexOf("read-only") >= 0, "…the refusal says read-only");

// Every node type answers.
var lightId = scene.addLight("spot");
var light = rowsByName(node.properties(lightId));
assert(light.intensity.type === "float" && light.lightColor.type === "color",
       "a light reflects intensity (float) and lightColor (color)");
assert(!light.intensity.hasOwnProperty("min"),
       "light intensity declares no range yet — absent, not invented");
assert(light.shadowMapResolution.type === "int", "…and shadowMapResolution as an int");

var emptyId = scene.addEmpty();
assert(node.properties(emptyId).length >= 8, "an empty node reflects the base rows");

var particlesId = scene.addParticles();
var particles = rowsByName(node.properties(particlesId));
assert(particles.particlesPerSecond.type === "float", "an emitter reflects its scalars");
assert(particles.shape.type === "string" && particles.shape.value.length > 0,
       "…its enum rows travel as NAMES: shape = " + particles.shape.value);
assert(particles.extents.type === "vec3", "…and its vector rows as vec3");
assert(particles.texture.writable === false,
       "an emitter's texture row reports writable:false (node.setParticleTexture owns it)");

var decalId = scene.addDecal("");
var decal = rowsByName(node.properties(decalId));
assert(decal.metalness.min === 0 && decal.metalness.max === 1,
       "a decal's metalness declares the 0..1 range its setter enforces");
assert(decal.roughness.min === 0 && decal.roughness.max === 1, "…and so does roughness");
assert(!decal.width.hasOwnProperty("min"), "…while width, which is only floored, declares none");

// ---- #1: the two "unknown property" errors carry the key list ---------------
var readMsg = refusal(function () { node.property(cubeId, "colour"); },
                      "node.property with a bad key");
assert(readMsg.indexOf("position") >= 0 && readMsg.indexOf("castShadow") >= 0,
       "…and lists the base rows");
assert(readMsg.indexOf("faceCullingMode") >= 0, "…including the mesh-specific ones");

var writeMsg = refusal(function () { node.setProperty(cubeId, "colour", 1); },
                       "node.setProperty with a bad key");
assert(writeMsg.indexOf("position") >= 0 && writeMsg.indexOf("faceCullingMode") >= 0,
       "…and lists this node's keys");
assert(writeMsg.indexOf("node.properties") >= 0, "…and points at node.properties");

// The list is per NODE TYPE, not a global constant — a light's message names
// light keys the cube's does not.
var lightMsg = refusal(function () { node.setProperty(lightId, "colour", 1); },
                       "a bad key on a light");
assert(lightMsg.indexOf("spotCutOff") >= 0 && lightMsg.indexOf("faceCullingMode") < 0,
       "…a light's list is the light's own");

// ---- #9: material.properties ------------------------------------------------
var mat = material.properties(cubeId);
assert(mat["class"] === "PbrMaterial", "a primitive's material is a PbrMaterial");
assert(mat.rows.length >= 8, "…with declared rows: " + mat.rows.length);
var mrows = rowsByName(mat.rows);
assert(mrows.metallic.type === "float" && mrows.metallic.min === 0 && mrows.metallic.max === 1,
       "metallic declares the real 0..1 range PbrMaterial sets");
assert(mrows.emissiveIntensity.max === 10, "emissiveIntensity declares 0..10");
assert(mrows.baseColor.type === "color" && String(mrows.baseColor.value).charAt(0) === "#",
       "baseColor is a colour and reads as #rrggbb: " + mrows.baseColor.value);
assert(!mrows.baseColor.hasOwnProperty("min"), "a colour row declares no numeric range");

// writableKeys is the authoritative list: the row names UNIONED with the PBR
// texture slots. (The spec expected the six slots to be undeclared; at this pin
// PbrMaterial::createProperties does declare all six, so the union is a no-op
// today — writableKeys is still what material.set consults, and the union is
// deduplicated so the error message never prints a key twice.)
assert(mat.writableKeys.indexOf("metallic") >= 0, "writableKeys covers the declared rows");
assert(mat.writableKeys.indexOf("baseColorMap") >= 0 &&
       mat.writableKeys.indexOf("emissiveMap") >= 0,
       "…and every PBR texture slot material.set accepts");
var seen = {};
for (var d = 0; d < mat.writableKeys.length; ++d) {
    assert(!seen[mat.writableKeys[d]], "writableKeys has no duplicates: " + mat.writableKeys[d]);
    seen[mat.writableKeys[d]] = true;
}
for (var k = 0; k < mat.rows.length; ++k)
    assert(mat.writableKeys.indexOf(mat.rows[k].name) >= 0,
           "every declared row is in writableKeys: " + mat.rows[k].name);
// The F7 fix's other half: the legacy spellings are NOT writable here.
assert(mat.writableKeys.indexOf("diffuseTexture") < 0,
       "the legacy shader texture names are NOT offered as writable keys");

// material.set's rejection now names exactly those keys.
var matMsg = refusal(function () { material.set(cubeId, { shininess: 1 }); },
                     "material.set with a bad key");
assert(matMsg.indexOf("metallic") >= 0 && matMsg.indexOf("baseColorMap") >= 0,
       "…and lists the writable keys, texture slots included");
assert(matMsg.indexOf("diffuseTexture") < 0,
       "…and does NOT offer a key the F7 fix makes fail");
// A legacy key still gets its own, more specific message — INCLUDING when its
// value is not a real file, which is the case that used to be swallowed. The
// key was checked after the value, so `{diffuseTexture: "x.png"}` was refused
// with "no texture file or asset 'x.png'" and sent the reader hunting for a
// missing file instead of telling them the slot does not exist. The fix-wave
// suite only ever passed an existing fixture path, so it never saw this.
var legacyMsg = refusal(function () { material.set(cubeId, { diffuseTexture: "x.png" }); },
                        "material.set with a legacy texture name and a bogus path");
assert(legacyMsg.indexOf("legacy shader texture name") >= 0,
       "…and it is the LEGACY-KEY message, not a missing-file message");
assert(legacyMsg.indexOf("baseColorMap") >= 0, "…which points at the PBR slot instead");
var specularMsg = refusal(function () { material.set(cubeId, { specularTexture: "nope" }); },
                          "a legacy key with no PBR equivalent");
assert(specularMsg.indexOf("no PBR equivalent") >= 0, "…and says so plainly");

// The verb refuses a non-mesh rather than answering with an empty object.
refusal(function () { material.properties(lightId); }, "material.properties on a light");

// The declared values are live, not defaults.
material.set(cubeId, { metallic: 0.25 });
assert(rowsByName(material.properties(cubeId).rows).metallic.value > 0.24,
       "material.properties reports the value material.set just wrote");

// ---- #10: the nine world set* aliases ---------------------------------------
// Each alias must move the SAME field its noun-setter does — the assertion that
// catches a delegation wired to the wrong twin.
world.setAmbient("#204060");
assert(world.get().ambient === "#204060", "world.setAmbient moves the ambient colour");
world.setGravity(3.5);
assert(Math.abs(world.get().gravity - 3.5) < 0.001, "world.setGravity moves gravity");
world.setFog({ enabled: true, density: 0.25 });
assert(world.get().fog.enabled === true, "world.setFog moves the fog block");
world.setShadows({ enabled: false });
assert(world.get().shadows === false, "world.setShadows moves the shadow flag");
world.setGi({ mode: "instant_radiosity" });
assert(world.get().gi.mode === "instant_radiosity", "world.setGi moves the GI mode");
world.setAmbientFromSky(false);
assert(world.get().ambientFromSky === false, "world.setAmbientFromSky moves the sky-ambient flag");
world.setSky("color", { color: "#112233" });
assert(world.get().sky.color === "#112233", "world.setSky moves the sky");
assert(world.setMode({ mode: "high" }) === "high", "world.setMode applies a tier");
assert(world.mode() === "high", "…and the noun reads it back");
var fx = world.setPostFx({ exposure: 1.25 });
assert(Math.abs(fx.exposure - 1.25) < 0.001, "world.setPostFx moves the post chain");

// Reading through the alias is the same read (setMode with no argument).
assert(world.setMode() === "high", "world.setMode() with no argument still reads");

// And the aliases are documented as aliases — the docs must not read as
// eighteen unrelated verbs (owner decision D5).
var worldHelp = api.help("world");
assert(worldHelp.indexOf("Alias of world.fog") >= 0,
       "each alias's doc string names its twin");

console.log("PASS: scripting.e2e.introspection");
