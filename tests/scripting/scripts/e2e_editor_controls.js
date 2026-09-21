// scripting.e2e.editor_controls — EDITOR_SHORTCUTS_SPEC §5/§6: the editor
// verbs behind the Unreal-style controls, driven before/alongside their UI.
// Runs inside the real app (--script, engine viewport up, scratch HOME).
//
// Phase A: gizmoMode/setGizmoMode, focusSelection (camera provably moves).
// Phase B: gameView round-trip.
// Phase C: snapSize/setSnapSize, snapToFloor.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) < (eps || 1e-3); }

var guid = project.create("Editor Controls " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- gizmo mode verbs (phase A) ----
assert(editor.gizmoMode() === "translate", "gizmo mode defaults to translate");
assert(editor.setGizmoMode("rotate"), "setGizmoMode(rotate)");
assert(editor.gizmoMode() === "rotate", "gizmoMode reads back rotate");
assert(editor.setGizmoMode("scale"), "setGizmoMode(scale)");
assert(editor.gizmoMode() === "scale", "gizmoMode reads back scale");
var badModeRefused = false;
try { editor.setGizmoMode("bend"); } catch (e) { badModeRefused = true; }
assert(badModeRefused, "setGizmoMode refuses an unknown mode");
assert(editor.setGizmoMode("translate"), "back to translate");

// ---- focusSelection (phase A): the camera provably moves ----
// A cube high above the default scene: the initial camera (0,5,14 looking at
// the origin) cannot see it, so the screenshot centre changes when F frames it.
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 60, z: 0 } });
assert(cube.length > 10, "cube added at y=60");

var noSelRefused = false;
editor.select(null);
try { editor.focusSelection(); } catch (e) { noSelRefused = true; }
assert(noSelRefused, "focusSelection refuses with nothing selected");

editor.frame(2);
var before = editor.screenshot("controls_before.png", 256, 256);
assert(editor.select(cube), "cube selected");
assert(editor.focusSelection(), "focusSelection");
editor.frame(2);
var after = editor.screenshot("controls_after.png", 256, 256);
var moved = !near(before.center.r, after.center.r, 2) ||
            !near(before.center.g, after.center.g, 2) ||
            !near(before.center.b, after.center.b, 2);
assert(moved, "screenshot centre changed after focus (" +
    before.center.r + "," + before.center.g + "," + before.center.b + " -> " +
    after.center.r + "," + after.center.g + "," + after.center.b + ")");

// ---- gameView round-trip (phase B) ----
assert(editor.isGameView() === false, "game view off by default");
assert(editor.gameView(true), "gameView(true)");
assert(editor.isGameView() === true, "isGameView reads back true");
editor.frame(2);   // a frame renders fine with every helper hidden
assert(editor.gameView(false), "gameView(false)");
assert(editor.isGameView() === false, "isGameView reads back false");

// ---- snap sizes (phase C): editor-global, persisted, drive the gizmos ----
// ALL THREE now (2026-09-06 verb-coverage audit F10): the rotate and scale
// gizmos snap to SnapSettings exactly as the translate one does, and only the
// [ / ] keys could ever change them. The bare number stays the translate
// alias — the spelling this verb shipped with, and the one that also moves
// the ground grid.
var snaps = editor.snapSize();
assert(near(snaps.translate, 1.0, 1e-4), "translate snap defaults to 1.0");
assert(near(snaps.rotate, 10.0, 1e-4), "rotate snap defaults to 10 degrees");
assert(near(snaps.scale, 0.25, 1e-4), "scale snap defaults to 0.25");

var afterAlias = editor.setSnapSize(0.5);
assert(near(afterAlias.translate, 0.5, 1e-4), "a bare number is the TRANSLATE alias");
assert(near(afterAlias.rotate, 10.0, 1e-4) && near(afterAlias.scale, 0.25, 1e-4),
       "…and leaves the other two alone");
assert(near(editor.snapSize().translate, 0.5, 1e-4), "translate snap reads back 0.5");

var afterObject = editor.setSnapSize({ rotate: 15, scale: 0.5 });
assert(near(afterObject.rotate, 15.0, 1e-4) && near(afterObject.scale, 0.5, 1e-4),
       "the object form writes rotate and scale: " + JSON.stringify(afterObject));
assert(near(afterObject.translate, 0.5, 1e-4), "…and the omitted key kept its value");

// Clamping is per-size, and the RETURNED object is the truth, not an echo.
assert(near(editor.setSnapSize({ rotate: 999 }).rotate, 180.0, 1e-4),
       "rotate clamps at 180 degrees");
assert(near(editor.setSnapSize({ translate: 1000 }).translate, 100.0, 1e-4),
       "translate clamps at 100");
assert(near(editor.setSnapSize({ scale: 0.0001 }).scale, 0.01, 1e-4), "scale clamps at 0.01");

var badSnapRefused = false;
try { editor.setSnapSize(0); } catch (e) { badSnapRefused = true; }
assert(badSnapRefused, "setSnapSize(0) refused");
var badRotateRefused = false;
try { editor.setSnapSize({ rotate: -1 }); } catch (e) { badRotateRefused = true; }
assert(badRotateRefused, "a negative size in the object form is refused");
var badSnapKeyRefused = false;
try { editor.setSnapSize({ rotation: 5 }); } catch (e) { badSnapKeyRefused = true; }
assert(badSnapKeyRefused, "an unknown snap key is refused (not silently ignored)");
var emptySnapRefused = false;
try { editor.setSnapSize({}); } catch (e) { emptySnapRefused = true; }
assert(emptySnapRefused, "an empty snap change is refused");
assert(near(editor.snapSize().translate, 100.0, 1e-4),
       "refused sets left every value alone");

var restored = editor.setSnapSize({ translate: 1.0, rotate: 10, scale: 0.25 });
assert(near(restored.translate, 1.0, 1e-4) && near(restored.rotate, 10.0, 1e-4) &&
       near(restored.scale, 0.25, 1e-4), "all three snap sizes restored (they persist)");

// ---- snapToFloor (phase C): the framed cube at y=60 lands on the ground ----
editor.select(cube);
var before2 = node.info(cube).position.y;
assert(near(before2, 60, 1e-2), "cube still at y=60");
assert(editor.snapToFloor(), "snapToFloor");
var after2 = node.info(cube).position.y;
assert(after2 < 3 && after2 > -0.01,
    "cube dropped from y=60 to rest on the floor (y=" + after2 + ")");
assert(editor.snapToFloor(), "second snapToFloor is a no-op");
assert(near(node.info(cube).position.y, after2, 1e-3), "already-floored cube stays put");

editor.select(null);
try { editor.snapToFloor(); throw new Error("snapToFloor without selection must throw"); }
catch (e) { assert(String(e).indexOf("selected") >= 0, "snapToFloor refuses with nothing selected"); }

// ---- canonical views (editor.setView / editor.view — Views dropdown) ----
// After the focus/snapToFloor phases the camera hangs high up staring at
// where the cube USED to be (empty sky) — a flat frame. setView(bottom)
// would turn it to MORE empty sky (same flat grey), so the pixel proof uses
// "top": looking straight down guarantees the ground plane fills the frame.
assert(editor.view() === "perspective", "view defaults to perspective");
editor.frame(2);
var perspShot = editor.screenshot("view_persp.png", 256, 256);
assert(editor.setView("bottom"), "setView(bottom)");
assert(editor.view() === "bottom", "view reads back bottom");
editor.frame(2);
assert(editor.setView("top"), "setView(top)");
assert(editor.view() === "top", "view reads back top");
editor.frame(30);   // the arcball controller animates to the target
var topShot = editor.screenshot("view_top.png", 256, 256);
var viewChanged = !near(perspShot.center.r, topShot.center.r, 2) ||
                  !near(perspShot.center.g, topShot.center.g, 2) ||
                  !near(perspShot.center.b, topShot.center.b, 2);
assert(viewChanged, "camera turned: sky-facing perspective frame vs the ground plane from the top view (persp " +
    perspShot.center.r + "," + perspShot.center.g + "," + perspShot.center.b +
    " -> top " + topShot.center.r + "," + topShot.center.g + "," + topShot.center.b + ")");
var badViewRefused = false;
try { editor.setView("diagonal"); } catch (e) { badViewRefused = true; }
assert(badViewRefused, "setView refuses an unknown view");
assert(editor.view() === "top", "refused setView left the view alone");
assert(editor.setView("perspective"), "back to perspective");
assert(editor.view() === "perspective", "view reads back perspective");
editor.frame(2);

