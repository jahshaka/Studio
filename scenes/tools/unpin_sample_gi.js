// RE-AUTHORS three shipped samples' archives and re-shoots their previews,
// around whatever world-level change is being applied to them.
//
// AS OF OWNER DECISION D8 (2026-09-13) THE UNPIN STEP ITSELF IS A NO-OP: the
// document carries no GI bounds at all any more (the World panel's Min/Max
// rows, its Fit button, world.fitGiBounds and world.gi's boundsMin/boundsMax/
// autoBoundsMax keys are deleted under the CRUD law), so there is nothing left
// to clear and an old scene with a pin opens unpinned by itself. The tool is
// KEPT for its pattern — open, measure, save, preview, export, re-import,
// verify — which is what re-authoring a shipped sample looks like.
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed -e "s|@TREE@|$TREE|" -e "s|@MODE@|apply|" -e "s|@OUT@|<a scratch dir>|" \
//       $TREE/scenes/tools/unpin_sample_gi.js > /tmp/unpin.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/unpin.js --data-root <a scratch root>
//
// @MODE@ = "measure" reads the two volumes and photographs both states without
// writing anything — the same run, minus project.save, the preview and
// project.exportArchive. It is how this lane proved the fit before it landed.
//
// NOT --headless: giStatus's fit is a RENDERER measurement and the previews are
// real frames.
//
// WHY IT WAS WRITTEN (owner, 2026-09-13): "we need to get rid of the boundaries
// for GI — a room built from zero must never carry a room size". Mirror Room,
// Showroom and Showroom 2 each shipped with world.gi({boundsMin, boundsMax})
// pinned to the room their generator had just built. A pin is a promise about a scene that
// only holds while nothing moves: build a wall outside it and the wall is
// unlit, drag the room and the lighting stays behind, and — the reason the pins
// were written in the first place — an early automatic fit spread the probes
// over inflated bounds. That last reason is gone: the fit is re-measured on
// every rebuild AND on every reuse-arm refresh, it is centred on the scene's
// CONTENT with the outlier trim taking the empty acres off the ground plane,
// and the probe region is a separate, tighter reading of the room's own walls.
// So the samples stop saying how big their room is and let the renderer measure
// it, like every scene a user makes.
//
// WHAT IS NOT TOUCHED: the probe grid (pccGrid is a COUNT — an authored choice
// about how many photographs the room gets, not a boundary), the tier, the
// camera, the materials, the content. The generators keep their grids too.
//
// IDEMPOTENT: a second run finds the volume already automatic, measures the
// same fit, re-shoots the same picture and re-exports the same archive.

var TREE = "@TREE@";
var MODE = "@MODE@";
var OUT  = "@OUT@";

// Each sample keeps the SIZE and the GRADE its preview shipped at. "viewport"
// (the boolean the generators pass) is what these three were photographed
// with, and this run is about GI and nothing else: changing the grade here
// would re-develop the picture as well as re-light it.
var SAMPLES = [
    { name: "Mirror Room", archive: "Mirror Room.zip", preview: "mirrorroom.png", w: 1280, h: 720 },
    { name: "Showroom",    archive: "Showroom.zip",    preview: "showroom.png",   w: 1280, h: 720 },
    { name: "Showroom 2",  archive: "Showroom 2.zip",  preview: "showroom2.png",  w: 1280, h: 720 }
];

// Centre plus the four quarters, at the sample's own saved camera.
var PROBES = [{ x: 0.5, y: 0.5 }, { x: 0.25, y: 0.25 }, { x: 0.75, y: 0.25 },
              { x: 0.25, y: 0.75 }, { x: 0.75, y: 0.75 }];



function log(m) { console.log("[unpin] " + m); }
function fail(m) { throw new Error("unpin: " + m); }
function J(x) { return JSON.stringify(x); }
function r2(v) { return Math.round(v * 100) / 100; }
function vec(v) { return "(" + r2(v.x) + ", " + r2(v.y) + ", " + r2(v.z) + ")"; }
function size(a, b) { return "(" + r2(b.x - a.x) + " x " + r2(b.y - a.y) + " x " + r2(b.z - a.z) + ")"; }

function giLine(tag, st) {
    log(tag + ": bounds " + vec(st.boundsMin) + " .. " + vec(st.boundsMax) +
        " size " + size(st.boundsMin, st.boundsMax) +
        " voxelMetres " + r2(st.voxelMetres) +
        " | probes " + st.probeCount + " region " + vec(st.probeRegionMin) + " .. " +
        vec(st.probeRegionMax) + " enclosedAxes " + st.probeEnclosedAxes +
        " gridRefused " + st.probeGridRefused + " clamped " + st.probesClampedToRegion +
        " pccBound " + st.pccBound + " vctBound " + st.vctBound + " ifdBound " + st.ifdBound);
}

