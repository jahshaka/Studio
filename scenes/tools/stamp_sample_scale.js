// Stamps the SCENE-SCALE BLOCK into the shipped sample archives' manifests.
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/stamp_sample_scale.js > /tmp/stamp.js
//   cd <a scratch dir>
//   HOME=<a scratch home> <build>/bin/Jahshaka --script /tmp/stamp.js --headless
//
// HEADLESS is enough: import, open, export are document verbs, and the scale
// block is measured from the document (services/sceneextents.h) — the stand-in
// viewport a headless run boots holds the scene and the saved camera, which is
// everything the measurement needs. Nothing renders, nothing is saved: the
// scene blob these archives carry goes back out unchanged unless the identity
// pass below has something to fix, and the intended difference is
// jah.manifest.json's new "scene" object:
//
//   "scene": { "units": "meters", "unitScale": 1.0,
//              "extent": { "min": [...], "max": [...], "size": [...] },
//              "camera": { "fov": 45.0, "height": 2.5 } }
//
// WHY: a consumer that wants to LABEL a scene — the sample dialog's tiles, the
// Ogre-samples tab's ports — is looking at an archive on disk before any
// project is open, so the document is unreachable and the manifest is the only
// place the answer can live. See SPECS / CLAUDE.md for the convention itself
// (owner 2026-09-08: 1 unit = 1 metre, human-scale rooms, 45-degree lens).
//
// The three samples with generators of their own (make_grand_showroom.js,
// make_mirror_room.js, restage_skeletal_scale.js) get the block from those, so
// this tool covers the four that are content rather than script — but running
// it over all seven is harmless and idempotent.

var SAMPLES = ["Matcaps", "Particles", "Physics", "World Background"];

// NODE IDENTITY, fixed here because it is fixed in the DATA (owner's naming
// style, 2026-09-09: the Unreal numeric suffix). World Background shipped three
// meshes ALL called "SceneNode24" — an importer leftover — and the first of them
// carried an EMPTY guid, so scene.find("SceneNode24") returned "" and no
// guid-addressed verb could reach it at all. The missing guid is repaired in the
// READER now (a node without one is unaddressable, so one is minted); the NAMES
// are content and belong here. Anything else that shares a name with a sibling
// gets the same treatment automatically, below.
var RENAMES = { "World Background": { "SceneNode24": "Dragon" } };

var TREE = "@TREE@";

function log(m) { console.log("[stamp] " + m); }
function fail(m) { throw new Error("stamp: " + m); }

for (var i = 0; i < SAMPLES.length; i++) {
    var name = SAMPLES[i];
    var zip = TREE + "/scenes/" + name + ".zip";

    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(name + ": importArchive failed");
    if (project.open(imported.guid) !== true) fail(name + ": open failed");

    var nodes = scene.nodes().length;
    if (nodes < 2) fail(name + ": opened with " + nodes + " nodes — that is not the sample");

    // ---- identity: every node addressable, every sibling name unique --------
    // Renames first (the semantic ones), then the numeric suffix for anything
    // still duplicated. "Dragon", "Dragon2", "Dragon3" — no "Dragon1".
    var rows = scene.nodes();
    var wanted = RENAMES[name] || {};
    var seen = {}, renamed = 0;
    for (var n = 0; n < rows.length; n++) {
        var row = rows[n];
        if (!row.id) fail(name + ": node '" + row.name + "' has no guid — the reader " +
                          "should have minted one (src/io/scenereader.cpp)");
        var base = wanted[row.name] || row.name;
        // SIBLINGS, not the whole scene: two nodes with the same name under
        // DIFFERENT parents are addressable by path and are how the Particles
        // sample legitimately carries a "Fire" on the sphere and a "Fire" on the
        // fire pit. Two under the SAME parent are a coin toss for every verb.
        var key = (row.parent || "") + "|" + base;
        var target = base;
        if (seen[key]) target = base + (seen[key] + 1);
        seen[key] = (seen[key] || 0) + 1;
        if (target !== row.name) {
            if (!node.setProperty(row.id, "name", target))
                fail(name + ": could not rename '" + row.name + "' to '" + target + "'");
            log(name + ": renamed '" + row.name + "' -> '" + target + "'");
            renamed++;
        }
    }
    // A rename is a document edit, so this sample DOES need saving before it is
    // exported — the four this tool covers are otherwise untouched.
    if (renamed > 0 && project.save() !== true) fail(name + ": save failed");

    // The measurement the manifest will carry, logged so a re-run is auditable.
    var rootId = scene.root();
    var ids = [];
    var rows = scene.nodes({ depth: 1 });
    for (var r = 0; r < rows.length; r++) {
        if (rows[r].id === rootId || rows[r].name === "Ground") continue;
        ids.push(rows[r].id);
    }
    log(name + ": " + nodes + " nodes, extent " + JSON.stringify(scene.bounds({ nodes: ids }).size) +
        ", lens " + editor.camera().fov + " deg");

    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(name + ": exportArchive failed");
    log(name + ": wrote " + out.path + " (" + out.assets + " assets, " + out.objects + " objects)");

    if (project.close() !== true) fail(name + ": close failed");
}

log("scale block stamped into " + SAMPLES.length + " archives");