// A canonical axis view must LOOK AT THE SCENE, not just turn in place.
// EditorCameraController::setAxisView rotated the free camera and left its
// POSITION alone, so every view was taken from wherever the explorer happened
// to be: "left"/"right" sat inside the x=0 plane their grid lies in (the grid
// exactly edge-on, the scene 14 units off to the side) and "back" had the
// whole scene behind its near plane. Only "front" worked, by luck, because the
// default pose is already on +Z (owner report 2026-09-07). The camera lands ON
// the axis of the view now, looking at the origin, with the standoff it had.
var axisFraming = [
    { view: "top",    axis: "y", sign:  1 },
    { view: "bottom", axis: "y", sign: -1 },
    { view: "left",   axis: "x", sign:  1 },
    { view: "right",  axis: "x", sign: -1 },
    { view: "front",  axis: "z", sign:  1 },
    { view: "back",   axis: "z", sign: -1 }
];
for (var ai = 0; ai < axisFraming.length; ++ai) {
    var af = axisFraming[ai];
    assert(editor.setView(af.view), "axis framing: setView(" + af.view + ")");
    editor.frame(2);
    var ap = editor.camera().position;
    var offAxis = (af.axis === "x") ? [ap.y, ap.z]
                : (af.axis === "y") ? [ap.x, ap.z]
                                    : [ap.x, ap.y];
    assert(near(offAxis[0], 0, 1e-2) && near(offAxis[1], 0, 1e-2),
        af.view + ": the camera is ON the " + af.axis + " axis (" +
        ap.x + "," + ap.y + "," + ap.z + ")");
    assert(af.sign * ap[af.axis] > 1,
        af.view + ": it stands off along " + (af.sign > 0 ? "+" : "-") + af.axis);
}
assert(editor.setView("perspective"), "axis framing: back to perspective");
editor.frame(2);

// ---- per-view camera memory (owner defect: Top -> Perspective reset the
// camera). Each view remembers its camera for the viewport session:
// perspective its full pose, each ortho view its own pan + zoom. Verified
// pixel-free through the editor.camera() pose verb.
function vecNear(a, b, eps) {
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}
function quatNear(a, b, eps) {  // q and -q are the same rotation
    var d = Math.abs(a.x * b.x + a.y * b.y + a.z * b.z + a.scalar * b.scalar);
    return d > 1 - (eps || 1e-4);
}

// Give the perspective view a distinctive, content-rich pose first (the
// earlier phases left it staring at empty sky): frame the floored cube.
editor.select(cube);
assert(editor.focusSelection(), "frame the cube in perspective");
editor.frame(2);
var persp0 = editor.camera();
assert(persp0.projection === "perspective", "camera() reports perspective");
editor.screenshot("memory_persp_before.png", 256, 256);

assert(editor.setView("top"), "into the top view");
editor.frame(5);
// Mutate the top view's camera so its memory is distinguishable from the
// default axis snap: focusing the floored cube pans the camera over it.
editor.select(cube);
assert(editor.focusSelection(), "focus inside the top view (pans the camera)");
editor.frame(2);
var top0 = editor.camera();
assert(top0.projection === "orthogonal", "top view is orthographic");
editor.screenshot("memory_top.png", 256, 256);

assert(editor.setView("perspective"), "top -> perspective");
editor.frame(2);
editor.screenshot("memory_persp_after.png", 256, 256);
var persp1 = editor.camera();
assert(persp1.projection === "perspective", "perspective projection restored");
assert(vecNear(persp1.position, persp0.position, 1e-3),
    "perspective position restored (" + persp0.position.x + "," + persp0.position.y + "," +
    persp0.position.z + " == " + persp1.position.x + "," + persp1.position.y + "," + persp1.position.z + ")");
assert(quatNear(persp1.rotation, persp0.rotation), "perspective orientation restored");

// Each ortho view has its OWN memory: detour through front (first visit =
// default framing), then top must return to the focused pose, not re-snap.
assert(editor.setView("front"), "into the front view");
editor.frame(5);
assert(editor.setView("top"), "front -> top");
editor.frame(2);
var top1 = editor.camera();
assert(top1.projection === "orthogonal", "top is orthographic again");
assert(vecNear(top1.position, top0.position, 1e-3), "top view pan (position) restored");
assert(near(top1.orthoSize, top0.orthoSize, 1e-3), "top view zoom (orthoSize) restored");

assert(editor.setView("perspective"), "back to perspective once more");
editor.frame(2);
var persp2 = editor.camera();
assert(vecNear(persp2.position, persp0.position, 1e-3) && quatNear(persp2.rotation, persp0.rotation),
    "perspective pose survives repeated trips through ortho views");

// ---- the memory works under the arcball controller too ----
assert(editor.cameraMode() === "free", "camera mode defaults to free");
assert(editor.setCameraMode("orbit"), "switch to the arcball controller");
assert(editor.cameraMode() === "orbit", "cameraMode reads back orbit");
editor.frame(2);   // let the orbit controller's recomposition settle
var orbP0 = editor.camera();
assert(editor.setView("top"), "orbit: into top");
editor.frame(40);  // the arcball lerps to the axis view
assert(editor.setView("perspective"), "orbit: back to perspective");
editor.frame(2);
var orbP1 = editor.camera();
assert(vecNear(orbP1.position, orbP0.position, 1e-2) &&
       quatNear(orbP1.rotation, orbP0.rotation, 1e-3),
    "orbit mode: perspective pose restored (" + orbP0.position.x + "," + orbP0.position.y + "," +
    orbP0.position.z + " == " + orbP1.position.x + "," + orbP1.position.y + "," + orbP1.position.z + ")");
var badCamModeRefused = false;
try { editor.setCameraMode("chase"); } catch (e) { badCamModeRefused = true; }
assert(badCamModeRefused, "setCameraMode refuses an unknown mode");
assert(editor.setCameraMode("free"), "back to the free camera");
assert(editor.cameraMode() === "free", "cameraMode reads back free");

console.log("editor_controls: verbs verified");

// ---- editor.setCamera / editor.frameNode -------------------------------
// AI_SURFACE_PROGRAM_SPEC lane B #3. The verbs that let an agent point the
// camera at something instead of guessing at a view name.
//
// THE assertion in here is the controller RESYNC one: both existing camera
// movers end by handing the moved camera back to the active controller, and
// the arcball controller REBUILDS the camera pose from its own pivot/yaw/pitch
// on EVERY frame (OrbitalCameraController::update -> updateCameraRot). A
// setCamera that skipped the resync therefore looks like it worked and is
// silently undone by the next frame — which reads as "the camera verb is
// broken" and gets blamed on the model.

/// The camera's forward direction from the quaternion editor.camera() reports:
/// (0,0,-1) rotated by q (v + 2*qv x (qv x v + w*v)).
function forwardOf(q) {
    var x = q.x, y = q.y, z = q.z, w = q.scalar;
    var vx = 0, vy = 0, vz = -1;
    var cx = y * vz - z * vy, cy = z * vx - x * vz, cz = x * vy - y * vx;
    cx += w * vx; cy += w * vy; cz += w * vz;
    var dx = y * cz - z * cy, dy = z * cx - x * cz, dz = x * cy - y * cx;
    return { x: vx + 2 * dx, y: vy + 2 * dy, z: vz + 2 * dz };
}
function normalized(v) {
    var l = Math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return { x: v.x / l, y: v.y / l, z: v.z / l };
}
function refused(fn, what) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, what);
}

assert(editor.cameraMode() === "free", "camera verbs: starting on the free camera");

// position alone moves without turning
var beforePose = editor.camera();
var moved1 = editor.setCamera({ position: { x: 3, y: 4, z: 5 } });
assert(vecNear(moved1.position, { x: 3, y: 4, z: 5 }, 1e-3), "setCamera({position}) moved the camera");
assert(quatNear(moved1.rotation, beforePose.rotation), "setCamera({position}) alone did not turn it");
assert(vecNear(editor.camera().position, moved1.position, 1e-4),
    "the returned pose is what editor.camera() reports");

