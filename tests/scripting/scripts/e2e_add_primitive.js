// scripting.e2e.add_primitive — A PRIMITIVE IS AN ATOM ASSET, THROUGH THE VERB
// (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_ASSET_DESIGN.md §2.3).
//
// `scene.addPrimitive` used to hand the node a mesh parsed from a .obj resource at
// the moment of the add — ~6 ms of surface-card generation on the thread that
// draws, and DELIBERATELY no LOD chain, so every cube in every sample rendered its
// full triangle count at any distance. The primitives are baked library assets now
// (jahshaka/src/services/primitiveassets.h), and this is the claim that says so
// from the outside: the node's mesh reports MORE THAN ONE LEVEL.
//
// The reading is `app.renderStats().perObject`, which reports the level each
// object DREW and how many it has (ATOM P1's AT-A12) — a renderer reading, not a
// document one, so it also proves the chain survived the mirror and the upload.
//
// Small primitives are named and excluded ON PURPOSE: a cube is twelve triangles
// and a plane is two, the bake's chain floor is 512 (a level must be able to halve
// to 128 and still shed 15 %), and "one level" is the honest answer for them —
// `lodLevelCount` is `lodIndices.size() + 1`, so they read as one level and every
// consumer treats them as fine at every distance.
//
// THE ROWS ARE KEYED BY TRIANGLE COUNT: the authored count identifies each shipped
// primitive uniquely (960, 768, 1,536, 1,024 for the four below). The NAME is the
// last arm's subject: the engine's Items are unnamed, and `app.renderStats()`
// resolves each row to the DOCUMENT node's name through the mirror
// (SMALL-FIXES-1 — every row came back with an empty name before).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

assert(project.create("add_primitive") !== false, "project.create");

// The subjects: the shipped primitives whose triangle count is above the bake's
// chain floor, so every one of them owes a chain.
// name -> the mesh's AUTHORED triangle count (level 0), which is what identifies
// its row in the readout.
var CHAINED = { "Sphere": 960, "Hemisphere": 768, "Torus": 1536, "Capsule": 1024 };
var FLAT = { "Cube": 12, "Plane": 2 };
var wanted = Object.keys(CHAINED).concat(Object.keys(FLAT));
for (var i = 0; i < wanted.length; ++i) {
    var id = scene.addPrimitive(wanted[i]);
    assert(id !== false && id !== null, "scene.addPrimitive('" + wanted[i] + "')");
}

// The scene's own answer first: the nodes are there and they carry geometry.
var meshes = 0;
var nodes = scene.nodes();
for (var n = 0; n < nodes.length; ++n) if (nodes[n].type === "mesh") ++meshes;
assert(meshes >= wanted.length,
       "every add landed a mesh node (" + meshes + " in the scene, floor and all)");

editor.frame(30);              // settle: frames, never wall clock

var per = app.renderStats().perObject;
assert(per && per.length > 0, "app.renderStats().perObject reports the frame's objects");
for (var p = 0; p < per.length; ++p)
    console.log("perObject [" + per[p].name + "]: level " + per[p].level + " of "
                + per[p].levels + ", triangles " + per[p].triangles);

function rowDrawing(tris) {
    // At bias 0 every object draws level 0, so its drawn triangle count IS its
    // authored one; before that a row may have settled on a coarser level, which
    // is why the search also accepts a row whose level is not 0.
    for (var r = 0; r < per.length; ++r)
        if (per[r].triangles === tris) return per[r];
    return null;
}

for (var name in CHAINED) {
    var subject = rowDrawing(CHAINED[name]);
    assert(!!subject, name + " (" + CHAINED[name] + " triangles) is in the readout");
    assert(subject.levels > 1,
           name + "'s mesh reports " + subject.levels + " levels — a real chain "
           + "(it reported exactly one before ATOM P2)");
    assert(subject.level >= 0 && subject.level < subject.levels,
           name + " drew level " + subject.level + ", one the chain HAS");
}
for (var flat in FLAT) {
    var small = rowDrawing(FLAT[flat]);
    assert(!!small, flat + " (" + FLAT[flat] + " triangles) is in the readout too");
    assert(small.levels === 1,
           flat + " reports ONE level, which is the honest answer for "
           + small.triangles + " triangles");
}

// AND THE LEVEL IS THE VIEW'S, not a document property: pinning the bias at 0
// draws every object at its finest level (editor.setLodBias), which is how a test
// asserts one picture and how the dolly gate will force the others.
editor.setLodBias(0);
editor.frame(10);
var pinned = app.renderStats().perObject;
var coarse = 0;
for (var q = 0; q < pinned.length; ++q) if (pinned[q].level !== 0) ++coarse;
assert(coarse === 0,
       "at bias 0 every one of the " + pinned.length + " objects draws level 0 (its finest)");

// EVERY OBJECT'S ROW CARRIES THE DOCUMENT'S NAME (SMALL-FIXES-1). Three
// primitives get names nobody else has; each must come back on its own row, and
// every mesh node of the document must be named in the readout — the measurement
// lanes read ATOM's per-object LOD by name. (The readout also lists the editor's
// own drawn helpers — light icons, the sun disc, the horizon — which mirror no
// document node and so carry no name; they are counted, not named.)
var NAMED = { "Sphere": "Probe Alpha", "Torus": "Probe Beta", "Capsule": "Probe Gamma" };
var renamed = {};
for (var prim in NAMED) {
    var nid = scene.find(prim);
    assert(!!nid, "the " + prim + " is in the scene to rename");
    assert(node.rename(nid, NAMED[prim]) === NAMED[prim], prim + " renamed '" + NAMED[prim] + "'");
    renamed[NAMED[prim]] = CHAINED[prim];
}
editor.frame(2);
var meshNames = scene.nodes().filter(function (n) { return n.type === "mesh"; })
                             .map(function (n) { return n.name; });
var named = app.renderStats().perObject;
var rowNames = named.filter(function (row) { return row.name !== ""; })
                    .map(function (row) { return row.name; });
console.log("perObject names: " + J(rowNames) + " (+" + (named.length - rowNames.length)
            + " editor helper rows); document meshes: " + J(meshNames));
assert(rowNames.length === meshNames.length,
       "one NAMED row per document mesh node (" + rowNames.length + " named rows, "
       + meshNames.length + " mesh nodes)");
meshNames.forEach(function (nm) {
    assert(rowNames.indexOf(nm) >= 0, "the mesh node '" + nm + "' has its row, by name");
});
for (var nm in renamed) {
    var hit = named.filter(function (row) { return row.name === nm; });
    assert(hit.length === 1 && hit[0].triangles === renamed[nm],
           "'" + nm + "' is exactly one row, the one drawing its " + renamed[nm] + " triangles");
}

console.log("PASS");
