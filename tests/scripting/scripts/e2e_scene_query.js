// scripting.e2e.scene_query — AI_SURFACE_PROGRAM_SPEC lane B #8, verb half.
//
// scene.nodes() used to be one thing: a depth-first dump of EVERY node with its
// transform, which is what describe_scene sent an agent on every single turn.
// This file gates the options that bound and enrich it — {subtree, depth,
// include} — because describe_scene is only their byte-carrying view and must
// not be the only place they are tested.
//
// What matters here, in order of how badly it would hurt to get wrong:
//   - a bounded read SAYS it was bounded (childCount + truncated), so nothing
//     is silently missing;
//   - the enrichment blocks report what the document actually holds;
//   - unknown options and unknown include names are REFUSED, not ignored (the
//     F7/F8 silent-success class).
//
// Every verb here is a document verb -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) < (eps || 1e-3); }
function refused(fn, what) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, what);
}
function byId(rows, id) {
    for (var i = 0; i < rows.length; i++) if (rows[i].id === id) return rows[i];
    return null;
}

var proj = project.create("Scene Query " + Date.now());
assert(proj.length > 10, "project.create -> " + proj);

// A deliberately DEEP tree: root -> a -> b -> c, so a depth bound has
// something to cut.
var a = scene.addEmpty({ position: { x: 1, y: 0, z: 0 } });
node.setProperty(a, "name", "Level A");
var b = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 }, parent: a });
var c = scene.addPrimitive("sphere", { position: { x: 0, y: 1, z: 0 }, parent: b });
var lamp = scene.addLight("spot", { position: { x: 0, y: 5, z: 0 } });
assert(a.length > 10 && b.length > 10 && c.length > 10 && lamp.length > 10,
    "a 3-deep chain plus a spot light");

var root = scene.root();

// ---- the historic shape is untouched -----------------------------------
var all = scene.nodes();
assert(all.length >= 5, "scene.nodes() with no argument is still the whole tree (" + all.length + ")");
var cubeRow = byId(all, b);
assert(cubeRow && cubeRow.parent === a && near(cubeRow.position.y, 1),
    "…with the same {id, name, type, parent, position, rotation, scale} rows");
assert(cubeRow.material === undefined && cubeRow.light === undefined &&
       cubeRow.visible === undefined,
    "…and no enrichment unless it was asked for");
assert(cubeRow.truncated === undefined, "…and no truncation marker on an unbounded read");

// ---- depth --------------------------------------------------------------
var d0 = scene.nodes({ depth: 0 });
assert(d0.length === 1 && d0[0].id === root, "depth 0 is the root alone");
assert(d0[0].truncated === true && d0[0].childCount >= 2,
    "…and it says so: truncated with childCount " + d0[0].childCount);

var d1 = scene.nodes({ depth: 1 });
assert(byId(d1, a) !== null && byId(d1, lamp) !== null, "depth 1 lists the root's children");
assert(byId(d1, b) === null, "…and stops before the grandchildren");
assert(byId(d1, a).truncated === true && byId(d1, a).childCount === 1,
    "the cut-off child carries its own childCount");
assert(byId(d1, lamp).truncated === undefined,
    "a childless node at the boundary is NOT marked truncated");

var d9 = scene.nodes({ depth: 9 });
assert(d9.length === all.length, "a depth deeper than the tree equals the whole tree");
assert(scene.nodes({ depth: -1 }).length === all.length, "depth -1 means unbounded");

// ---- subtree ------------------------------------------------------------
var sub = scene.nodes({ subtree: a });
assert(sub.length === 3 && sub[0].id === a, "subtree starts AT the named node and includes it");
assert(byId(sub, b) !== null && byId(sub, c) !== null, "…and carries its descendants");
assert(byId(sub, lamp) === null, "…and nothing outside it");
var subBounded = scene.nodes({ subtree: a, depth: 1 });
assert(subBounded.length === 2 && byId(subBounded, c) === null, "subtree and depth compose");
refused(function () { scene.nodes({ subtree: "no-such-node" }); },
    "scene.nodes refuses an unknown subtree id");

