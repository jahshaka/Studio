// app.library_keep, PHASE 2 — one arm of Clear Database (ASSETS-HOME-1). The
// shell prepends CLEAR_ASSETS, CLEAR_MATERIALS and the guids keep_populate.js
// printed. The reset runs with exactly those boxes; afterwards EXACTLY the
// unticked storages are there, each row in its home with its members and its
// definition, and nothing else of the user's is.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function homeOf(g) { try { return assets.metadata(g).home; } catch (e) { return ""; } }

var result = app.resetLibrary({ clearAssets: CLEAR_ASSETS, clearMaterials: CLEAR_MATERIALS });
assert(result.ok === true, "app.resetLibrary({clearAssets: " + CLEAR_ASSETS + ", clearMaterials: "
       + CLEAR_MATERIALS + "}) -> ok (" + app.lastError() + ")");
console.log("KEPT_ROWS=" + result.kept.rows);

var keepAssets = !CLEAR_ASSETS, keepMaterials = !CLEAR_MATERIALS;
function expect(guid, wanted, kept, what) {
    var h = homeOf(guid);
    if (kept) assert(h === wanted, what + " is KEPT, in " + wanted + " (" + h + ")");
    else assert(h === "", what + " is CLEARED (" + h + ")");
}
expect(TEXTURE, "assets", keepAssets, "the imported texture");
expect(MODEL, "assets", keepAssets, "the imported model");
expect(RIG, "assets", keepAssets, "the imported rig");
expect(CLIP, "assets", keepAssets, "the imported clip");
expect(SAVED, "assets", keepAssets, "the material saved to Assets");
expect(AVATAR, "avatars", keepAssets, "the avatar (it follows the Assets box)");
expect(MAT, "materials", keepMaterials, "the New Material");
expect(FROMPRESET, "materials", keepMaterials, "the material created from a preset");
expect(PROJMAT, "project", false, "the project's own material");
assert(project.list().length === 0, "every project is cleared");

// THE KEPT BUNDLES ARE WHOLE: every member comes back in its owner's home.
function wholeBundle(guid, wanted, what) {
    var members = materials.members(guid);
    assert(members.length > 0, what + " still names its members (" + members.length + ")");
    members.forEach(function (m) {
        assert(homeOf(m.guid) === wanted, "…" + m.name + " is there, in " + wanted);
    });
}
if (keepMaterials) {
    wholeBundle(FROMPRESET, "materials", "the preset copy");
    wholeBundle(MAT, "materials", "the New Material");
}
if (keepAssets) {
    wholeBundle(SAVED, "assets", "the saved material");
    assert(assets.dependencies(AVATAR).indexOf(RIG) >= 0, "the avatar still names its rig");
}

// THE ASSETS LISTING is exactly the kept Assets rows.
var tiles = assets.list({ scope: "store" }).map(function (r) { return r.guid; });
var wantTiles = keepAssets ? [TEXTURE, MODEL, RIG, CLIP, SAVED] : [];
assert(tiles.length === wantTiles.length
       && wantTiles.every(function (g) { return tiles.indexOf(g) >= 0; }),
       "the Assets page lists exactly the kept imports and saves (" + tiles.length + ")");
console.log("ALL PASS");
