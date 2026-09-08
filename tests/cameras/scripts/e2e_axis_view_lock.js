// cameras.e2e.axis_lock — THE AXIS-VIEW ROTATION LOCK, through the verbs, in
// the real binary (owner report 2026-09-08: "when in Top/Left/Right/Bottom
// views we should not be able to rotate the camera — only pan and zoom; the
// camera should be locked top-down, bottom-up etc").
//
// The GESTURES are pinned by input.axis_view_lock, which drives the two camera
// controllers directly. What can only be proven HERE, in an app with a real
// viewport, is the wiring around them:
//
//   A. editor.camera().rotationLocked follows editor.setView, for all six axis
//      views and back;
//   B. an axis view is ORTHOGRAPHIC and its grid is turned into the view plane
//      (editor.overlays().gridPlane) — and PANNING inside the view does not
//      move that plane, which is the "the grid stays in the view plane" half of
//      the report;
//   C. returning to "perspective" unlocks AND restores the remembered
//      perspective pose;
//   D. BOTH camera controllers are armed (editor.setCameraMode round trip);
//   E. the two documented exceptions: a PILOTED camera is never locked, and the
//      placement verbs still write any pose they are given;
//   F. F-focus (editor.focusSelection) CENTRES in a locked view instead of
//      turning the camera — the key that would otherwise tilt a top view off
//      its axis while the view still called itself "top";
//   G. gridPlane is read-only.

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
function sameRot(a, b, eps) {
    eps = eps || 1e-4;
    return Math.abs(a.x - b.x) <= eps && Math.abs(a.y - b.y) <= eps &&
           Math.abs(a.z - b.z) <= eps && Math.abs(a.scalar - b.scalar) <= eps;
}
function rotStr(r) {
    return "(" + r.x.toFixed(4) + " " + r.y.toFixed(4) + " " + r.z.toFixed(4) + " " +
           r.scalar.toFixed(4) + ")";
}

