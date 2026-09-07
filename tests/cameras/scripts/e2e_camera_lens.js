// cameras.e2e.lens — CAMERA_LENS_SPEC §3 (P1 + P2) through the verbs, in the
// real app binary.
//
// Phase A: the preset TABLES (camera.filmbackPresets / camera.lensPresets).
// Phase B: camera.filmback — presets, sensorFit, the anamorphic squeeze, lens
//          shift, and every refusal.
// Phase C: camera.lens — presets by name, by number, the f-stop, the refusals,
//          and the fact that a preset never reopens the aperture behind you.
// Phase D: the new camera.settings keys, the read-block round trip with them in
//          it, and the reflected-property (keyframeable) half.
// Phase E: camera.focusInfo against hand-computed optics, including the
//          hyperfocal identity and the Infinity answer.
// Phase F: focus TRACKING resolved by the mirror on a real frame.
// Phase G: SAVE -> CLOSE -> OPEN of every new field (the real writer + reader),
//          and the tolerated-absent contract for a file that never had them.
// Phase H: undo.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps, msg) {
    var d = Math.abs(a - b);
    assert(d <= (eps || 1e-3), msg + " (" + a + " vs " + b + ")");
}
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("Lens " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

var cam = scene.addCamera({ position: { x: 0, y: 0, z: 5 } });
assert(cam.length > 10, "scene.addCamera -> " + cam);
// A definite frame shape: a horizontal fit binds through it, so leaving it at
// the default 1.0 would make every angle below a square-frame angle.
camera.settings(cam, { aspectRatio: 16 / 9 });

// ---- phase A: the preset tables -----------------------------------------
var fbs = camera.filmbackPresets();
assert(fbs.length >= 8, "camera.filmbackPresets() lists " + fbs.length + " filmbacks");
var full = null, squeezed = null;
for (var i = 0; i < fbs.length; i++) {
    assert(fbs[i].name && fbs[i].sensorWidth > 0 && fbs[i].sensorHeight > 0,
           "filmback '" + fbs[i].name + "' is " + fbs[i].sensorWidth + " x " +
           fbs[i].sensorHeight + " mm");
    if (fbs[i].sensorWidth === 36 && fbs[i].sensorHeight === 24) full = fbs[i];
    if (fbs[i].anamorphicSqueeze === 2) squeezed = fbs[i];
}
assert(full !== null, "…including the 36x24 full frame the class defaults to");
assert(squeezed !== null, "…and an anamorphic one, whose squeeze is 2");
near(full.sensorAspect, 1.5, 1e-4, "sensorAspect is the SENSOR's shape, not the camera's");

var lenses = camera.lensPresets();
assert(lenses.length >= 7, "camera.lensPresets() lists " + lenses.length + " primes");
var mm50 = null;
for (var j = 0; j < lenses.length; j++) {
    assert(lenses[j].name && lenses[j].focalLength > 0 && lenses[j].minFStop > 0 &&
           lenses[j].note, "lens '" + lenses[j].name + "' — " + lenses[j].note);
    if (lenses[j].focalLength === 50) mm50 = lenses[j];
}
assert(mm50 !== null && mm50.name === "50mm", "…including the 50mm normal");

// ---- phase B: the filmback ----------------------------------------------
var f = camera.filmback(cam);
near(f.sensorWidth, 36, 1e-3, "the camera starts on 36 x 24");
near(f.sensorHeight, 24, 1e-3, "…24 high");
assert(f.sensorFit === "vertical", "…vertically fitted (the historical binding)");
assert(f.fitAxis === "vertical", "…which resolves to the vertical axis");
near(f.anamorphicSqueeze, 1, 1e-6, "…spherical");
near(f.lensShiftX, 0, 1e-6, "…and unshifted");
assert(f.preset === "Full Frame (35mm)", "…and it reports the preset it matches");

f = camera.filmback(cam, "Super 35");
near(f.sensorWidth, 24.89, 1e-3, "camera.filmback(id, 'Super 35') loads the sensor width");
near(f.sensorHeight, 18.66, 1e-3, "…and the height");
assert(f.preset === "Super 35", "…and reports itself as Super 35 afterwards");
near(f.angle, 45, 1e-3, "a degrees-authored camera KEEPS its framing across a filmback change");

// The fit is what makes sensorWidth matter.
f = camera.filmback(cam, { sensorFit: "horizontal" });
assert(f.sensorFit === "horizontal" && f.fitAxis === "horizontal", "sensorFit written");
var l = camera.lens(cam, "50mm");
near(l.focalLength, 50, 1e-3, "camera.lens(id, '50mm') fits a 50mm");
near(l.horizontalFov, 27.9538, 1e-3,
     "…which on Super 35 is 27.9538 degrees across — the sensor WIDTH decided that");
near(l.angle, 15.9399, 1e-3, "…and 15.9399 vertical at 16:9");

// The squeeze widens the same lens.
f = camera.filmback(cam, { anamorphicSqueeze: 2 });
near(f.anamorphicSqueeze, 2, 1e-6, "anamorphicSqueeze written");
near(camera.lens(cam).focalLength, 50, 1e-3, "…it is still a 50mm lens");
near(camera.filmback(cam).horizontalFov, 2 * Math.atan(24.89 / 50) * 180 / Math.PI, 1e-3,
     "…that now sees as wide as a spherical 25mm");
camera.filmback(cam, { anamorphicSqueeze: 1 });

// Lens shift: a fraction of the frame, in [-1, 1].
f = camera.filmback(cam, { lensShiftX: 0.25, lensShiftY: -0.1 });
near(f.lensShiftX, 0.25, 1e-6, "lensShiftX written");
near(f.lensShiftY, -0.1, 1e-6, "lensShiftY written");
camera.filmback(cam, { lensShiftX: 0, lensShiftY: 0 });

// A preset is a FLOOR: explicit rows in the same call win.
f = camera.filmback(cam, { preset: "Full Frame (35mm)", sensorHeight: 20.25 });
near(f.sensorWidth, 36, 1e-3, "a preset plus an explicit row: the preset supplies the width");
near(f.sensorHeight, 20.25, 1e-3, "…and the explicit row wins on the height");

refuses(function () { camera.filmback(cam, "No Such Sensor"); },
        "an unknown filmback preset is refused, and the message lists the real ones");
refuses(function () { camera.filmback(cam, { sensorFit: "diagonal" }); },
        "an unknown sensorFit is refused");
refuses(function () { camera.filmback(cam, { anamorphicSqueeze: 0 }); },
        "a zero anamorphic squeeze is refused (it would divide the sensor away)");
refuses(function () { camera.filmback(cam, { lensShiftX: 3 }); },
        "a lens shift outside [-1, 1] is refused — it is a FRACTION of the frame");
refuses(function () { camera.filmback(cam, { fStop: 4 }); },
        "the aperture is not a filmback row (camera.lens owns it)");

// ---- phase C: the lens ---------------------------------------------------
camera.filmback(cam, "Full Frame (35mm)");
camera.filmback(cam, { sensorFit: "vertical" });
l = camera.lens(cam, 85);
near(l.focalLength, 85, 1e-3, "camera.lens(id, 85) is a bare focal length in millimetres");
near(l.angle, 16.0714, 1e-3, "…16.0714 vertical degrees on full frame");
assert(l.preset === "85mm", "…and it matches the 85mm preset");

var stopBefore = camera.settings(cam).fStop;
l = camera.lens(cam, "24mm");
near(l.fStop, stopBefore, 1e-6,
     "swapping the lens does NOT reopen the aperture behind you");
l = camera.lens(cam, { preset: "24mm", fStop: 1.4 });
near(l.fStop, 1.4, 1e-6, "…but an explicit f-stop in the same call is applied");

refuses(function () { camera.lens(cam, "600mm"); }, "an unknown lens preset is refused");
refuses(function () { camera.lens(cam, { preset: "50mm", focalLength: 35 }); },
        "a preset AND a focal length together are refused (a preset IS one)");
refuses(function () { camera.lens(cam, { sensorWidth: 36 }); },
        "the sensor is not a lens row (camera.filmback owns it)");

// ---- phase D: the new settings keys --------------------------------------
var s = camera.settings(cam, {
    sensorFit: "auto", anamorphicSqueeze: 1.33, lensShiftX: 0.1, lensShiftY: 0.2,
    focusOffset: -0.25, smoothFocus: true, focusSmoothingSpeed: 4.5,
    minFocusDistance: 0.35, bladeCount: 9, focusPlaneVisible: true
});
assert(s.sensorFit === "auto", "camera.settings writes sensorFit");
near(s.anamorphicSqueeze, 1.33, 1e-5, "…anamorphicSqueeze");
near(s.lensShiftX, 0.1, 1e-6, "…lensShiftX");
near(s.lensShiftY, 0.2, 1e-6, "…lensShiftY");
near(s.focusOffset, -0.25, 1e-6, "…focusOffset");
assert(s.smoothFocus === true, "…smoothFocus");
near(s.focusSmoothingSpeed, 4.5, 1e-6, "…focusSmoothingSpeed");
near(s.minFocusDistance, 0.35, 1e-6, "…minFocusDistance");
assert(s.bladeCount === 9, "…bladeCount");
assert(s.focusPlaneVisible === true, "…and focusPlaneVisible");

refuses(function () { camera.settings(cam, { bladeCount: 2 }); },
        "a 2-bladed diaphragm is refused");
refuses(function () { camera.settings(cam, { sensorFit: "sideways" }); },
        "an unknown sensorFit is refused through camera.settings too");

// The reflected-property half: every new row is keyframeable because it is a
// property, and it must be the SAME field, not a parallel copy.
near(node.property(cam, "lensShiftX"), 0.1, 1e-6, "node.property sees lensShiftX");
assert(node.setProperty(cam, "lensShiftX", 0.4), "node.setProperty writes it");
near(camera.settings(cam).lensShiftX, 0.4, 1e-6, "…and camera.settings sees the write");
assert(node.setProperty(cam, "focusOffset", 1.5) &&
       Math.abs(camera.settings(cam).focusOffset - 1.5) < 1e-6,
       "…and the same for focusOffset (a focus pull is keyframes on these rows)");

// The read block still round-trips with the new keys in it.
camera.settings(cam, { sensorFit: "vertical", angle: 30, anamorphicSqueeze: 1 });
var before = camera.settings(cam);
var writeBack = JSON.parse(JSON.stringify(before));
delete writeBack.id; delete writeBack.name; delete writeBack.outputWidth;
delete writeBack.focalLength;
assert(JSON.stringify(camera.settings(cam, writeBack)) === JSON.stringify(before),
       "re-applying the whole read block is STILL a no-op with the lens rows in it");

// ---- phase E: focusInfo --------------------------------------------------
camera.settings(cam, { sensorWidth: 36, sensorHeight: 24, sensorFit: "vertical" });
camera.lens(cam, { preset: "85mm", fStop: 4 });
camera.settings(cam, { focusMode: "manual", focusDistance: 10 });
var fi = camera.focusInfo(cam);
near(fi.focusDistance, 10, 1e-6, "focusInfo echoes the focus distance");
near(fi.cocLimit, 0.0288444, 1e-6, "…full frame's circle of confusion is 0.0288 mm");
near(fi.hyperfocal, 62.705452, 1e-3, "…85mm at f/4 is hyperfocal at 62.7054 m");
near(fi.nearLimit, 8.633082, 1e-3, "…sharp from 8.6331 m");
near(fi.farLimit, 11.881210, 1e-3, "…to 11.8812 m");

// Focus AT the hyperfocal distance: sharp from half of it to infinity.
camera.settings(cam, { focusDistance: fi.hyperfocal });
var hyper = camera.focusInfo(cam);
assert(!isFinite(hyper.farLimit) || hyper.farLimit > 1e5,
       "focusing at the hyperfocal distance is sharp to infinity");
near(hyper.nearLimit, fi.hyperfocal / 2, 0.01,
     "…and its near limit is exactly half the hyperfocal distance");

// Opening up moves both limits in, closing down moves them out — the direction
// is the whole reason an f-stop is worth storing.
camera.settings(cam, { focusDistance: 10, fStop: 1.4 });
var wide = camera.focusInfo(cam);
assert(wide.farLimit - wide.nearLimit < fi.farLimit - fi.nearLimit,
       "opening the aperture SHRINKS the depth of field");
refuses(function () { camera.focusInfo("not-a-node"); },
        "focusInfo refuses an id that names nothing");

// ---- phase F: focus TRACKING --------------------------------------------
// The mirror resolves it on a synced frame, in world space, along the optical
// axis: the camera sits at z = +5 looking down -Z, the subject at the origin,
// so the tracked distance is 5 metres — plus whatever focusOffset says.
var subject = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
camera.lookAt(cam, subject);
camera.settings(cam, {
    focusMode: "track", focusTarget: subject, focusOffset: 0,
    smoothFocus: false, minFocusDistance: 0.1
});
editor.frame(2);
near(camera.settings(cam).focusDistance, 5, 0.05,
     "track mode resolves the focus distance from the target's world position");

camera.settings(cam, { focusOffset: -1 });
editor.frame(2);
near(camera.settings(cam).focusDistance, 4, 0.05, "…plus focusOffset");

// A subject BEHIND the camera has no focus distance; the minimum is the floor.
node.transform(subject, { position: { x: 0, y: 0, z: 40 } });
editor.frame(2);
near(camera.settings(cam).focusDistance, 0.1, 1e-3,
     "a subject behind the camera clamps to minFocusDistance instead of going negative");
node.transform(subject, { position: { x: 0, y: 0, z: 0 } });
editor.frame(2);

// Manual mode is not touched by any of this.
camera.settings(cam, { focusMode: "manual", focusDistance: 7.5, focusOffset: 0 });
editor.frame(2);
near(camera.settings(cam).focusDistance, 7.5, 1e-4,
     "manual focus is never rewritten by the tracker");

// ---- phase G: save -> close -> open -------------------------------------
camera.settings(cam, {
    sensorWidth: 24.89, sensorHeight: 18.66, sensorFit: "horizontal",
    anamorphicSqueeze: 2, lensShiftX: -0.15, lensShiftY: 0.35,
    focusOffset: 0.75, smoothFocus: true, focusSmoothingSpeed: 3.25,
    minFocusDistance: 0.6, bladeCount: 11, focusPlaneVisible: true
});
var saved = camera.settings(cam);
assert(project.save(), "project.save()");
assert(project.close(), "project.close()");
assert(project.open(guid), "project.open() — the real reader");

var rows = scene.cameras();
assert(rows.length >= 1, "the cameras came back");
var re = null;
for (var k = 0; k < rows.length; k++) if (rows[k].id === cam) re = rows[k];
assert(re !== null, "…including the one this phase configured");
near(re.sensorWidth, 24.89, 1e-3, "sensorWidth survived the round trip");
near(re.sensorHeight, 18.66, 1e-3, "…sensorHeight");
assert(re.sensorFit === "horizontal", "…sensorFit");
near(re.anamorphicSqueeze, 2, 1e-5, "…anamorphicSqueeze");
near(re.lensShiftX, -0.15, 1e-5, "…lensShiftX");
near(re.lensShiftY, 0.35, 1e-5, "…lensShiftY");
near(re.focusOffset, 0.75, 1e-5, "…focusOffset");
assert(re.smoothFocus === true, "…smoothFocus");
near(re.focusSmoothingSpeed, 3.25, 1e-5, "…focusSmoothingSpeed");
near(re.minFocusDistance, 0.6, 1e-5, "…minFocusDistance");
assert(re.bladeCount === 11, "…bladeCount");
assert(re.focusPlaneVisible === true, "…focusPlaneVisible");
near(re.angle, saved.angle, 1e-3, "…and the angle of view itself");
near(re.focalLength, saved.focalLength, 1e-3,
     "…so the derived focal length comes back identical too");

// TOLERATED-ABSENT: a camera whose file never carried any of these keys must
// load as the pre-phase defaults. A camera added AFTER the reopen has them at
// their defaults for the same reason (the constructor is the one source), which
// is what the reader falls back to key by key.
var fresh = scene.addCamera({ position: { x: 3, y: 0, z: 0 } });
var fs = camera.settings(fresh);
assert(fs.sensorFit === "vertical" && fs.anamorphicSqueeze === 1 &&
       fs.lensShiftX === 0 && fs.lensShiftY === 0 && fs.focusOffset === 0 &&
       fs.smoothFocus === false && fs.bladeCount === 5 && fs.focusPlaneVisible === false,
       "a camera with none of these keys is vertical, spherical, unshifted and unblurred " +
       "— exactly what every file written before this phase means");
near(fs.angle, 45, 1e-3, "…and its projection is the historical 45 degrees");

// ---- phase H: undo -------------------------------------------------------
// A script run is ONE open macro, so editor.undo() cannot reach inside it —
// `pushes` (the total number of commands ever pushed) is the only honest answer
// to "did that record an undo step?" from in here. Every row of a filmback or
// lens write is one command, exactly as camera.settings' rows are.
var before = editor.undoState().pushes;
var undoTarget = camera.filmback(fresh, "Super 35");
near(undoTarget.sensorWidth, 24.89, 1e-3, "a filmback preset write");
var pushed = editor.undoState().pushes - before;
assert(pushed >= 2, "…pushed " + pushed + " undoable commands (width, height, squeeze)");

before = editor.undoState().pushes;
camera.lens(fresh, { preset: "85mm", fStop: 2 });
assert(editor.undoState().pushes - before >= 2,
       "…and a lens write is undoable too (the lens and the stop)");

before = editor.undoState().pushes;
camera.focusInfo(fresh);
camera.filmbackPresets();
camera.lensPresets();
camera.filmback(fresh);
assert(editor.undoState().pushes === before,
       "…while the READ verbs push nothing at all");

console.log("\nALL CAMERA LENS E2E CHECKS PASSED");
