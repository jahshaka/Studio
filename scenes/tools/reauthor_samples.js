// RE-SHOOTS AND RE-EXPORTS ONE SHIPPED SAMPLE ON TODAY'S BUILD (the owner's
// forward-only rule: "samples are re-authored when a build finishes"). Run it,
// never hand-edit the archives. ONE SAMPLE PER RUN, ON A VIRGIN HOME (a library
// holding a second copy of an archive's bytes would answer content lookups for
// both):
//
//   TREE=<absolute path to the source tree>   OUT=<a scratch dir>
//   for S in Matcaps "Mirror Room" Particles Physics Showroom "Showroom 2" \
//            "Skeletal Animation" "World Background"; do
//       sed -e "s|@TREE@|$TREE|" -e "s|@SAMPLE@|$S|" -e "s|@OUT@|$OUT|" \
//           $TREE/scenes/tools/reauthor_samples.js > $OUT/re.js
//       rm -rf $OUT/home; mkdir -p $OUT/home/run
//       ( cd $OUT/home/run && HOME=$OUT/home DISPLAY=<your Xvfb, 1920x1080> \
//             <build>/bin/Jahshaka --script $OUT/re.js --data-root $OUT/home/data ) \
//           | tee "$OUT/$S.log"
//   done
//
// NOT --headless: the previews are real frames, and the drag-in check renders.
//
// What a run does, on the archive as it ships: drops any project Tile.png row
// the default floor does not wear (an archive carries every row of its
// project, so a stray checker row is shipped dead weight — and a tray tile
// e2e_tray_panel's closure check names), re-shoots the preview at the
// encoded display, re-exports the archive, and proves the round trip: the
// reopened sample has 0 UI-thread parses, 0 scene issues, ONE checker row, and
// every library object drags in.
//
// THE SHIPPED SAMPLES' WORLD (SAMPLES-1, 2026-10-01 — what a new edit should
// keep): every sample stands on the Basic template's floor (scene.addFloor),
// filed under its "World" group, carrying what it authored on its ground
// (Showroom's glossy lobe, Showroom 2's 8 m checker, Physics' bounce, Skeletal
// Animation's body-less floor, World Background's deck plate). HEIGHT FOG is on
// in the four open scenes under a procedural sky (Matcaps, Particles, Physics,
// Skeletal Animation); the three walled rooms have none, and neither does
// World Background, whose sky is a photograph the fog would paint over. All
// eight are MANUAL at 0 stops, the re-derived default exposure chain.

var TREE    = "@TREE@";
var SAMPLE  = "@SAMPLE@";
var OUT     = "@OUT@";

