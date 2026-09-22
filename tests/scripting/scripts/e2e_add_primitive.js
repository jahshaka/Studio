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
// THE ROWS ARE KEYED BY TRIANGLE COUNT, NOT BY NAME, and that is a finding rather
// than a preference: `perObject[].name` is documented as the object's name but is
// filled from the Ogre Item's name (irisgl/engine/src/OgreMesh.cpp objectLods),
// which the mirror never sets — every row comes back with an EMPTY name. The
// authored triangle count identifies each shipped primitive uniquely (960, 768,
// 1,536, 1,024 for the four below), so the claim is asserted exactly; the empty
// name is reported for whoever owns the readout.

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

console.log("PASS");
