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
// scene blob these archives carry goes back out byte-identical, and the ONLY
// intended difference is jah.manifest.json's new "scene" object:
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
