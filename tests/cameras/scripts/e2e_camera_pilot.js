// cameras.e2e.pilot — CAMERAS_SPEC phase 3 (D4/D8) and the phase-2c preview
// preference, through the verbs, in the real app.
//
// Phase A: editor.pip / editor.setPip — the preference, its clamping, its
//          refusal of unknown keys, and the difference between "the preference
//          is on" and "an inset is on screen right now".
// Phase B: editor.pilot / editor.piloting — entering, the pick-ray sweep seen
//          from the API (editor.camera() vs the piloted camera's transform),
//          ejecting, and the flown pose being KEPT.
// Phase C: editor.setViewCamera — the dropdown's vocabulary.
// Phase D: the refusals: a node that is not a camera, a guid that is not in the
//          scene, and editor.setView while piloting.
// Phase E: the flight is ONE undo step, and undoing it puts the camera back.
//
// Runs inside the real binary (--script, scratch HOME).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps, msg) {
    var d = Math.abs(a - b);
    assert(d <= (eps || 1e-3), msg + " (" + a + " vs " + b + ")");
}
function isNone(v) { return v === null || v === undefined; }
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("Pilot " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- phase A: the preview preference ------------------------------------
var pip = editor.pip();
assert(typeof pip.enabled === "boolean", "editor.pip().enabled is a boolean");
assert(pip.size > 0 && pip.size <= 0.6, "editor.pip().size is a fraction of the viewport");
assert(isNone(pip.camera), "nothing is selected, so nothing is being previewed");

var set = editor.setPip({ enabled: true, size: 0.9 });
assert(set.enabled === true, "editor.setPip turned the preview on");
near(set.size, 0.6, 1e-6, "an over-large size is CLAMPED rather than accepted");
set = editor.setPip({ size: 0.001 });
near(set.size, 0.08, 1e-6, "…and an under-small one too");
set = editor.setPip({ size: 0.3 });
near(set.size, 0.3, 1e-6, "a sane size is kept verbatim");
refuses(function () { editor.setPip({ enabled: true, wobble: 3 }); },
        "editor.setPip refuses an unknown key rather than ignoring it");

// A camera, and selecting it is what puts the inset on screen.
var camA = scene.addCamera({ position: { x: 3, y: 2, z: 6 }, name: "Shot A" });
assert(camA.length > 10, "scene.addCamera -> " + camA);
assert(editor.select(camA), "editor.select(camera)");
editor.frame(2);
assert(editor.pip().camera === camA,
       "selecting a scene camera is what raises the preview — the PREFERENCE alone does not");
assert(editor.setPip({ enabled: false }).enabled === false, "the preference turns it off");
assert(isNone(editor.pip().camera), "…and then nothing is previewed even with the camera selected");
editor.setPip({ enabled: true });
assert(editor.pip().camera === camA, "…and back on again");

// ---- phase B: piloting ---------------------------------------------------
assert(isNone(editor.piloting()), "the viewport starts as the free explorer");
var explorerBefore = editor.camera();

assert(editor.pilot(camA), "editor.pilot(camera)");
assert(editor.piloting() === camA, "editor.piloting() reports it");
editor.frame(2);
assert(isNone(editor.pip().camera),
       "the preview of the camera you are LOOKING THROUGH is hidden — an inset of your own " +
       "view is a hall of mirrors");

// The viewport now IS the camera: placing the view places the camera node.
editor.setCamera({ position: { x: 10, y: 4, z: -2 }, lookAt: { x: 0, y: 0, z: 0 } });
editor.frame(1);
var camInfo = node.info(camA);
near(camInfo.position.x, 10, 1e-2, "flying the view moved the CAMERA NODE (x)");
near(camInfo.position.y, 4, 1e-2, "…y");
near(camInfo.position.z, -2, 1e-2, "…z");

// ---- phase D: the refusals (while piloting) ------------------------------
refuses(function () { editor.setView("top"); },
        "canonical views are the EXPLORER's and are refused while piloting");

// ---- phase B (cont): ejecting keeps the flown pose ------------------------
assert(editor.pilot(null), "editor.pilot(null) ejects");
assert(isNone(editor.piloting()), "…and the viewport is the explorer again");
var after = node.info(camA);
near(after.position.x, 10, 1e-2, "the camera KEEPS its flown pose — piloting doubles as placement");
var explorerAfter = editor.camera();
near(explorerAfter.position.x, explorerBefore.position.x, 1e-2,
     "…and the explorer is exactly where it was left");

// ---- phase C: the dropdown's vocabulary ----------------------------------
assert(editor.setViewCamera(camA), "editor.setViewCamera(id) pilots");
assert(editor.piloting() === camA, "…same mechanism as editor.pilot");
assert(editor.setViewCamera("viewport"), "editor.setViewCamera('viewport') ejects");
assert(isNone(editor.piloting()), "…back to the explorer");

// ---- phase D: the other refusals -----------------------------------------
var cube = scene.addPrimitive("cube");
refuses(function () { editor.pilot(cube); },
        "editor.pilot refuses a node that is not a camera");
refuses(function () { editor.pilot("not-a-guid"); },
        "editor.pilot refuses a guid that names nothing in the scene");
assert(isNone(editor.piloting()), "a refused pilot leaves the viewport alone");

// ---- phase E: the flight is ONE undo step --------------------------------
// COUNTED, not executed: a script run is itself one open undo macro, so
// editor.undo() cannot reach anything the run just did (editor.undoState()
// reports macroOpen for exactly this reason). What is provable — and what the
// spec actually claims — is that a whole flight pushes exactly ONE command,
// and that a flight that moved nothing pushes none.
assert(editor.undoState().macroOpen,
       "a script run is one open undo macro, so undo() cannot reach the run's own steps");

var camB = scene.addCamera({ position: { x: 0, y: 0, z: 5 }, name: "Shot B" });
var before = editor.undoState().pushes;
assert(editor.pilot(camB), "pilot the second camera");
editor.setCamera({ position: { x: -7, y: 1, z: 1 } });
editor.frame(1);
near(node.info(camB).position.x, -7, 1e-2, "flown");
assert(editor.undoState().pushes === before,
       "MID-FLIGHT nothing has been pushed — a command per mouse event would bury the stack");
assert(editor.pilot(null), "eject");
assert(editor.undoState().pushes === before + 1,
       "ejecting pushed exactly ONE command for the whole flight");

// …and a flight that went nowhere is not a step at all.
var quiet = editor.undoState().pushes;
assert(editor.pilot(camB), "pilot again");
assert(editor.pilot(null), "eject without moving");
assert(editor.undoState().pushes === quiet,
       "a camera that never moved does not put a no-op on the undo stack");

// ---- phase F: ADOPTION MUST NOT WRITE THE CAMERA -------------------------
// The regression gate for the defect the sockets lane found and this lane fixed
// (2026-09-06): both camera controllers' setCamera() used to end in
// updateCameraRot(), writing `Quat::fromEulerAngles(pitch, yaw, 0)` onto the
// node — so merely HANDING a camera to the viewport zeroed its roll, on the
// document, permanently. Piloting is the plainest way to hand one over.
//
// What is asserted is the distinction, not just the number: adoption (pilot,
// idle frames, a camera-mode switch, eject) leaves the pose alone; NAVIGATION
// is still free to level the roll — that is what navigating means.
var rolled = scene.addCamera({ position: { x: 5, y: 3, z: 5 }, name: "Rolled" });
assert(node.transform(rolled, { rotation: { x: -15, y: 30, z: 40 } }),
       "a camera with authored ROLL (z = 40)");
var rollBefore = node.info(rolled).rotation;
near(rollBefore.z, 40, 1e-2, "…the document really holds the roll");

assert(editor.pilot(rolled), "pilot it");
var rollAfter = node.info(rolled).rotation;
near(rollAfter.x, rollBefore.x, 1e-2, "PILOTING ALONE does not rewrite the camera (x)");
near(rollAfter.y, rollBefore.y, 1e-2, "…y");
near(rollAfter.z, rollBefore.z, 1e-2, "…and the ROLL survives adoption");

editor.frame(3);
rollAfter = node.info(rolled).rotation;
near(rollAfter.z, rollBefore.z, 1e-2,
     "…and survives frames of the controller running with nobody navigating");

// The arcball is the other controller, and it used to rewrite the node from
// (pitch, yaw, 0) + pivot on EVERY frame it was active.
assert(editor.setCameraMode("orbit"), "switch to the arcball while piloting it");
editor.frame(3);
rollAfter = node.info(rolled).rotation;
near(rollAfter.z, rollBefore.z, 1e-2, "…the arcball does not level it either");
assert(editor.setCameraMode("free"), "back to the fly camera");

assert(editor.pilot(null), "eject");
rollAfter = node.info(rolled).rotation;
near(rollAfter.z, rollBefore.z, 1e-2, "…and ejecting hands it back unrewritten");
assert(node.remove(rolled), "the probe camera goes away again");

// Leave the preferences as we found them: in a QT_DEBUG build jahsettings.ini
// lives beside the BINARY, so every suite in this build directory shares it.
editor.setPip({ enabled: true, size: 0.28 });

console.log("cameras.e2e.pilot: all checks passed");