// ---- include: visibility ------------------------------------------------
var vis = scene.nodes({ include: ["visibility"] });
assert(byId(vis, b).visible === true && byId(vis, b).visibleInScene === true,
    "visibility: a visible node reads visible in the scene");
assert(node.setProperty(a, "visible", false), "hide the PARENT");
vis = scene.nodes({ include: ["visibility"] });
assert(byId(vis, a).visible === false, "the hidden node's own flag is false");
assert(byId(vis, c).visible === true, "the grandchild's OWN flag is still true");
assert(byId(vis, c).visibleInScene === false,
    "…but visibleInScene is false because an ANCESTOR hides it — the answer that " +
    "used to need a manual parent walk");
assert(node.setProperty(a, "visible", true), "unhide it");
assert(byId(scene.nodes({ include: ["visibility"] }), c).visibleInScene === true, "…and it comes back");

// ---- include: lights ----------------------------------------------------
assert(node.setProperty(lamp, "intensity", 3.25), "set the spot's intensity");
assert(node.setProperty(lamp, "lightColor", "#20a0ff"), "and its colour");
assert(node.setProperty(lamp, "spotCutOff", 22), "and its cone");
var lit = byId(scene.nodes({ include: ["lights"] }), lamp);
assert(lit.light !== undefined, "lights: the light node carries a light block");
assert(lit.light.lightType === "spot", "…naming the type as a NAME, never an ordinal");
assert(near(lit.light.intensity, 3.25), "…with the intensity that was set");
assert(lit.light.color === "#20a0ff", "…and the colour, in the surface's colour spelling");
assert(near(lit.light.spotCutOff, 22), "…and the spot cone");
assert(byId(scene.nodes({ include: ["lights"] }), b).light === undefined,
    "a mesh node gets no light block");

// Only the rows that MEAN something for the type: a point light has no cone.
var point = scene.addLight("point", { position: { x: 3, y: 3, z: 3 } });
var pointRow = byId(scene.nodes({ include: ["lights"] }), point);
assert(pointRow.light.lightType === "point" && pointRow.light.spotCutOff === undefined,
    "a point light reports no spot cone (reporting one teaches a model to set a no-op)");
assert(pointRow.light.distance !== undefined, "…but it does report its range");

// ---- include: materials -------------------------------------------------
assert(material.set(b, { baseColor: "#ff8000", metallic: 0.75, roughness: 0.2 }),
    "give the cube a distinctive material");
var mat = byId(scene.nodes({ include: ["materials"] }), b);
assert(mat.material !== undefined, "materials: the mesh node carries a material block");
assert(mat.material["class"] === "pbr", "…naming the class");
assert(mat.material.baseColor === "#ff8000", "…with the base colour that was set");
assert(near(mat.material.metallic, 0.75) && near(mat.material.roughness, 0.2),
    "…and the metallic/roughness");
assert(mat.material.maps !== undefined && mat.material.maps.length === 0,
    "…and an (empty) list of the texture slots in use");
assert(byId(scene.nodes({ include: ["materials"] }), lamp).material === undefined,
    "a light gets no material block");

// ---- several includes at once, and the refusals -------------------------
var both = scene.nodes({ include: ["materials", "lights", "visibility"], depth: 9 });
assert(byId(both, b).material !== undefined && byId(both, b).visible === true,
    "the includes compose on one row");
assert(byId(both, lamp).light !== undefined, "…across node types");

refused(function () { scene.nodes({ include: ["textures"] }); },
    "scene.nodes refuses an unknown include name");
refused(function () { scene.nodes({ inculde: ["lights"] }); },
    "scene.nodes refuses a misspelled option key");
refused(function () { scene.nodes({ depth: "deep" }); },
    "scene.nodes refuses a non-numeric depth");
refused(function () { scene.nodes("everything"); },
    "scene.nodes refuses a non-object argument");
// …and a refusal changes nothing.
assert(scene.nodes().length === all.length + 1,
    "the tree is exactly what it was, plus the point light");

console.log("scene_query: scene.nodes({subtree, depth, include}) verified");