// name -> the preview it ships, at the size and grade it ships at, and the
// warm-up its content needs before the shutter (a particle sample photographs
// as an empty scene until the plumes have filled).
var PREVIEWS = {
    "Matcaps":            { file: "matcaps.png",    w: 460,  h: 215,  warm: 120, grade: "scene" },
    "Particles":          { file: "particles.png",  w: 480,  h: 270,  warm: 240, grade: "scene" },
    "Physics":            { file: "physics.png",    w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Skeletal Animation": { file: "skeletal.png",   w: 1280, h: 720,  warm: 120, grade: "scene" },
    "World Background":   { file: "world.png",      w: 1920, h: 1080, warm: 120, grade: "scene" },
    "Mirror Room":        { file: "mirrorroom.png", w: 1280, h: 720,  warm: 120, grade: true },
    "Showroom":           { file: "showroom.png",   w: 1280, h: 720,  warm: 120, grade: true },
    "Showroom 2":         { file: "showroom2.png",  w: 1280, h: 720,  warm: 120, grade: true }
};

var PROBES = [{ x: 0.5, y: 0.5 }, { x: 0.25, y: 0.25 }, { x: 0.75, y: 0.25 },
              { x: 0.25, y: 0.75 }, { x: 0.75, y: 0.75 }];

var P = PREVIEWS[SAMPLE];
var SLUG = SAMPLE.replace(/ /g, "_");
var ZIP = TREE + "/scenes/" + SAMPLE + ".zip";

function log(m) { console.log("[reauthor] " + SAMPLE + ": " + m); }
function fail(m) { throw new Error("reauthor: " + SAMPLE + ": " + m); }
function J(x) { return JSON.stringify(x); }
function r3(v) { return Math.round(v * 1000) / 1000; }

if (!P) fail("not a shipped sample");

function compose(warm) {
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(warm, 1.0 / 60.0);
}

function shoot(tag) {
    var shot = editor.screenshot(OUT + "/" + SLUG + "-" + tag + ".png", 1280, 720, PROBES, "scene");
    var line = [];
    for (var i = 0; i < shot.probes.length; i++) {
        var p = shot.probes[i];
        line.push(Math.round(p.r) + "," + Math.round(p.g) + "," + Math.round(p.b));
    }
    log("probes [" + tag + "] " + line.join("  "));
}

function defaultFloor() {
    var floors = scene.nodes().filter(function (r) {
        return r.type === "mesh" && node.property(r.id, "defaultFloor") === true;
    });
    if (floors.length !== 1) fail(floors.length + " default floors — one floor per sample");
    return floors[0].id;
}

function checkerRows() {
    return assets.list({ scope: "project", type: "texture", query: "Tile.png" })
        .filter(function (r) { return r.name === "Tile.png"; });
}

function openArchive(path) {
    var imported = project.importArchive(path);
    if (!imported || !imported.guid) fail("importArchive failed: " + path);
    if (imported.bakeFailures && imported.bakeFailures.length)
        fail("the archive import could not bake " + J(imported.bakeFailures));
    app.openStats({ reset: true });
    if (project.open(imported.guid) !== true) fail("open failed");
    if (scene.nodes().length < 2) fail("opened with " + scene.nodes().length + " nodes");
    return imported;
}

// ---- the archive as it ships: tidy, re-shoot, re-export ----------------------
openArchive(ZIP);
var floor = defaultFloor();
var worn = "" + ((node.serialize(floor).node.material || {}).values || {}).baseColorMap;
var stale = checkerRows().filter(function (r) { return r.guid !== worn; });
stale.forEach(function (r) {
    if (assets.removeFromProject(r.guid) !== true) fail("could not drop the stale Tile.png row " + r.guid);
});
log("CHECKER ROWS: " + stale.length + " unused Tile.png row(s) dropped; the floor wears " + (worn || "(none)"));
if (project.save() !== true) fail("save failed");

compose(P.warm);
shoot("after");
editor.frame(60, 1.0 / 60.0);
var shot = editor.screenshot(TREE + "/scenes/preview/" + P.file, P.w, P.h, [{ x: 0.5, y: 0.5 }], P.grade);
log("preview " + P.file + " " + P.w + "x" + P.h + " centre " + J(shot.center));
editor.gameView(false);

var out = project.exportArchive(ZIP);
if (!out || !out.path) fail("exportArchive failed");
log("wrote " + out.path + " (" + out.assets + " assets, " + out.objects + " objects)");
if (project.close() !== true) fail("close failed");

// ---- the round trip: what the shipped archive opens as -------------------
openArchive(ZIP);
editor.frame(120, 1.0 / 60.0);
var stats = app.openStats();
var issues = editor.issues();
var missing = issues.filter(function (i) { return i.kind === "model.missing"; });
log("REOPENED: " + scene.nodes().length + " nodes, uiThreadParses " + stats.uiThreadParses +
    ", bakeBuilds " + stats.bakeBuilds + ", model.missing " + missing.length + ", issues " +
    issues.length + " " + J(issues.map(function (i) { return i.kind + ":" + i.nodeName; })));
defaultFloor();
if (checkerRows().length > 1) fail("the reopened archive carries " + checkerRows().length + " Tile.png rows");
if (stats.uiThreadParses !== 0) fail("the open parsed " + stats.uiThreadParses + " model(s)");
if (issues.length) fail("the reopened sample has scene issues");

// ---- the drag-in: every library object arrives rotated and dressed ------
var objects = assets.list({ scope: "project", type: "object" }).filter(function (a) {
    return a.type === "object" || a.type === "Object";
});
var dragged = 0;
objects.forEach(function (a) {
    var id;
    try { id = assets.addToScene(a.guid, { position: { x: 0, y: 0, z: 30 } }); }
    catch (e) { return; }                     // a built-in row has no blob to place
    if (!id) return;
    var parts = [{ id: id, name: node.info(id).name, depth: 0 }].concat(node.components(id));
    var imported = parts.some(function (p) {
        return node.info(p.id).type === "mesh"
            && ("" + node.property(p.id, "meshPath")).indexOf(":/") !== 0;
    });
    if (!imported) { node.remove(id); return; }   // a built-in's row: no blob of its own
    ++dragged;
    var rotated = 0, meshes = [];
    parts.forEach(function (p) {
        var r = node.info(p.id).rotation;
        if (Math.abs(r.x) + Math.abs(r.y) + Math.abs(r.z) > 1e-3) ++rotated;
        if (node.info(p.id).type === "mesh") {
            var m = material.get(p.id);
            meshes.push(p.name + " " + m.baseColor + " r" + r3(m.roughness) + " m" + r3(m.metallic));
        }
    });
    log("DRAG-IN " + a.name + ": " + parts.length + " node(s), " + rotated + " rotated; " +
        meshes.join(" | "));
    node.remove(id);
});
log(dragged + " library object(s) dragged in");
if (project.close() !== true) fail("close (round trip) failed");
log("done");