function shoot(sample, tag) {
    var shot = editor.screenshot(OUT + "/" + sample.replace(/ /g, "_") + "-" + tag + ".png",
                                 1280, 720, PROBES, "scene");
    var line = "";
    for (var i = 0; i < shot.probes.length; i++) {
        var p = shot.probes[i];
        line += (i ? "  " : "") + Math.round(p.r) + "," + Math.round(p.g) + "," + Math.round(p.b);
    }
    log(sample + " pixels [" + tag + "] scene grade: " + line);
    return shot.probes;
}

for (var i = 0; i < SAMPLES.length; i++) {
    var S = SAMPLES[i];
    var zip = TREE + "/scenes/" + S.archive;
    log(S.name + ": importing " + zip);

    var imported = project.importArchive(zip);
    if (!imported || !imported.guid) fail(S.name + ": importArchive failed");
    if (project.open(imported.guid) !== true) fail(S.name + ": open failed");

    var nodes = scene.nodes().length;
    if (nodes < 2) fail(S.name + ": opened with " + nodes + " nodes — that is not the sample");
    log(S.name + ": " + nodes + " nodes, camera " + J(editor.camera().position) +
        " fov " + editor.camera().fov);

    // The sample's own picture: no selection, no light wires, Game View — the
    // frame its preview ships, which is also where the pixels are read.
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(120, 1.0 / 60.0);

    giLine(S.name + " AS SHIPPED", world.giStatus());
    shoot(S.name, "pinned-1");
    editor.frame(60, 1.0 / 60.0);
    // The second shot of the SAME state: these samples are not open-to-open
    // pixel deterministic (VCT + planar reflectors), so the pinned pair is the
    // churn the unpinned pair has to be read against.
    shoot(S.name, "pinned-2");

    // ---- the unpin: A NO-OP SINCE OWNER DECISION D8 (2026-09-13) -------------
    // The document has no bounds fields left to clear — the user-facing bounds
    // controls (the World panel rows, the Fit button, world.fitGiBounds and
    // world.gi's boundsMin/boundsMax/autoBoundsMax keys) are DELETED, an old
    // scene with a pin opens unpinned, and every scene's lit volume is the
    // renderer's automatic fit. The step stays here, empty, because the rest of
    // this tool is the RE-AUTHORING PATTERN (open -> measure -> save -> preview
    // -> export -> re-import -> verify) and that is what it is kept for.
    editor.frame(120, 1.0 / 60.0);
    giLine(S.name + " AUTOMATIC", world.giStatus());
    shoot(S.name, "auto-1");
    editor.frame(60, 1.0 / 60.0);
    shoot(S.name, "auto-2");

    if (MODE !== "apply") {
        log(S.name + ": MEASURE ONLY — nothing written");
        editor.gameView(false);
        if (project.close() !== true) fail(S.name + ": close failed");
        continue;
    }

    if (project.save() !== true) fail(S.name + ": save failed");

    // ---- the shipped preview ------------------------------------------------
    // Game View (already on), the SAVED camera, the size and the grade the
    // preview shipped at: the only thing that moved is the lit volume.
    editor.frame(60, 1.0 / 60.0);
    var shot = editor.screenshot(TREE + "/scenes/preview/" + S.preview,
                                 S.w, S.h, [{ x: 0.5, y: 0.5 }], true);
    log(S.name + ": preview " + S.w + "x" + S.h + " centre " + J(shot.center));
    editor.gameView(false);

    // ---- the archive --------------------------------------------------------
    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(S.name + ": exportArchive failed");
    log(S.name + ": wrote " + out.path + " (" + out.assets + " assets, " +
        out.objects + " objects)");
    if (project.close() !== true) fail(S.name + ": close failed");

    // ---- and it survived the round trip -------------------------------------
    var back = project.importArchive(zip);
    if (!back || !back.guid) fail(S.name + ": re-import failed");
    if (project.open(back.guid) !== true) fail(S.name + ": re-open failed");
    editor.frame(120, 1.0 / 60.0);
    giLine(S.name + " REOPENED", world.giStatus());
    if (project.close() !== true) fail(S.name + ": close (after re-import) failed");
}

log(MODE === "apply" ? "the three samples measure their own lit volume now"
                     : "measured only — no archive or preview was written");
