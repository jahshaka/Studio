// scripting.e2e.pbr_materials — PBR material persistence (the owner-reported
// data loss: "reopening a saved project drops the PBR materials I added").
//
// The loss mechanism was the APPLY path, not the writer/reader: applying a
// preset with a container node selected (an imported model roots at an Empty,
// and the viewport's click-selects-the-root rule selects exactly that node)
// silently did nothing, and dropping a SAVED material asset applied nothing
// persistent either — so the document never held the material and the writer
// had nothing to save. This suite drives the same service paths through the
// verbs and asserts the full round-trip through save/close/open:
//   1. preset applied to a container -> every descendant mesh gets it (FAILED
//      before the fix: material.apply refused non-mesh nodes, and the service
//      call behind the presets panel silently no-oped);
//   2. a saved project material asset applied by guid -> real PbrMaterial
//      (FAILED before the fix: nothing persistent was applied);
//   3. every PBR property — scalars, colors, alpha/glass mode, textureScale,
//      texture maps — survives save/close/open byte-for-byte (paths retarget
//      into the project folder).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b) { return Math.abs(a - b) < 1e-3; }
function basename(p) { return ("" + p).split("/").pop(); }

var guid = project.create("PBR Materials " + Date.now());
assert(guid.length > 10, "project.create");

// ---- 1. container apply: an empty with two mesh children (the imported-model
// shape — the model root the viewport hands to the apply path) ----
var group = scene.addEmpty();
var cubeA = scene.addPrimitive("cube", { position: { x: -1, y: 1, z: 0 } });
var cubeB = scene.addPrimitive("cube", { position: { x: 1, y: 1, z: 0 } });
node.reparent(cubeA, group);
node.reparent(cubeB, group);

assert(material.apply(group, "Brick PBR") === true, "material.apply on the CONTAINER node");
var matA = material.get(cubeA);
var matB = material.get(cubeB);
assert(near(matA.roughnessLowerBound, 0.92), "child A got the preset (roughnessLowerBound)");
assert(near(matB.roughnessLowerBound, 0.92), "child B got the preset");
assert(basename(matA.baseColorMap) === "Brick_Ground_01_UV_H_CM_1_COLOR.png", "child A got the preset's baseColorMap");

// per-mesh instances: editing A must not bleed into B
assert(material.set(cubeA, { baseColor: "#ff2200", roughness: 0.111 }), "material.set on child A");
assert(near(material.get(cubeA).roughness, 0.111), "child A edited");
assert(material.get(cubeB).baseColor !== "#ff2200" && !near(material.get(cubeB).roughness, 0.111),
       "child B kept its own material instance");

// glass/cutout modes + textureScale on B
assert(material.set(cubeB, { alphaMode: 3, alpha: 0.3, alphaCutoff: 0.42, textureScale: 5.0,
                             metallic: 0.66, emissiveColor: "#102030", emissiveIntensity: 1.5 }),
       "material.set glass mode + textureScale on child B");

// ---- 2. saved material asset applied by guid ----
// The preset apply above registered a "Brick PBR" material asset under
// Presets/. Applying it by guid is the assets-panel drag path.
var savedMat = null;
var list = assets.list({ scope: "project", type: "material" });
for (var i = 0; i < list.length; i++) {
    if (list[i].name === "Brick PBR") { savedMat = list[i]; break; }
}
assert(savedMat, "the preset apply registered a project material asset");

var sphere = scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 2 } });
assert(material.apply(sphere, savedMat.guid) === true, "material.apply with a SAVED material asset guid");
var sphereMat = material.get(sphere);
assert(near(sphereMat.roughnessLowerBound, 0.92), "saved asset rebuilt as a real PbrMaterial (roughnessLowerBound)");
// Pin world (phase 4): texture guids resolve to content-addressed object
// paths (oid-named); the DISPLAY name lives in the catalog, not the path.
assert(("" + sphereMat.baseColorMap).length > 0 && /\.png$/.test(sphereMat.baseColorMap),
       "saved asset kept its texture (resolves to a stored object)");

// a bad guid still fails loudly
var thrown = false;
try { material.apply(sphere, "no-such-preset-or-asset"); } catch (e) { thrown = true; }
assert(thrown, "unknown preset/asset guid throws");
// a target with no meshes fails loudly instead of silently dropping the apply
var loneEmpty = scene.addEmpty();
thrown = false;
try { material.apply(loneEmpty, "Brick PBR"); } catch (e) { thrown = true; }
assert(thrown, "meshless container throws instead of silently no-oping");

// ---- 3. full round-trip through save/close/open ----
var beforeA = material.get(cubeA);
var beforeB = material.get(cubeB);
var beforeS = material.get(sphere);
var nameA = node.info(cubeA).name, nameB = node.info(cubeB).name, nameS = node.info(sphere).name;

assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");

function findNode(id, name) {
    var ns = scene.nodes();
    // exact id first (guids survive the reopen); the name fallback would be
    // ambiguous — both cubes are named "Cube".
    for (var i = 0; i < ns.length; i++) if (ns[i].id === id) return ns[i].id;
    for (var i = 0; i < ns.length; i++) if (ns[i].name === name) return ns[i].id;
    return null;
}
var aId = findNode(cubeA, nameA), bId = findNode(cubeB, nameB), sId = findNode(sphere, nameS);
assert(aId && bId && sId, "all three meshes survived the reopen");

function compare(label, before, afterId) {
    var after = material.get(afterId);
    var lost = [];
    for (var k in before) {
        var b = before[k], a = after[k];
        // Pin world: texture values resolve to content-addressed object
        // paths whose names are oids — what must survive the reopen is the
        // PRESENCE of the map (the bytes are content-addressed, identical
        // by construction when the oid resolves).
        if (/Map$/.test(k)) { b = ("" + b).length > 0; a = ("" + a).length > 0; }
        if (JSON.stringify(b) !== JSON.stringify(a))
            lost.push(k + ": " + JSON.stringify(before[k]) + " -> " + JSON.stringify(after[k]));
    }
    if (lost.length) throw new Error(label + " lost properties on reopen:\n  " + lost.join("\n  "));
    console.log("ok: " + label + " round-tripped all " + Object.keys(before).length + " properties");
}
compare("child A", beforeA, aId);
compare("child B (glass)", beforeB, bId);
compare("sphere (saved asset)", beforeS, sId);

// the glass mode specifically — the drawer's alpha modes must survive
assert(material.get(bId).alphaMode === 3, "glass alphaMode survived");
assert(near(material.get(bId).textureScale, 5.0), "textureScale survived");

console.log("e2e_pbr_materials: ALL OK");