// lookAt aims it
var aimed = editor.setCamera({ position: { x: 0, y: 3, z: 12 }, lookAt: { x: 0, y: 0, z: 0 } });
var wantDir = normalized({ x: 0 - 0, y: 0 - 3, z: 0 - 12 });
var haveDir = forwardOf(aimed.rotation);
assert(vecNear(haveDir, wantDir, 1e-3),
    "setCamera({lookAt}) points the camera at the target (" +
    haveDir.x + "," + haveDir.y + "," + haveDir.z + ")");

// rotation round-trips through the quaternion editor.camera() hands out
var savedPose = editor.camera();
editor.setCamera({ position: { x: -20, y: 9, z: -20 }, lookAt: { x: 5, y: 0, z: 5 } });
assert(!quatNear(editor.camera().rotation, savedPose.rotation), "the camera really turned away");
var restored = editor.setCamera({ position: savedPose.position, rotation: savedPose.rotation });
assert(vecNear(restored.position, savedPose.position, 1e-3) &&
       quatNear(restored.rotation, savedPose.rotation),
    "setCamera({rotation}) round-trips editor.camera()'s quaternion");
// …and Euler degrees are accepted too (what node.info() reports)
var eulered = editor.setCamera({ rotation: { x: -30, y: 45, z: 0 } });
assert(quatNear(eulered.rotation, editor.camera().rotation), "setCamera({rotation}) accepts Euler degrees");
editor.setCamera({ position: savedPose.position, rotation: savedPose.rotation });

// fov
var fovPose = editor.setCamera({ fov: 60 });
assert(near(fovPose.fov, 60, 1e-3), "setCamera({fov}) writes the field of view");
assert(vecNear(editor.camera().position, savedPose.position, 1e-3), "…without moving the camera");
assert(near(editor.setCamera({ fov: 45 }).fov, 45, 1e-3), "fov restored to 45");

// refusals — a pose verb that silently ignores half of what it was asked is
// the F7/F8 defect class this program exists to stop repeating.
refused(function () { editor.setCamera({ postion: { x: 1, y: 1, z: 1 } }); },
    "setCamera refuses a misspelled key instead of ignoring it");
refused(function () { editor.setCamera({ lookAt: { x: 0, y: 0, z: 0 }, rotation: { x: 0, y: 0, z: 0 } }); },
    "setCamera refuses lookAt AND rotation together");
refused(function () { editor.setCamera({ position: { x: 1, y: 2, z: 3 }, lookAt: { x: 1, y: 2, z: 3 } }); },
    "setCamera refuses a lookAt at the camera's own position");
refused(function () { editor.setCamera({ fov: 0 }); }, "setCamera refuses fov 0");
refused(function () { editor.setCamera({ fov: 400 }); }, "setCamera refuses fov 400");
refused(function () { editor.setCamera("here"); }, "setCamera refuses a non-object");

// ---- the resync: the pose survives the next frames, in BOTH modes ------
// `at`/`look` differ per mode ON PURPOSE: switching the controller syncs it to
// whatever pose the camera is in, so re-placing the camera where it already is
// would pass with no resync at all. The pose must be NEW after the controller
// has settled on the old one — that is the only shape of this test that fails
// when the resync is removed (verified by deleting it).
function poseSurvivesFrames(mode, at, look) {
    assert(editor.setCameraMode(mode), "resync check: " + mode + " camera");
    editor.frame(2);   // the controller now owns the CURRENT pose
    var placed = editor.setCamera({ position: at, lookAt: look });
    editor.frame(5);
    var after = editor.camera();
    assert(vecNear(after.position, placed.position, 1e-2),
        mode + ": setCamera survived 5 frames (" + placed.position.x + "," + placed.position.y + "," +
        placed.position.z + " -> " + after.position.x + "," + after.position.y + "," + after.position.z + ")");
    assert(quatNear(after.rotation, placed.rotation, 1e-3), mode + ": and the orientation held");
}
poseSurvivesFrames("free", { x: 6, y: 5, z: 14 }, { x: 0, y: 1, z: 0 });
poseSurvivesFrames("orbit", { x: -9, y: 7, z: -11 }, { x: 2, y: 0, z: 3 });
assert(editor.setCameraMode("free"), "back to the free camera");

// ---- frameNode --------------------------------------------------------
var target = scene.addPrimitive("sphere", { position: { x: 12, y: 0, z: -7 } });
assert(target.length > 10, "a sphere to frame at (12,0,-7)");

// look at it from straight above (top view) and from the front: two different
// poses of the same subject, both centred on it.
var fromTop = editor.frameNode(target, { yaw: 0, pitch: -80, distance: 9 });
assert(vecNear(fromTop.target, { x: 12, y: 0, z: -7 }, 0.6),
    "frameNode reports the bounds centre it framed (" + fromTop.target.x + "," +
    fromTop.target.y + "," + fromTop.target.z + ")");
assert(near(fromTop.distance, 9, 1e-2), "frameNode honoured distance 9");
assert(fromTop.position.y > 8, "pitch -80 put the camera above the sphere (y=" + fromTop.position.y + ")");
var toTarget = normalized({ x: fromTop.target.x - fromTop.position.x,
                            y: fromTop.target.y - fromTop.position.y,
                            z: fromTop.target.z - fromTop.position.z });
assert(vecNear(forwardOf(fromTop.rotation), toTarget, 1e-3), "…and it looks straight at it");

var fromFront = editor.frameNode(target, { yaw: 0, pitch: 0, distance: 9 });
assert(near(fromFront.position.z, -7 + 9, 1e-2) && near(fromFront.position.y, 0, 1e-2),
    "yaw 0 / pitch 0 is the front view: +Z of the subject (" + fromFront.position.x + "," +
    fromFront.position.y + "," + fromFront.position.z + ")");

// pitch is CLAMPED: the poles are where the controllers' yaw/pitch
// decomposition degenerates and where the camera flips under its subject.
var clamped = editor.frameNode(target, { yaw: 30, pitch: -200, distance: 9 });
var atLimit = editor.frameNode(target, { yaw: 30, pitch: -89, distance: 9 });
assert(vecNear(clamped.position, atLimit.position, 1e-3),
    "frameNode clamps pitch -200 to -89 (" + clamped.position.y + " == " + atLimit.position.y + ")");
var clampedUp = editor.frameNode(target, { yaw: 30, pitch: 400, distance: 9 });
var atUpper = editor.frameNode(target, { yaw: 30, pitch: 89, distance: 9 });
assert(vecNear(clampedUp.position, atUpper.position, 1e-3), "and pitch 400 to 89");

// no options = keep the direction, use the bounds-derived distance (the F key)
editor.setCamera({ position: { x: 12, y: 0, z: 3 }, lookAt: { x: 12, y: 0, z: -7 } });
var kept = editor.frameNode(target);
assert(kept.distance > 0.5 && kept.distance < 20,
    "frameNode() with no options derives a framing distance (" + kept.distance + ")");
assert(near(kept.position.x, 12, 0.5) && near(kept.position.y, 0, 0.5) && kept.position.z > -7,
    "…from the direction the camera already looked (" + kept.position.x + "," +
    kept.position.y + "," + kept.position.z + ")");

refused(function () { editor.frameNode("not-a-guid"); }, "frameNode refuses an unknown id");
refused(function () { editor.frameNode(target, { yawe: 10 }); }, "frameNode refuses a misspelled key");
refused(function () { editor.frameNode(target, { yaw: "left" }); }, "frameNode refuses a non-numeric yaw");

// frameNode's pose survives the frames too, under the arcball
assert(editor.setCameraMode("orbit"), "orbit for the frameNode resync check");
var framed = editor.frameNode(target, { yaw: 25, pitch: -35, distance: 8 });
editor.frame(5);
var framedAfter = editor.camera();
assert(vecNear(framedAfter.position, framed.position, 1e-2) &&
       quatNear(framedAfter.rotation, framed.rotation, 1e-3),
    "orbit: frameNode survived 5 frames — the arcball adopted the pivot we framed");
assert(editor.setCameraMode("free"), "back to the free camera again");

// ---- pixels: framing the sphere is not the same picture as looking away ----
editor.setCamera({ position: { x: -40, y: 30, z: 40 }, lookAt: { x: -60, y: 40, z: 60 } });
editor.frame(3);
var away = editor.screenshot("camera_away.png", 256, 256);
editor.frameNode(target, { yaw: 0, pitch: -20, distance: 6 });
editor.frame(3);
var onTarget = editor.screenshot("camera_framed.png", 256, 256);
var pixelsMoved = !near(away.center.r, onTarget.center.r, 2) ||
                  !near(away.center.g, onTarget.center.g, 2) ||
                  !near(away.center.b, onTarget.center.b, 2);
