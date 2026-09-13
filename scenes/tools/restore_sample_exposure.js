// Puts the DOCUMENT'S OWN EXPOSURE back into the five samples that shipped dark,
// and re-shoots their previews at the picture the app now gives.
//
// Run it, do not hand-edit the archives:
//
//   TREE=<absolute path to the source tree>
//   sed "s|@TREE@|$TREE|" $TREE/scenes/tools/restore_sample_exposure.js > /tmp/exposure.js
//   cd <a scratch dir>
//   HOME=<a scratch home> DISPLAY=<your Xvfb, 1920x1080> \
//       <build>/bin/Jahshaka --script /tmp/exposure.js --data-root <a scratch root>
//
// NOT --headless: the previews are real frames off the on-screen viewport, and
// the "scene" grade photographs at the exposure that viewport has converged on,
// which is the whole point of re-shooting them here.
//
// WHAT WAS WRONG (2026-09-13, the darkness set — the evidence is in
// spikes/render-review-2026-09-13/darkness/INDEX.md, the fix in
// spikes/exposure-1/): these five archives carry `"exposure": 0` in their saved
// scene, EXPLICITLY, while Mirror Room, Showroom and Showroom 2 carry the
// document default of 0.6 and every scene the app creates carries 0.6. The
// shader's exposure term is 1024*e^(exposure-2) over the scene's average
// log-luminance, and at 0 the filmic curve lands mid-grey at about 0.31: the
// five shipped a picture roughly half as bright as the same scene made today.
// Measured on Physics, mean Rec.709 luma 18.5 at 0 versus 38.1 at 0.6; on World
// Background 28.0 versus 52.1. Tier, sun, ambient and the ground material were
// each A/B'd and refuted — it is the grade, and only the grade.
//
// The reader's fallback for an ABSENT exposure key was 0.0 too, disagreeing
// with the constructor's 0.6 (src/io/scenereader.cpp, this lane) — but these
// five say 0 out loud, so the reader fix alone cannot reach them. This is the
// re-authoring half.
//
// THE OLD Showroom IS NOT IN THE LIST, deliberately: it is the owner's
// reference (scenes/tools/make_grand_showroom.js says so in as many words) and
// it already carries 0.6.
//
// IDEMPOTENT: a second run finds 0.6, writes 0.6, re-shoots the same picture
// and re-exports the same archive.

var TREE = "@TREE@";
var EXPOSURE = 0.6;          // iris::Scene's constructor value, scene.cpp:161

// Each sample keeps the SIZE its preview shipped at (the tiles scale, and a
// re-shoot is not the place to change a thumbnail's aspect), and gets the warm-
// up its content needs before the shutter: a particle sample photographs as an
// empty scene until the plumes have filled.
var SAMPLES = [
    { name: "Matcaps",            preview: "matcaps.png",   w: 460,  h: 215,  warm: 120 },
    { name: "Particles",          preview: "particles.png", w: 480,  h: 270,  warm: 240 },
    { name: "Physics",            preview: "physics.png",   w: 1920, h: 1080, warm: 120 },
    { name: "Skeletal Animation", preview: "skeletal.png",  w: 1280, h: 720,  warm: 120 },
    { name: "World Background",   preview: "world.png",     w: 1920, h: 1080, warm: 120 }
];

function log(m) { console.log("[exposure] " + m); }
function fail(m) { throw new Error("exposure: " + m); }
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

    var before = world.postFx().exposure;
    world.postFx({ exposure: EXPOSURE });
    var after = world.postFx().exposure;
    if (Math.abs(after - EXPOSURE) > 1e-6)
        fail(S.name + ": exposure did not take (" + after + ")");
    log(S.name + ": " + nodes + " nodes, exposure " + before + " -> " + after);

    if (project.save() !== true) fail(S.name + ": save failed");

    // ---- the shipped preview ------------------------------------------------
    // Game View: the thumbnail is the SCENE, not the editor — no grid, no gizmo,
    // no selection outline, no light wires. The camera is the one the sample
    // SAVED; nothing here re-frames anything.
    editor.select(null);
    editor.setOverlays({ lightWires: false });
    editor.gameView(true);
    editor.frame(S.warm);
    // "scene" = the editor's own picture, the whole post chain at the exposure
    // the on-screen viewport converged on. "viewport"/true (what the older
    // generators passed) re-seeds an adaptive exposure an offscreen view lives
    // too few frames to converge, so it grades at the seed — which is exactly
    // the thing this run is fixing.
    var shot = editor.screenshot(TREE + "/scenes/preview/" + S.preview,
                                 S.w, S.h, [{ x: 0.5, y: 0.5 }], "scene");
    log(S.name + ": preview " + S.w + "x" + S.h + " centre " + J(shot.center));
    editor.gameView(false);

    // ---- the archive --------------------------------------------------------
    var out = project.exportArchive(zip);
    if (!out || !out.path) fail(S.name + ": exportArchive failed");
    log(S.name + ": wrote " + out.path + " (" + out.assets + " assets, " +
        out.objects + " objects)");

    if (project.close() !== true) fail(S.name + ": close failed");
}

log("exposure " + EXPOSURE + " restored in " + SAMPLES.length + " archives, previews re-shot");
