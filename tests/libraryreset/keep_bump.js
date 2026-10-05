// app.library_keep, PHASE 3 — A FORMAT BUMP (ASSETS-HOME-1; the lead's
// acceptance 2026-10-05). The shell boots the populated library with
// JAHSHAKA_TEST_LIBRARY_GENERATION one above this build's generation, so the
// startup check runs the bump this build would run: the catalog is dropped and
// the user's storages are REBUILT from their sidecars — every row back in its
// home with its members, edges and definitions — their bakes re-derived from
// their sources; projects cleared. The shell prepends the guids.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function meta(g) { try { return assets.metadata(g); } catch (e) { return {}; } }

var gen = app.libraryGeneration();
assert(gen.outcome === "wiped" && gen.generation === gen.onDisk + 1,
       "the startup check ran the bump (" + gen.onDisk + " -> " + gen.generation + ", "
       + gen.outcome + ")");
assert(gen.keptRows > 0 && gen.unreadable === 0,
       "…and rebuilt " + gen.keptRows + " storage rows from their sidecars, reading every one");

[[TEXTURE, "assets", "import"], [MODEL, "assets", "import"], [RIG, "assets", "import"],
 [CLIP, "assets", "import"], [SAVED, "assets", "save"], [AVATAR, "avatars", "create"],
 [MAT, "materials", "create"], [FROMPRESET, "materials", "create"]].forEach(function (e) {
    var m = meta(e[0]);
    assert(m.home === e[1] && m.origin === e[2],
           e[0] + " is back in " + e[1] + " as '" + e[2] + "' (" + m.home + "/" + m.origin + ")");
});
assert(meta(PROJMAT).home === undefined && project.list().length === 0,
       "the project and its own material are cleared");

// MEMBERS AND BLOBS: a bundle's definition is readable and names rows that
// exist in its own home.
[[FROMPRESET, "materials"], [MAT, "materials"], [SAVED, "assets"]].forEach(function (e) {
    var members = materials.members(e[0]);
    assert(members.length > 0, e[0] + ": its definition is back and names " + members.length
           + " member(s)");
    members.forEach(function (m) {
        assert(meta(m.guid).home === e[1], "…" + m.name + " is back in " + e[1]);
    });
});
assert(materials.loadGraph(FROMPRESET).nodes >= 3, "the preset copy's GRAPH is back (its blob)");

// EDGES: the avatar names its rig, the model its meshes.
assert(assets.dependencies(AVATAR).indexOf(RIG) >= 0, "the avatar's edge to its rig is back");
assert(assets.dependencies(MODEL).length > 0, "the model's edges to its members are back");

// THE BAKES ARE RE-DERIVED, never read from the old build: the bump dropped
// them, the sweep makes them again from the stored sources.
var before = assets.bakeAll({ dryRun: true });
assert(before.needed > 0, "the bump dropped the derived bakes (" + before.needed + " needed)");
var baked = assets.bakeAll({ dryRun: false });
assert(baked.failed === 0, "…they rebuild from the stored sources (" + baked.baked + " baked)");
assert(assets.bakeAll({ dryRun: true }).needed === 0, "…and none is left to bake");
assert(assets.meshLods(MODEL).length > 0, "the model draws from its rebuilt bake");
console.log("ALL PASS");