assert(pixelsMoved, "frameNode changed what the viewport renders (" +
    away.center.r + "," + away.center.g + "," + away.center.b + " -> " +
    onTarget.center.r + "," + onTarget.center.g + "," + onTarget.center.b + ")");

console.log("editor_controls: camera placement verbs verified");
// ---- overlays (AI_SURFACE_PROGRAM_SPEC lane D #15) ----
// The View Options rows as a verb pair. FIVE keys now: the fifth, `stats`, was
// refused by name for as long as setShowFps was an empty override and nothing
// drew a counter — an `fps` key would have been a silent no-op, which is the
// defect class that program existed to stop. STATS_OVERLAY_SPEC discharged the
// refusal: the engine draws the readout, so the key exists, and it is called
// `stats` because it reports frame time, draws and triangles rather than a
// frame rate. The read-back object is the assertion.
var ov0 = editor.overlays();
assert(typeof ov0.grid === "boolean", "overlays().grid is a boolean");
assert(typeof ov0.lightWires === "boolean", "overlays().lightWires is a boolean");
assert(typeof ov0.selectionWireframe === "boolean", "overlays().selectionWireframe is a boolean");
assert(typeof ov0.stats === "boolean", "overlays().stats is a boolean");
assert(typeof ov0.gameView === "boolean", "overlays().gameView is a boolean");
assert(ov0.fps === undefined, "the key is 'stats', not 'fps'");
// Grid default flipped OFF 2026-09-06: scenes ship a tiled floor, so the
// perspective grid is opt-in (the canonical axis views force their own,
// independent of this flag — that is render behavior, not this preference).
assert(ov0.grid === false && ov0.lightWires === true,
    "grid defaults OFF (tiled floors), light wires default ON");
assert(ov0.selectionWireframe === false, "selection highlight defaults to the outline");

assert(editor.setOverlays({ grid: false, lightWires: false, selectionWireframe: true }), "setOverlays");
var ov1 = editor.overlays();
assert(ov1.grid === false && ov1.lightWires === false && ov1.selectionWireframe === true,
    "all three read back changed: " + JSON.stringify(ov1));
editor.frame(2);   // a frame renders with the helpers off

// Omitted keys keep their value.
assert(editor.setOverlays({ grid: true }), "setOverlays({grid:true}) alone");
var ov2 = editor.overlays();
assert(ov2.grid === true, "grid came back on");
assert(ov2.lightWires === false && ov2.selectionWireframe === true, "the other two did not move");

// gameView is in the same object and is the same switch editor.gameView flips.
assert(editor.setOverlays({ gameView: true }), "setOverlays({gameView:true})");
assert(editor.overlays().gameView === true && editor.isGameView() === true,
    "overlays().gameView and isGameView() agree");
assert(editor.overlays().grid === true,
    "grid still reads ON while Game View hides it (the object is honest, not a render report)");
assert(editor.gameView(false), "back out of game view");
assert(editor.overlays().gameView === false, "and overlays() agrees");

// ---- the stats readout (STATS_OVERLAY_SPEC phase 4) ----
// Off by default, switchable through the verb, visible in the read-back, and
// NOT hidden by Game View — that last one is a deliberate exception to the
// gameView master switch (D3): a frame-time readout is a diagnostic, and "what
// is my frame time in the game view" is the question people ask.
// `stats` is backed by the persisted `show_fps` preference, and in QT_DEBUG
// the settings file lives at applicationDirPath — SHARED with interactive
// runs, so a developer who left the readout on would fail a "default"
// assertion here (2026-09-05 gate). Reset explicitly and assert the reset;
// the true default-value contract belongs to the per-test data-root override
// (WINDOWS_BUILD_SPEC), not to this suite.
assert(editor.setOverlays({ stats: false }), "reset the persisted stats preference");
assert(editor.overlays().stats === false, "the stats readout is off after the reset");
assert(editor.setOverlays({ stats: true }), "setOverlays({stats:true})");
assert(editor.overlays().stats === true, "stats reads back on");
editor.frame(4);   // the readout is composed and drawn on real frames
assert(editor.gameView(true), "into game view with the readout on");
assert(editor.overlays().stats === true,
    "the stats readout SURVIVES Game View (it is a diagnostic, not a helper)");
assert(editor.gameView(false), "back out of game view");
assert(editor.setOverlays({ stats: false }), "setOverlays({stats:false})");
assert(editor.overlays().stats === false, "stats reads back off");
editor.frame(2);

// Refusals: an unknown key and a non-boolean value both fail loudly.
var fpsRefused = false;
try { editor.setOverlays({ fps: true }); } catch (e) {
    fpsRefused = ("" + e).indexOf("'stats', not 'fps'") >= 0;
}
assert(fpsRefused, "setOverlays refuses 'fps' and names the key that DOES exist");
var typoRefused = false;
try { editor.setOverlays({ lightwires: false }); } catch (e) { typoRefused = true; }
assert(typoRefused, "a mis-cased key is refused rather than ignored");
var typeRefused = false;
try { editor.setOverlays({ grid: "yes" }); } catch (e) { typeRefused = true; }
assert(typeRefused, "a non-boolean value is refused");
var emptyRefused = false;
try { editor.setOverlays({}); } catch (e) { emptyRefused = true; }
assert(emptyRefused, "an empty change is refused");
assert(editor.overlays().grid === true, "and none of the refusals changed anything");

// ---- physicsDebug (2026-09-06 verb-coverage audit F11) ----
// The SIXTH overlay: View -> Wireframes -> Physics Debug Overlay had a menu
// item and an IEditorViewport seam and no verb, so the one overlay a script
// most wants while debugging a simulation was the one it could not reach.
assert(typeof ov0.physicsDebug === "boolean", "overlays().physicsDebug is a boolean");
assert(editor.overlays().physicsDebug === false, "the physics drawer is off by default");
assert(editor.setOverlays({ physicsDebug: true }), "setOverlays({physicsDebug:true})");
assert(editor.overlays().physicsDebug === true, "physicsDebug reads back on");
editor.frame(2);   // a frame renders with the debug drawer armed
assert(editor.setOverlays({ physicsDebug: false }), "setOverlays({physicsDebug:false})");
assert(editor.overlays().physicsDebug === false, "physicsDebug reads back off");

// The empty-map refusal lists EVERY key, including the two that were missing
// from it (stats and physicsDebug) — a caller who reads that message must not
// have to guess.
var emptyMsg = "";
try { editor.setOverlays({}); } catch (e) { emptyMsg = "" + e; }
assert(emptyMsg.indexOf("stats") >= 0 && emptyMsg.indexOf("physicsDebug") >= 0 &&
       emptyMsg.indexOf("grid") >= 0 && emptyMsg.indexOf("gameView") >= 0,
       "the empty-map message lists all six keys: " + emptyMsg);

// Restore the defaults for anything running after this script.
assert(editor.setOverlays({ grid: true, lightWires: true, selectionWireframe: false }),
    "overlays restored");

console.log("editor_controls: overlays verified");

// ---- gizmo transform space (F12) ----
// The toolbar's globe/cube pair had no verb; the buttons also LIED at startup
// (they hard-checked Global while every Gizmo is constructed in Local space
// and nothing reconciled the two — the toolbar now starts on what the gizmos
// actually are).
var space0 = editor.gizmoSpace();
assert(space0 === "local" || space0 === "global", "gizmoSpace() is one of the two: " + space0);
assert(editor.setGizmoSpace("global"), "setGizmoSpace(global)");
assert(editor.gizmoSpace() === "global", "gizmoSpace reads back global");
assert(editor.setGizmoSpace("local"), "setGizmoSpace(local)");
assert(editor.gizmoSpace() === "local", "gizmoSpace reads back local");
var badSpace = false;
try { editor.setGizmoSpace("screen"); } catch (e) { badSpace = true; }
assert(badSpace, "setGizmoSpace refuses an unknown space");
assert(editor.gizmoSpace() === "local", "the refused switch changed nothing");
assert(editor.setGizmoSpace("global"), "back to global");

