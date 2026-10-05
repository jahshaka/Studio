// app.library_keep, PHASE 1 — a library with something in EVERY home
// (ASSETS-HOME-1). The shell prepends the fixture paths (KEEP_PNG, KEEP_RIG,
// KEEP_CLIP, KEEP_MODEL). Each guid is printed as `NAME=guid` for the arms.
//
// It is also where the door rules are asserted on the gestures themselves:
//   * an import lands in Assets (origin "import");
//   * New Material lands in the Materials storage, NOT Assets;
//   * Create material from a preset is a unique bundle IN FULL — its maps its
//     own rows in its own home, never the preset's platform rows;
//   * Save to Assets copies a material into Assets, members and all;
//   * an avatar lives in the Avatar module's storage.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function store() { return assets.list({ scope: "store", members: true }).map(function (r) { return r.guid; }); }
function home(g) { return assets.metadata(g).home; }

var proj = project.create("Keep Populate");
assert(proj.length > 10, "a project (it is cleared by every reset)");

// ---- imports: Assets, origin import ----------------------------------------
var texture = assets.importFile(KEEP_PNG);
var model = assets.import(KEEP_MODEL);
var rig = assets.import(KEEP_RIG);
var clip = assets.import(KEEP_CLIP);
[texture, model, rig, clip].forEach(function (g) {
    assert(!!g && home(g) === "assets" && assets.metadata(g).origin === "import",
           "an import lands in Assets as an import (" + g + ")");
});

// ---- the avatar: the Avatar module's own storage ----------------------------
var av = avatar.createAsset(rig);
assert(av.length > 10 && home(av) === "avatars", "an avatar lives in the Avatar storage");
assert(store().indexOf(av) < 0, "…and it is not an Assets tile");

// ---- New Material: the Materials storage, never Assets ----------------------
var mat = materials.create("Keep Mat");
materials.addTexture(mat, KEEP_PNG, { slot: "baseColorMap" });
assert(home(mat) === "materials" && store().indexOf(mat) < 0,
       "NEW MATERIAL LANDS IN THE MATERIALS STORAGE, not Assets");
materials.members(mat).forEach(function (m) {
    assert(home(m.guid) === "materials", "…its picture is a Materials row too (" + m.name + ")");
});

// ---- Create material from a preset: a unique bundle in full -----------------
var BRICK = "00000000-0000-0000-0000-000000002014";
materials.seedPresets();
var fromPreset = materials.createFromPreset(BRICK, { name: "Keep Bricks" });
var presetMaps = materials.members(BRICK).map(function (m) { return m.guid; });
var ownMaps = materials.members(fromPreset);
assert(home(fromPreset) === "materials" && ownMaps.length === presetMaps.length && ownMaps.length > 0,
       "CREATE MATERIAL FROM A PRESET: a Materials bundle with its " + ownMaps.length + " maps");
ownMaps.forEach(function (m) {
    assert(presetMaps.indexOf(m.guid) < 0 && home(m.guid) === "materials",
           "…" + m.name + " is the copy's OWN row, not the preset's platform row");
});

// ---- Save to Assets: a full copy in Assets ---------------------------------
var saved = materials.saveToAssets(fromPreset, { name: "Keep Saved" });
assert(home(saved) === "assets" && assets.metadata(saved).origin === "save"
       && store().indexOf(saved) >= 0, "SAVE TO ASSETS puts a copy in Assets (origin save)");
materials.members(saved).forEach(function (m) {
    assert(home(m.guid) === "assets" && ownMaps.map(function (o) { return o.guid; }).indexOf(m.guid) < 0,
           "…with its OWN members in Assets (" + m.name + ")");
});
assert(home(fromPreset) === "materials", "…and the original stays in Materials");

// ---- a project's own material (cleared by every reset) ---------------------
var projMat = materials.create("Keep Project Mat", { folder: "" });
assert(home(projMat) === "project", "the editor's material is the project's own");

console.log("TEXTURE=" + texture);
console.log("MODEL=" + model);
console.log("RIG=" + rig);
console.log("CLIP=" + clip);
console.log("AVATAR=" + av);
console.log("MAT=" + mat);
console.log("FROMPRESET=" + fromPreset);
console.log("SAVED=" + saved);
console.log("PROJMAT=" + projMat);
console.log("ALL PASS");