var guid = project.create("AxisLock " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- A. the lock follows the view ---------------------------------------
assert(editor.view() === "perspective", "a fresh viewport is in the perspective view");
var persp = editor.camera();
assert(persp.rotationLocked === false, "perspective is NOT rotation-locked");
assert(persp.projection === "perspective", "…and is a perspective projection");
assert(editor.overlays().gridPlane === "floor", "its grid lies in the floor plane");

var planes = { top: "floor", bottom: "floor", front: "frontXY", back: "frontXY",
               left: "sideYZ", right: "sideYZ" };
for (var name in planes) {
    assert(editor.setView(name), "editor.setView('" + name + "')");
    editor.frame(1);
    var c = editor.camera();
    assert(editor.view() === name, name + ": editor.view() reports it");
    assert(c.rotationLocked === true, name + ": the camera is ROTATION-LOCKED");
    assert(c.projection === "orthogonal", name + ": …and orthographic");
    assert(editor.overlays().gridPlane === planes[name],
           name + ": the grid is turned into the view plane (" + planes[name] + ")");
}

// ---- B. panning does not move the view plane (or unlock) -----------------
assert(editor.setView("top"), "back to the top view");
editor.frame(1);
var topBefore = editor.camera();
// The pan a middle-mouse drag makes, as a verb: a move in the view plane (the
// top view's plane is XZ). The pose verb is deliberately allowed to place the
// camera — what is asserted is that the VIEW does not change underneath it.
editor.setCamera({ position: { x: topBefore.position.x + 6,
                               y: topBefore.position.y,
                               z: topBefore.position.z - 4 } });
editor.frame(2);
var topAfter = editor.camera();
assert(sameRot(topAfter.rotation, topBefore.rotation),
       "top: a pan leaves the camera pointing straight down " + rotStr(topAfter.rotation));
assert(topAfter.rotationLocked === true, "top: …still locked after the pan");
assert(editor.view() === "top", "top: …still the top view");
assert(editor.overlays().gridPlane === "floor",
       "top: THE GRID STAYS IN THE VIEW PLANE after the pan");
assert(editor.setView("front"), "front view, then pan there too");
editor.frame(1);
var frontBefore = editor.camera();
editor.setCamera({ position: { x: frontBefore.position.x - 3,
                               y: frontBefore.position.y + 2,
                               z: frontBefore.position.z } });
editor.frame(2);
assert(editor.overlays().gridPlane === "frontXY",
       "front: the grid stays in the FRONT plane after a pan");
assert(editor.camera().rotationLocked === true, "front: still locked");

// ---- C. perspective unlocks and restores the remembered pose -------------
assert(editor.setView("perspective"), "editor.setView('perspective')");
editor.frame(1);
var back = editor.camera();
assert(back.rotationLocked === false, "perspective: the lock is GONE");
assert(back.projection === "perspective", "perspective: …and the projection is back");
near(back.position.x, persp.position.x, 1e-3, "the remembered perspective pose came back (x)");
near(back.position.y, persp.position.y, 1e-3, "…y");
near(back.position.z, persp.position.z, 1e-3, "…z");
assert(sameRot(back.rotation, persp.rotation), "…and its rotation " + rotStr(back.rotation));
assert(editor.overlays().gridPlane === "floor", "…and the grid is the floor grid again");

// ---- D. both camera controllers are armed --------------------------------
assert(editor.setCameraMode("orbit"), "editor.setCameraMode('orbit')");
assert(editor.setView("left"), "the arcball enters the left view");
editor.frame(2);
assert(editor.camera().rotationLocked === true,
       "the ARCBALL is locked in an axis view too (both controllers are armed)");
assert(editor.setCameraMode("free"), "switching camera mode INSIDE the axis view");
assert(editor.camera().rotationLocked === true,
       "…hands the incoming controller a locked camera, not a free one");
assert(editor.setView("perspective") && editor.camera().rotationLocked === false,
       "and perspective unlocks whichever controller is active");

// ---- E. the exceptions ---------------------------------------------------
var cam = scene.addCamera({ position: { x: 4, y: 3, z: 8 }, name: "Axis Shot" });
assert(cam.length > 10, "scene.addCamera -> " + cam);
assert(editor.setView("top"), "into the top view again");
assert(editor.camera().rotationLocked === true, "locked");
assert(editor.pilot(cam), "editor.pilot(camera) from inside a locked view");
editor.frame(1);
assert(editor.camera().rotationLocked === false,
       "A PILOTED CAMERA IS NEVER LOCKED — it is being flown, not measured");
refuses(function () { if (!editor.setView("front")) throw new Error("refused"); },
        "…and editor.setView stays refused while piloting (unchanged)");
assert(editor.pilot(null), "eject");
editor.frame(1);
assert(editor.camera().rotationLocked === true,
       "ejecting back into the axis view re-arms the lock");

// The placement verbs still place: the lock constrains GESTURES only.
var turned = editor.setCamera({ position: { x: 0, y: 20, z: 0 },
                                lookAt: { x: 5, y: 0, z: 5 } });
assert(sameRot(turned.rotation, editor.camera().rotation),
       "editor.setCamera WROTE a rotation in a locked view — the lock constrains " +
       "gestures, not the placement verbs");
assert(editor.camera().rotationLocked === true,
       "…and the view is still the locked top view (the verb is not a gesture)");
assert(editor.setView("top"), "re-picking the view re-snaps it");
editor.frame(1);

// ---- F. F-focus centres, it does not turn --------------------------------
var cube = scene.addPrimitive("cube", { position: { x: 7, y: 0, z: -5 } });
assert(cube.length > 10, "scene.addPrimitive(cube) -> " + cube);
assert(editor.select(cube), "select it");
var beforeFocus = editor.camera();
assert(editor.focusSelection(), "editor.focusSelection() (the F key) in a locked top view");
editor.frame(2);
var afterFocus = editor.camera();
assert(sameRot(afterFocus.rotation, beforeFocus.rotation),
       "F CENTRES WITHOUT TURNING in a locked view " + rotStr(afterFocus.rotation));
near(afterFocus.position.x, 7, 0.5, "…the camera is over the cube (x)");
near(afterFocus.position.z, -5, 0.5, "…and z");
assert(afterFocus.orthoSize < beforeFocus.orthoSize,
       "…and the framing is done by the ORTHO ZOOM, which is what 'closer' means here");
assert(afterFocus.rotationLocked === true, "…still locked afterwards");

// In perspective F still frames the old way (turning is allowed there).
assert(editor.setView("perspective"), "back to perspective");
assert(editor.focusSelection(), "F frames in perspective as before");
editor.frame(1);

// ---- G. gridPlane is read-only -------------------------------------------
refuses(function () { if (!editor.setOverlays({ gridPlane: "floor" })) throw new Error("refused"); },
        "editor.setOverlays refuses gridPlane — it is a consequence of the view, not a setting");

console.log("RESULT: PASS");
