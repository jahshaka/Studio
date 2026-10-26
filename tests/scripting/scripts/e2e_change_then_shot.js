// scripting.e2e.change_then_shot (PHOTON-7 stage 10-3; L1, liveness) - A SCREENSHOT TAKEN AT ONCE AFTER A CAMERA MOVE
// SETTLES TO REST, NEVER TO ITS CAP. The default scene at the self-test's second pose, at rest; the camera 5 cm along x
// (a demand change for the surface cache) and the shot at once, so the shot's own view is the cards' only reader.
// THE DEFECT: the cache's demand window stayed open while a reader's clock (its newest stamp) had not passed its first
// stamp by S frames - and the shot's gather over this near-empty view grazed the cards once, 5 frames after the change,
// and never again. Its clock froze, the window never closed, the rest decision never came, the cards owed their hold and
// giAtRest stayed false: the shot ran to its 2,002-frame cap. The window now closes in the cache's frames as well (its
// stamps have all landed by the read-back's period and lag past first + S).
editor.setCamera({ position: { x: 5, y: 5, z: 14 }, lookAt: { x: -2, y: 0.5, z: -3 } });
editor.frame(240);
var i = 0;
for (; i < 4000 && world.giStatus().giAtRest !== true; ++i) editor.frame(1);
if (world.giStatus().giAtRest !== true) throw new Error("the editor's GI did not come to rest before the move (" + i + " frames)");
var c = world.giStatus().cards;
if (!c.built) throw new Error("no surface cache: the row measures nothing (the default scene's world is Photon with rays)");
console.log("change_then_shot: at rest after " + i + " frames; " + c.cards + " cards resident, " + c.restDecisions +
            " rest decision(s)");
editor.setCamera({ position: { x: 5.05, y: 5, z: 14 }, lookAt: { x: -2, y: 0.5, z: -3 } });
var s = editor.screenshot("change_then_shot.png", 256, 256);
var c2 = world.giStatus().cards;
console.log("change_then_shot: the shot settled " + s.settleFrames + " frames, at rest " + s.atRest +
            (s.atRestReason ? " (" + s.atRestReason + ")" : "") + "; " + c2.restDecisions + " rest decision(s)");
if (s.atRest !== true)
    throw new Error("THE SHOT RAN TO ITS CAP (" + s.settleFrames + " frames, " + s.atRestReason + "): the demand never closed");
"ok";
