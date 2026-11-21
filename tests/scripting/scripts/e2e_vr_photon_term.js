// vr.photon_term_stereo (PHOTON-7-9A, the final read's M4; CODING_RULES 1: MULTIVIEW IS THE TARGET) — THE PHOTON VIEW'S
// TERM RUNS IN THE STEREO VIEW, IN ONE MULTIVIEW PASS.
//
// The term view ('diffuse' / 'reflections': world.setPhotonView) writes the isolated term BESIDE the radiance, on a second
// colour attachment of the opaque pass, and ONE quad (Jahshaka/PhotonTerm) shows it over the scene result. Until M4 the
// view was mono-only (OgreView: ChainDesc::photonTerm needed !stereo) because the copy in front of that quad
// (Ogre/Copy/4xFP32) has no multiview arm - chain::multiviewMaterial refuses such a quad in a stereo chain, and the chain
// would not build. Now the quad reads both inputs as screen textures (JahScreen.glsl), the stereo chain layers them and
// draws its multiview twin, and the term attachment is layered with the target (the multiview render pass makes every
// attachment's view over the eyes: the fork's OgreVulkanRenderPassDescriptor.cpp, one view range per colour attachment).
// THE LAWS, in a live session against Monado's simulated headset with the term view on:
//   (1) the session draws its eyes - 30 eye frames - with the term view on (a stereo chain that failed to build draws none);
//   (2) the stereo view's own chain runs its ray work every frame - its hit list read back (giStatus().rayQuery.hitViews,
//       the view with two eyes) on 20 of them: the workspace exists and records (a chain whose build threw has none);
//   (3) no reflection recording met a chain at another stereo shape (the tier's defect counter, 0);
//   (4) THE TERM ENGAGED IN THE STEREO VIEW, read from the engine (the final read 2's owed item): the stereo view's hit-list
//       status carries photonTerm (its BUILT chain carries the term attachment and composite) - false before the view is
//       set (the NEGATIVE CONTROL: the field is not constant), true on the eye frames after it.
// The picture is not read here (a script cannot read the headset's eyes; vr.eyeScreenshot draws an eye MONO).
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

project.create("vr photon term " + Date.now());
world.mode({ mode: "epic" });
// a glossy floor and a wall in front of the head: the reflections term has something to show
scene.addPrimitive("Cube", { position: [0, -0.1, -2], scale: [6, 0.2, 6] });
scene.addPrimitive("Cube", { position: [0, 1.5, -4], scale: [6, 3, 0.2] });
editor.frame(10);

assert(vr.available().available === true, "the runtime is there");
assert(vr.begin({ mirror: "left" }) === true, "the session began: " + app.lastError());
// (4)'s NEGATIVE CONTROL: the stereo view's chain before the term view - read back, photonTerm false
function stereoView() {
    var vs = world.giStatus().rayQuery.hitViews || [];
    for (var i = 0; i < vs.length; ++i) if (vs[i].eyes === 2) return vs[i];
    return null;
}
var before = null, prePumps = 0;
while (!before && prePumps < 200) { editor.frame(1); ++prePumps; before = stereoView(); }
assert(before !== null, "the stereo view's list was read back before the term view (" + prePumps + " pumps)");
assert(before.photonTerm === false, "(4) negative control: without the term view the stereo chain carries no term: " + before.photonTerm);
assert(world.setPhotonView("reflections") === true, "the term view is on: " + app.lastError());
assert(world.photonView() === "reflections", "the scene's view reads 'reflections'");

var frames = 0, pumped = 0, readings = 0, maxStereoViews = 0, termReadings = 0;
while (frames < 30 && pumped < 300) {
    var r0 = vr.state().rendered;
    editor.frame(1);
    ++pumped;
    if (vr.state().rendered === r0) continue;   // the runtime asked for no picture this frame
    ++frames;
    var views = world.giStatus().rayQuery.hitViews || [];
    var stereoViews = 0;
    for (var i = 0; i < views.length; ++i)
        if (views[i].eyes === 2) { ++stereoViews; if (views[i].photonTerm === true) ++termReadings; }
    maxStereoViews = Math.max(maxStereoViews, stereoViews);
    if (stereoViews > 0) ++readings;
}
var shapeRefusals = world.giStatus().rayQuery.reflectShapeRefusals;
console.log("the term view in stereo: " + frames + " eye frames over " + pumped + " pumps, the stereo view's list read on " +
            readings + "; shape refusals " + shapeRefusals + "; the editor frames pumped " + (10 + pumped) +
            " + the pre-term pumps " + prePumps + " (the row's FRAMES)");
assert(frames >= 30, "(1) the session drew its eyes with the term view on: " + frames + " frames");
assert(maxStereoViews === 1 && readings >= 20,
       "(2) the stereo view's chain ran its ray work: one stereo view, its list read on " + readings + " eye frames");
assert(shapeRefusals === 0, "(3) no reflection recording met a chain at another stereo shape: " + shapeRefusals);
// (4): the term engaged - the eye frames after the rebuild read the stereo chain carrying it (the first readings may still
// be the pre-term chain's, a read-back late: at least 20 of the 30)
console.log("the stereo view's readings with the term engaged: " + termReadings + " of " + readings);
assert(termReadings >= 20, "(4) the term engaged in the stereo view: photonTerm on " + termReadings + " readings");
assert(world.setPhotonView("off") === true, "the term view off");
assert(vr.end() === true, "the session ended");