// ---- immersive fullscreen (F12's other half) ----
// F11 the KEY had no verb. Called with no argument it READS; with a boolean it
// sets and returns the state that resulted, so the round trip is one verb.
assert(editor.fullscreen() === false, "the window is not in immersive fullscreen");
assert(editor.fullscreen(true) === true, "editor.fullscreen(true) enters and reports it");
editor.frame(2);   // the viewport keeps rendering with the docks hidden
assert(editor.fullscreen() === true, "the read agrees");
assert(editor.fullscreen(true) === true, "setting the state it is already in is a no-op");
assert(editor.fullscreen(false) === false, "leaving reports the new state");
assert(editor.fullscreen() === false, "…and the read agrees");
editor.frame(2);
var badFullscreen = false;
try { editor.fullscreen("yes"); } catch (e) { badFullscreen = true; }
assert(badFullscreen, "a non-boolean is refused (it is not a read either)");
assert(editor.fullscreen() === false, "the refusal changed nothing");

// ---- ...AND THE STATE IS NOT OURS ALONE (RR2, lane ENGINE-7 item 5) ----
// Immersive fullscreen is a window state PLUS a set of hidden docks, and
// anything can take the window out of the state without telling the editor:
// app.resizeWindow() calls showNormal() before it resizes, and a window
// manager offers its own control. The flag used to keep saying "fullscreen"
// afterwards — and editor.fullscreen(true), which is idempotent against that
// flag, then did NOTHING, so F11 was dead until it was pressed twice.
assert(editor.fullscreen(true) === true, "fullscreen again, to be left by somebody else");
editor.frame(2);
assert(app.window().fullScreen === true, "the WINDOW agrees it is fullscreen");
var resized = app.resizeWindow(1100, 700);
editor.frame(2);
assert(resized.fullScreen === false, "app.resizeWindow left fullscreen at the window level");
assert(editor.fullscreen() === false,
       "...and the editor's own state followed it (it used to read true forever)");
assert(editor.fullscreen(true) === true,
       "...so entering again WORKS instead of being a no-op against a stale flag");
editor.frame(2);
assert(app.window().fullScreen === true, "the window really went fullscreen the second time");
assert(editor.fullscreen(false) === false, "and back out");
editor.frame(2);

console.log("editor_controls: gizmo space + fullscreen verified");

// ---- faceCullingMode travels as a NAME (F18) ----
// The one enum a node reflects. It used to come back as the document's ordinal
// and take one back, on a surface whose rule is "enums travel as names".
assert(editor.select(cube), "select the cube for the property read");
var cull0 = node.property(cube, "faceCullingMode");
assert(cull0 === "none" || cull0 === "back" || cull0 === "front" || cull0 === "material",
       "faceCullingMode reads as a NAME, never an ordinal: " + cull0);
assert(node.setProperty(cube, "faceCullingMode", "back") === true, "set it by name");
assert(node.property(cube, "faceCullingMode") === "back", "…and it reads back as the name");
var cullRow = null;
var rows = node.properties(cube);
for (var ri = 0; ri < rows.length; ri++) if (rows[ri].name === "faceCullingMode") cullRow = rows[ri];
assert(cullRow !== null, "node.properties carries the row");
assert(cullRow.value === "back", "…with the name as its value");
assert(cullRow.type === "list", "…typed as a list");
assert(cullRow.options.length === 4 && cullRow.options.indexOf("front") >= 0 &&
       cullRow.options.indexOf("material") >= 0,
       "…and OPTIONS listing every accepted name: " + JSON.stringify(cullRow.options));
var ordinalRefused = false;
try { node.setProperty(cube, "faceCullingMode", 2); } catch (e) {
    ordinalRefused = ("" + e).indexOf("NAME") >= 0;
}
assert(ordinalRefused, "an ORDINAL is refused, and the message says the enum travels as a name");
var cullNameRefused = false;
try { node.setProperty(cube, "faceCullingMode", "sideways"); } catch (e) { cullNameRefused = true; }
assert(cullNameRefused, "an unknown name is refused");
assert(node.property(cube, "faceCullingMode") === "back", "neither refusal changed the value");
assert(node.setProperty(cube, "faceCullingMode", cull0) === true, "restored");

console.log("editor_controls: faceCullingMode names verified");

// ---- gameplay input (AVATAR_LOCOMOTION_SPEC §8.2/§10, Stage 1) ----
// The API-first half: every verb the input layer ships, driven in the REAL app
// before any UI reads it. The behaviour of the layer itself is the input.actions
// suite's; what this proves is that the verbs are registered, reach the same
// singleton the keyboard producer writes, and refuse what they must.
//
// CAUTION, and the reason this section ends the way it does: input.bind
// PERSISTS, and in a QT_DEBUG build jahsettings.ini lives at applicationDirPath
// — SHARED with interactive runs. Everything below is restored explicitly.
// Defensive FIRST, not only last: this suite runs against a jahsettings.ini it
// shares with interactive runs, and a previous run that died mid-section would
// otherwise leave rebound rows that make every "defaults" assertion below lie.
assert(input.resetBindings(), "start from the shipped bindings");
var actions = input.bindings();
assert(actions.length === 4, "input.bindings lists exactly four actions");
var byName = {};
for (var i = 0; i < actions.length; ++i) byName[actions[i].action] = actions[i];
assert(byName.Move && byName.Look && byName.Jump && byName.Sprint,
    "the four actions are Move, Look, Jump, Sprint");
assert(byName.Move.type === "axis2d" && byName.Jump.type === "button",
    "Move is an axis, Jump is a button");
assert(byName.Jump.latched === true && byName.Sprint.latched === false,
    "Jump is the latched action, Sprint is held");
assert(byName.Look.mouse === true && byName.Look.keys.length === 0,
    "Look is the mouse, with no keys by default");
assert(byName.Move.keys.join(",") === "W,S,A,D,Up,Down,Left,Right",
    "Move defaults to W/S/A/D AND the arrows — the player takes both spellings");
assert(byName.Jump.keys.join(",") === "Space", "Jump defaults to Space (the owner's ask)");
assert(byName.Sprint.keys.join(",") === "Shift", "Sprint defaults to Shift");
assert(byName.Move.display === "W / S / A / D / Up / Down / Left / Right",
    "the Preferences display text is generated");

var st = input.state();
assert(st.move.x === 0 && st.move.y === 0 && st.jump === false && st.sprint === false,
    "input.state starts at rest");

// avatar.input — the scripted producer, and the whole reason locomotion is
// testable headless.
var driven = avatar.input({ move: { x: 0, y: 1 }, sprint: true });
assert(driven.move.y === 1 && driven.sprint === true, "avatar.input writes move + sprint");
assert(input.state().move.y === 1, "…and input.state sees the same singleton");
var clamped = avatar.input({ move: { x: 3, y: 4 } });
assert(near(clamped.move.x, 0.6) && near(clamped.move.y, 0.8),
    "a script's move is clamped to the unit disc (3,4 -> 0.6,0.8)");
assert(avatar.input({ jump: true }).jump === true, "avatar.input latches a jump");
assert(input.state().jump === true, "the latch is pending until a consumer takes it");
assert(avatar.input({ jump: false }).jump === true,
    "jump:false is a NO-OP — a script cannot swallow a pending jump");
avatar.input({ move: { x: 0, y: 0 }, sprint: false });
assert(input.state().move.y === 0 && input.state().sprint === false, "the state zeroes again");

// Rebinding: conflict-reported, atomic, persisted.
var conflict = "";
try { input.bind("Sprint", "W"); } catch (e) { conflict = "" + e; }
assert(conflict.indexOf("Move") >= 0 && conflict.indexOf("W") >= 0,
    "input.bind names the action AND the key it collides with");
assert(input.bindings()[3].keys.join(",") === "Shift", "the refused bind changed nothing");
var badAction = false;
try { input.bind("Crouch", "C"); } catch (e) { badAction = ("" + e).indexOf("closed") >= 0; }
assert(badAction, "an action outside the closed set is refused, and the message says so");
var badKey = false;
try { input.bind("Jump", "Nonsense"); } catch (e) { badKey = true; }
assert(badKey, "a key name that names no key is refused");
var axisAsString = false;
try { input.bind("Move", "W"); } catch (e) { axisAsString = true; }
assert(axisAsString, "an axis action refuses a single-key binding and says what it wants");

assert(input.bind("Jump", "Return"), "Jump rebound to Return");
assert(input.bindings()[2].keys.join(",") === "Return", "the rebind reads back");
assert(input.bind("Move", { up: "Up", down: "Down", left: "Left", right: "Right" }),
    "Move rebound to the arrow keys");
