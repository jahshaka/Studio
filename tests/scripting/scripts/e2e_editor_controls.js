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
assert(ov0.grid === true && ov0.lightWires === true, "grid and light wires are on by default");
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
assert(byName.Move.keys.join(",") === "W,S,A,D", "Move defaults to W/S/A/D");
assert(byName.Jump.keys.join(",") === "Space", "Jump defaults to Space (the owner's ask)");
assert(byName.Sprint.keys.join(",") === "Shift", "Sprint defaults to Shift");
assert(byName.Move.display === "W / S / A / D", "the Preferences display text is generated");

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
assert(input.bindings()[0].keys.join(",") === "W,S,A,D", "Move is back to W/S/A/D");
assert(input.bindings()[2].keys.join(",") === "Space", "Jump is back to Space");

console.log("editor_controls: gameplay input verbs verified");
