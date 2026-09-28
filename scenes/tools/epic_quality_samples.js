// THE THREE ROOMS AT EPIC'S OWN QUALITY ROW (D4-PHOTON-TIERS step 1, Epic a
// GiQuality row of its own): re-applies each room's Photon tier, re-shoots its
// preview and re-exports its archive.
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/epic_quality_samples.js > /tmp/epic.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/epic.js --data-root <a scratch root>
//
// NOT --headless: the previews are real frames.
//
// WHAT WAS WRONG: Epic used to be a FLAG on High (GiParams::epicTier, derived
// from the document's giTier), so Mirror Room, Showroom and Showroom 2 were
// written as giTier "epic" beside giQuality "high". Epic is now the fourth
// quality row and a document is read as written (forward-building: no reader
// derives one from the other), so the three opened at High — the tier's
// quality row a deviation (samples.cleanstart.*), their reflections traced one
// ray per 2x2 block instead of every pixel. Re-applying the tier writes the
// row's own quality (epic) into each room; every other tier row is already the
// tier's (the cleanstart suites assert no other deviation).
//
// IDEMPOTENT: a second run finds epic, writes epic, re-shoots the same picture.

var TREE = "@TREE@";

var SAMPLES = [
    { name: "Mirror Room", preview: "mirrorroom.png", w: 1280, h: 720, warm: 120 },
    { name: "Showroom",    preview: "showroom.png",   w: 1280, h: 720, warm: 120 },
    { name: "Showroom 2",  preview: "showroom2.png",  w: 1280, h: 720, warm: 120 }
];

function log(m) { console.log("[epic] " + m); }
function fail(m) { throw new Error("epic: " + m); }
function J(x) { return JSON.stringify(x); }

for (var i = 0; i < SAMPLES.length; i++) {
    var S = SAMPLES[i];
    var zip = TREE + "/scenes/" + S.name + ".zip";
    log(S.name + ": importing " + zip);
    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(S.name + ": importArchive failed");
    if (project.open(imported.guid) !== true) fail(S.name + ": open failed");
    var nodes = scene.nodes().length;
    if (nodes < 2) fail(S.name + ": opened with " + nodes + " nodes — that is not the sample");

    var before = world.photon();
    if (before.tier !== "epic") fail(S.name + ": tier " + before.tier + ", not a room this tool re-authors");
    if (world.gi({ tier: "epic" }) !== true) fail(S.name + ": world.gi({tier: epic}) refused");
    var after = world.photon();
    if (after.quality !== "epic" || after.custom)
        fail(S.name + ": quality " + after.quality + ", deviations " + J(after.deviations) + " after the tier");
    log(S.name + ": " + nodes + " nodes, quality " + before.quality + " -> " + after.quality);

    if (project.save() !== true) fail(S.name + ": save failed");

    // ---- the shipped preview (Game View, the sample's saved camera) --------
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(S.warm);
    var shot = editor.screenshot(TREE + "/scenes/preview/" + S.preview, S.w, S.h,
                                 [{ x: 0.5, y: 0.5 }], true);
    log(S.name + ": preview " + S.w + "x" + S.h + " centre " + J(shot.center));
    editor.gameView(false);

    // ---- the archive ---------------------------------------------------------
    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(S.name + ": exportArchive failed");
    log(S.name + ": wrote " + out.path + " (" + out.assets + " assets, " + out.objects + " objects)");
    if (project.close() !== true) fail(S.name + ": close failed");
}
log("the three rooms carry Epic's quality row; previews re-shot");