assert(input.bindings()[0].keys.join(",") === "Up,Down,Left,Right", "the axis rebind reads back");
assert(input.bindings()[0].display === "Up / Down / Left / Right",
    "the Preferences row follows the rebind");

// Restore — and the reset must leave NO override rows behind, so the shared
// settings file this ran against is byte-for-byte what it was.
assert(input.resetBindings(), "input.resetBindings()");
assert(input.bindings()[0].keys.join(",") === "W,S,A,D,Up,Down,Left,Right",
       "Move is back to the shipped eight");
assert(input.bindings()[2].keys.join(",") === "Space", "Jump is back to Space");

console.log("editor_controls: gameplay input verbs verified");

// ---------------------------------------------------------------------------
// THE CAMERA SPEED (owner R15, lane FLYSPEED-1) — ONE integer 1..32 for every
// way a person moves through a scene, verb-first: the toolbar's speed button,
// the popover's slider and number field, and the scroll-wheel gesture in the
// viewport are four more callers of exactly this, so the verb IS the feature.
var cs0 = editor.cameraSpeed();
assert(cs0.n === 10, "a fresh install sits at 10, the dial's middle: " + cs0.n);
assert(near(cs0.factor, 1, 1e-6), "and 10 is a factor of exactly 1 — today's speed");
assert(near(cs0.editorSpeed, 8, 1e-4), "the editor's fly is 8 u/s at 10: " + cs0.editorSpeed);
assert(near(cs0.playerSpeed, 25, 1e-4), "the Player's free camera is 25 u/s: " + cs0.playerSpeed);
assert(near(cs0.vrSpeed, world.vr().flySpeed, 1e-4),
    "and a VR wearer flies the PROJECT's own m/s at 10: " + cs0.vrSpeed);

var cs20 = editor.cameraSpeed(20);
assert(cs20.n === 20 && near(cs20.factor, 2, 1e-6), "cameraSpeed(20) -> a factor of 2");
assert(near(cs20.editorSpeed, 16, 1e-4) && near(cs20.playerSpeed, 50, 1e-4),
    "every surface doubles: " + cs20.editorSpeed + " u/s, " + cs20.playerSpeed + " u/s");
assert(near(cs20.vrSpeed, world.vr().flySpeed * 2, 1e-4),
    "...the headset included: " + cs20.vrSpeed + " m/s");
assert(editor.cameraSpeed().n === 20, "and it reads back");

// The wheel's gesture, as a verb: one step in each direction.
assert(editor.cameraSpeed("faster").n === 21, "\"faster\" steps the dial by one");
assert(editor.cameraSpeed("slower").n === 20, "\"slower\" steps it back");

// Clamped at the ends rather than refused — 1 and 32 are the dial.
assert(editor.cameraSpeed(1000).n === 32, "an absurd number clamps at 32");
assert(editor.cameraSpeed(0).n === 1, "and zero at 1 — there is no 'off'");
// ...AT ANY MAGNITUDE. 1e10 does not fit an int, and narrowing it before
// clamping is undefined behaviour that landed on INT_MIN here — so a caller
// asking for "as fast as possible" got the SLOWEST setting (fix round item 3).
assert(editor.cameraSpeed(1e10).n === 32, "1e10 clamps UP to 32, not down to 1");
assert(editor.cameraSpeed(-1e10).n === 1, "and -1e10 clamps to 1");
for (var i = 0; i < 5; i++) editor.cameraSpeed("slower");
assert(editor.cameraSpeed().n === 1, "stepping below the bottom holds at 1");

// NO DECIMALS. The dial is an integer and says so, rather than silently
// rounding a caller who thought otherwise (owner R15: "so we don't hold
// decimals in the UI").
var fractional = false;
try { editor.cameraSpeed(12.5); } catch (e) { fractional = ("" + e).indexOf("whole number") >= 0; }
assert(fractional, "12.5 is refused, and the message says the dial is an integer");
var badWord = false;
try { editor.cameraSpeed("quick"); } catch (e) { badWord = true; }
assert(badWord, "a word that is neither faster nor slower is refused");
// TWO WORDS, THE TWO THE DOCUMENTATION NAMES: "up"/"down" rode along
// undocumented from the retired setFlySpeed and went with it (the CRUD law).
var upWord = false;
try { editor.cameraSpeed("up"); } catch (e) { upWord = true; }
assert(upWord, "\"up\" is not a spelling of \"faster\" — the undocumented alias is gone");
var downWord = false;
try { editor.cameraSpeed("down"); } catch (e) { downWord = true; }
assert(downWord, "...nor \"down\" of \"slower\"");
var boolSpeed = false;
try { editor.cameraSpeed(true); } catch (e) { boolSpeed = true; }
assert(boolSpeed, "and a true/false is not a camera speed (it would arrive as 1)");
assert(editor.cameraSpeed().n === 1, "none of the refusals moved the dial");

// THE RETIRED VERBS ARE GONE (the CRUD law): two multipliers on two surfaces,
// a ladder of rungs, and a Player that could fly at a different speed from the
// editor that opened it.
assert(typeof editor.flySpeed === "undefined" && typeof editor.setFlySpeed === "undefined",
    "editor.flySpeed/setFlySpeed are deleted, not aliased");
assert(typeof player.flySpeed === "undefined" && typeof player.setFlySpeed === "undefined",
    "and so are player.flySpeed/setFlySpeed");

editor.cameraSpeed(10);   // leave no persisted state behind
assert(editor.cameraSpeed().n === 10, "back to 10");

// ---------------------------------------------------------------------------
// POST PROCESS PARAMETERS (fix wave item 8). world.postFx is generated from the
// same table the World > Post Process section is built from, so the two cannot
// disagree about a range. Everything here is in STOPS since EXPOSURE-1, and
// `exposureEv` is the key that says so.
var pf0 = world.postFx();
assert(typeof pf0.exposureEv === "number", "postFx().exposureEv");
assert(typeof pf0.exposureMin === "number" && typeof pf0.exposureMax === "number",
    "postFx() reports the automatic window as well as the exposure");
assert(pf0.exposureMin <= pf0.exposureMax, "the window is ordered");
// NULL under Manual: there is no measurement to report, and 0 is a real
// reading now (0 stops = "the meter landed where Manual would").
assert(pf0.exposureMeasured === null || pf0.exposureMeasured === undefined,
    "postFx().exposureMeasured is null while the exposure is a number, not a measurement");

var pf1 = world.postFx({ exposureMin: -1.0, exposureMax: 1.5 });
assert(near(pf1.exposureMin, -1.0) && near(pf1.exposureMax, 1.5), "the window round-trips");
// Written in the WRONG order: the verb re-orders rather than storing an
// inverted window that would clamp everything to nothing.
var pf2 = world.postFx({ exposureMin: 3.0 });
assert(pf2.exposureMin <= pf2.exposureMax,
    "an inverted window is re-ordered, never stored: " + pf2.exposureMin + ".." + pf2.exposureMax);
// THE PIN IS A MODE NOW, not a pair of equal numbers (EXPOSURE-1): the old
// "min == max" recipe is deleted, and the window is only read under Auto.
var badExposure = false;
try { world.postFx({ exposure: 1.0 }); } catch (e) { badExposure = true; }
assert(badExposure, "the old chain-unit 'exposure' key is refused by name");
var badParam = false;
try { world.postFx({ nonsense: 1 }); } catch (e) { badParam = true; }
assert(badParam, "an unknown post-fx parameter is refused by name");
world.postFx({ exposureMin: -3.5, exposureMax: 3.5 });   // back to the defaults

// ---------------------------------------------------------------------------
// SCREENSHOT GRADES (fix wave item 6). The default MUST stay raw — this verb is
// the tree's measuring instrument — and "tonemap" must be a different picture.
var shotRaw = editor.screenshot("uiux_raw.png", 64, 64);
var shotTone = editor.screenshot("uiux_tone.png", 64, 64, [], "tonemap");
assert(shotRaw.width === 64 && shotTone.width === 64, "both grades render");
var shotRaw2 = editor.screenshot("uiux_raw2.png", 64, 64);
assert(shotRaw2.center.r === shotRaw.center.r && shotRaw2.center.g === shotRaw.center.g &&
       shotRaw2.center.b === shotRaw.center.b,
    "the DEFAULT is still the exactly-reproducible raw readback");
assert(shotTone.center.r !== shotRaw.center.r || shotTone.center.g !== shotRaw.center.g ||
       shotTone.center.b !== shotRaw.center.b,
    "\"tonemap\" develops a different picture: raw " +
    [shotRaw.center.r, shotRaw.center.g, shotRaw.center.b].join(",") + " vs tonemapped " +
    [shotTone.center.r, shotTone.center.g, shotTone.center.b].join(","));
var badGrade = false;
try { editor.screenshot("uiux_bad.png", 64, 64, [], "sepia"); } catch (e) { badGrade = true; }
assert(badGrade, "an unknown grade is refused by name");

// ---------------------------------------------------------------------------
// NEW-SCENE GRID DEFAULT (fix wave item 4). A brand-new scene must come up at
// the DEFAULTS, not inherit the helper state of whatever was open before it.
// The loaded-scene case is asserted above (ov0.grid === false); this is the
// half that was wrong: EditorData::showGrid constructed TRUE while the loader
// default and the viewport default were both false.
assert(editor.setOverlays({ grid: true }), "turn the grid ON in this scene");
assert(editor.overlays().grid === true, "the grid is on");
var fresh = project.create("Grid Default " + Date.now());
assert(fresh.length > 10, "a brand-new project (and therefore a brand-new scene)");
assert(editor.overlays().grid === false,
    "THE NEW SCENE STARTS AT THE DEFAULT: the grid is off again, not inherited");
assert(editor.overlays().lightWires === true, "and light wires are back at their default (on)");

// ---------------------------------------------------------------------------
// S2: A DROP LANDS WHERE THE MOUSE IS (owner smoke, 2026-09-11 — "drag-and-drop
// of a primitive lands at world centre; it should land where the mouse is").
//
// The viewport's drop and this verb are ONE function (editor.dropPointAt =
// EngineSceneViewport::dropPositionAt): the surface under the cursor, else the
// y=0 ground plane. The drop then hands that point to
// SceneEditService::addPrimitive, which is exactly what
// scene.addPrimitive(name, {position}) does here — so this drives the whole
// placement path a dragged cube takes, minus the QDropEvent itself.
var vp = editor.viewportState();
assert(vp.width > 0 && vp.height > 0, "the viewport has a size (" + vp.width + "x" + vp.height + ")");

var left = editor.dropPointAt(vp.width * 0.30, vp.height * 0.70);
var right = editor.dropPointAt(vp.width * 0.70, vp.height * 0.70);
assert(left !== null && right !== null, "editor.dropPointAt answers with a world point");
assert(near(left.y, 0, 0.01) && near(right.y, 0, 0.01),
    "the drop point is ON THE GROUND (y=0 plane / the surface under the cursor)");
assert(Math.abs(left.x - right.x) > 1.0,
    "two different pixels give two different points (" + left.x.toFixed(2) + " vs "
    + right.x.toFixed(2) + ") — the cursor is what decides");

var dropped = scene.addPrimitive("cube", { position: left });
var dp = node.transform(dropped).position;
assert(near(dp.x, left.x, 1e-2) && near(dp.y, left.y, 1e-2) && near(dp.z, left.z, 1e-2),
    "the primitive is BORN at the drop point (" + [dp.x, dp.y, dp.z].join(",") + ")");
assert(node.mobility(dropped).graphStatic === true, "...and placing it is not a move (still static)");

var dropped2 = scene.addPrimitive("cube", { position: right });
var dp2 = node.transform(dropped2).position;
assert(Math.abs(dp2.x - dp.x) > 1.0,
    "a second drop at another pixel lands somewhere else (no stacking at the origin)");

// ---------------------------------------------------------------------------
// A DROP APPLIES TO WHAT IS UNDER IT — INCLUDING THE FLOOR (owner report via
// the rig, 2026-09-14: a MATERIAL dragged onto the default Ground was silently
// discarded, and a TEXTURE spawned a floating image plane instead of
// retexturing it).
//
// The cause was the target resolution, not the apply: the default Ground is
// setPickable(false) BY DESIGN (clicking the floor selects nothing), and the
// material/texture drops resolved their target with an ordinary pick, so the
// floor simply was not there. editor.dropTargetAt is that resolution — the
// same function the two drop branches call — and it forces pickability the way
// the object drops always have.
// The S2 section above left two cubes standing on the ground points this one
// aims at; they have made their point, and what is under the cursor is this
// section's whole subject, so they go first.
node.remove(dropped);
node.remove(dropped2);
editor.frame(2);

var target = editor.dropTargetAt(vp.width * 0.30, vp.height * 0.70);
assert(target != null, "editor.dropTargetAt over the ground answers with a node");
assert(node.property(target.id, "defaultFloor") === true,
    "...and it is the DEFAULT FLOOR — a locked node is under the cursor like any other ("
    + target.name + ")");

// THE LOCK IS THE ANSWER (owner, 2026-09-15: "we can select the floor like any
// other asset, it is just LOCKED by default … you can't drop a material on it
// while it is locked"). One flag says so — `pickable`, which is exactly what
// the hierarchy row's lock icon toggles — and the drop REFUSES a locked node
// by name (a toast) instead of doing nothing, and never turns a refused image
// into a floating plane.
assert(target.locked === true,
    "the default floor is LOCKED, and the verb says so rather than pretending there is no "
    + "target");
assert(node.property(target.id, "pickable") === false,
    "...which is the node's own `pickable` flag — the hierarchy's lock, not a second concept");

node.setProperty(target.id, "pickable", true);          // the lock icon, as a verb
editor.frame(2);
var unlocked = editor.dropTargetAt(vp.width * 0.30, vp.height * 0.70);
assert(unlocked != null && unlocked.id === target.id && unlocked.locked === false,
    "unlocked, the same floor is an ordinary drop target (" + JSON.stringify(unlocked) + ")");
node.setProperty(target.id, "pickable", false);         // put the floor back as it ships
editor.frame(2);
assert(editor.dropTargetAt(vp.width * 0.30, vp.height * 0.70).locked === true,
    "re-locked, it refuses again");
// …and a drop that hits NOTHING still has no target — the case that spawns an
// image plane for a dropped picture. Aimed deliberately: the camera is put on
// the horizon and asked about a pixel above it, because "the top of the
// viewport" is only sky for a camera that happens to be level.
var wasPose = editor.camera();
editor.setCamera({ position: { x: 0, y: 2, z: 12 }, lookAt: { x: 0, y: 2, z: 0 } });
editor.frame(2);
// `== null`, not `=== null`: a verb that answers "nothing" answers with an
// invalid QVariant, which reaches a script as `undefined`.
var sky = editor.dropTargetAt(vp.width * 0.5, 4);
assert(sky == null,
    "a drop against the sky has no target (that is the case that spawns an image plane), got "
    + JSON.stringify(sky));
editor.setCamera({ position: wasPose.position, rotation: wasPose.rotation });
editor.frame(2);

// AN OBJECT IN FRONT OF THE FLOOR TAKES THE DROP. The S2 section above left a
// cube standing at `left`, so the same pixel that answered "the floor" a moment
// ago answers "that cube" once something is standing on it — the target is the
// surface under the cursor, and forcing pickability did not turn every drop
// into a floor drop.
var onFloor = scene.addPrimitive("cube", { position: right, onSurface: true });
editor.frame(2);
var cubeTarget = editor.dropTargetAt(vp.width * 0.70, vp.height * 0.70);
assert(cubeTarget != null && node.property(cubeTarget.id, "defaultFloor") === false,
    "with a cube under the cursor, the drop targets the CUBE and not the floor ("
    + JSON.stringify(cubeTarget) + ")");
node.remove(onFloor);

// ---- THE RIGHT COLUMN'S TWO TABS (PROPERTY_FILTER_SPEC §2, RIGHT-TABS-1) ----
// The Properties column is World | Selection now, and the verb is the same
// implementation the tab bar and Ctrl+Shift+P drive. The TAB is a view state:
// moving it never moves the selection, and naming the root moves the tab
// without the tree having a row for it any more.
assert(editor.propertiesTab().tab === "world" || editor.propertiesTab().tab === "selection",
    "editor.propertiesTab() reads a tab (" + editor.propertiesTab().tab + ")");
var aCube = scene.addPrimitive("cube", { position: { x: 4, y: 0, z: 4 } });
editor.select(aCube);
editor.frame(1);
assert(editor.propertiesTab().tab === "selection", "a pick raises the Selection tab");
assert(editor.propertiesTab({ tab: "world" }).tab === "world",
    "editor.propertiesTab({tab:'world'}) raises the World tab");
assert(editor.selection() === aCube,
    "…and moving the TAB did not move the SELECTION");
editor.select(scene.root());
editor.frame(1);
assert(editor.propertiesTab().tab === "world",
    "editor.select(scene.root()) raises the World tab");
assert(editor.selection() === scene.root(),
    "…and the root really is the selection (the verbs are unchanged)");
editor.select(aCube);
editor.frame(1);
assert(editor.propertiesTab().tab === "selection", "a pick raises Selection again");
editor.select(null);
editor.frame(1);
assert(editor.propertiesTab().tab === "selection",
    "a DESELECT keeps the tab the user is on (D3)");
var badTabRefused = false;
try { editor.propertiesTab({ tab: "nonsense" }); } catch (e) { badTabRefused = true; }
assert(badTabRefused, "an unknown tab name is refused");
var badKeyRefused = false;
try { editor.propertiesTab({ nope: 1 }); } catch (e) { badKeyRefused = true; }
assert(badKeyRefused, "an unknown key is refused");
node.remove(aCube);

// ---- WHAT THE PROPERTIES COLUMN COSTS (ADD-1) -------------------------------
// `scene.addPrimitive` SELECTS the node it makes, and the right column used to
// rebuild itself for it: 44 ms of a scripted add's 50, for an object the user
// never asked to look at. A selection raises a mount DEBT now, settled once at
// the end of the event-loop turn, and a mesh pick between two objects of the
// same shape REFILLS the rows that are already there instead of destroying
// them. `editor.propertiesStats()` is how that is pinnable from a script.
var statsBefore = editor.propertiesStats();
assert(typeof statsBefore.mounts === "number" && typeof statsBefore.refills === "number"
    && typeof statsBefore.rebuilds === "number" && typeof statsBefore.rows === "number",
    "editor.propertiesStats() reports mounts/refills/rebuilds/rows");
var burst = [];
for (var bi = 0; bi < 24; bi++)
    burst.push(scene.addPrimitive("cube", { position: { x: -20 - bi, y: 0, z: -20 } }));
var statsAfterBurst = editor.propertiesStats();
// The script engine runs on its own thread, so the UI event loop turns between
// verbs and each turn settles at most ONE mount however many selections it
// carried - which is the law: 24 adds are at most 24 mounts and never 24
// rebuilds of the blade column (the refills below say the rest).
assert(statsAfterBurst.mounts - statsBefore.mounts <= 24,
    "24 adds mount the column at most once per event-loop turn ("
    + (statsAfterBurst.mounts - statsBefore.mounts) + ")");
// ASKING always gets a true answer, whether or not a mount is owed.
var rows = editor.properties({ tab: "selection" });
var statsSettled = editor.propertiesStats();
assert(rows.length > 0 && statsSettled.pending === false,
    "asking what the column holds settles any owed mount first (" + rows.length + " rows)");
// THE PICK: same-shape meshes reuse their rows.
var pickA = burst[0], pickB = burst[1];
editor.select(pickA); editor.properties({ tab: "selection" });
var pickBase = editor.propertiesStats();
for (var pi = 0; pi < 10; pi++) {
    editor.select(pi % 2 ? pickB : pickA);
    editor.properties({ tab: "selection" });
}
var pickAfter = editor.propertiesStats();
assert(pickAfter.mounts - pickBase.mounts === 10,
    "ten picks are ten mounts (" + (pickAfter.mounts - pickBase.mounts) + ")");
assert(pickAfter.refills - pickBase.refills === 10 && pickAfter.rebuilds === pickBase.rebuilds,
    "...and every one of them REFILLED the material rows rather than rebuilding them ("
    + (pickAfter.refills - pickBase.refills) + " refills, "
    + (pickAfter.rebuilds - pickBase.rebuilds) + " rebuilds)");
// ---- WHAT A SELECTION COSTS, PER CONSUMER (SELECT-COST-1) -------------------
// `vr.select()` measured 16-17 ms per call, independent of scene size, and a
// desktop click paid exactly the same: at 90 Hz a controller trigger press cost
// MORE THAN A FRAME. A selection fans out to four consumers (the viewport's
// outline and gizmo, the Properties column, the outliner's row, the timeline's
// subject) and raises a fifth cost, the column's deferred mount — and this is
// the only place all five are measured together, because only the app has them.
editor.select(pickA);
editor.properties({ tab: "selection" });        // settle anything owed
// The reset zeroes AFTER building the answer (a bracket reads what it is
// closing), so the proof is the NEXT read.
editor.selectionCost({ reset: true });
var costBase = editor.selectionCost();
assert(costBase.selections === 0 && costBase.totalMs === 0,
    "editor.selectionCost({reset:true}) zeroes the counters ("
    + costBase.selections + " selections, " + costBase.totalMs + " ms)");
// pickB first: the selection standing before the loop is pickA, so every one
// of the twenty really moves the primary.
for (var ci = 0; ci < 20; ci++) editor.select(ci % 2 ? pickA : pickB);
editor.properties({ tab: "selection" });        // the last mount lands
var cost = editor.selectionCost();
assert(cost.selections === 20 && cost.primaryChanges === 20,
    "twenty picks are twenty selections, every one of them a real change ("
    + cost.selections + "/" + cost.primaryChanges + ")");
assert(typeof cost.viewport.ms === "number" && typeof cost.properties.ms === "number"
    && typeof cost.hierarchy.ms === "number" && typeof cost.timeline.ms === "number"
    && typeof cost.setViewport.ms === "number" && typeof cost.setHierarchy.ms === "number"
    && typeof cost.mount.ms === "number",
    "editor.selectionCost() reports every consumer separately");
// THE SET FAN-OUT RUNS ON EVERY SINGLE PICK — a replace-select raises both of
// the service's signals — so its two consumers are charged as many times as
// the primary's, and a total that omitted them would not be the whole cost.
assert(cost.setViewport.calls === cost.selections
    && cost.setHierarchy.calls === cost.selections,
    "the SET fan-out is charged on every pick too (" + cost.setViewport.calls + "/"
    + cost.setHierarchy.calls + " of " + cost.selections + ")");
assert(cost.totalMs >= cost.setViewport.ms + cost.setHierarchy.ms,
    "...and totalMs includes them");
// THE BOUND IS A FRAME: 1000/90 = 11.11 ms, so 11.0. Measured on the
// development box at 1k and 10k nodes, all six consumers plus the mount:
// 0.61-0.76 ms per selection with the docks open (it was 6.96-7.52 before this
// lane, of which 5.2 was re-showing blades that had not changed and 1.7 three
// synchronous timeline repaints; the two SET consumers, untouched by that
// work, are 0.011 ms of the total).
assert(cost.perSelectionMs < 11.0,
    "a selection change fits inside a 90 Hz frame (" + cost.perSelectionMs.toFixed(2) + " ms)");
// ...and the column re-points the blades it already has rather than showing
// them again: the structural half of the same claim, machine-independent.
assert(editor.propertiesStats().attached === 0,
    "a pick between two meshes attaches NO blades ("
    + editor.propertiesStats().attached + ")");
// RE-SELECTING THE SAME NODE is not a change, and the counter says so.
editor.selectionCost({ reset: true });
for (var si = 0; si < 5; si++) editor.select(pickA);
var same = editor.selectionCost();
// The standing selection here is pickA (the loop ended on it), so the FIRST of
// these five is not a change either.
assert(same.selections === 5 && same.primaryChanges === 0,
    "re-selecting the same node is not a primary change (" + same.primaryChanges
    + " of " + same.selections + ")");
var costKeyRefused = false;
try { editor.selectionCost({ nope: 1 }); } catch (e) { costKeyRefused = true; }
assert(costKeyRefused, "editor.selectionCost refuses an unknown key");

for (var ri = 0; ri < burst.length; ri++) node.remove(burst[ri]);

console.log("editor_controls: fly speed, post-fx params, screenshot grades, the "
          + "new-scene defaults, the drop point, the drop TARGET, the "
          + "properties tabs and the column mount/refill counters and the "
          + "per-consumer selection cost verified");
