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

// ---- snap size (phase C): editor-global, persisted, drives the grid ----
assert(near(editor.snapSize(), 1.0, 1e-4), "snap size defaults to 1.0");
assert(editor.setSnapSize(0.5), "setSnapSize(0.5)");
assert(near(editor.snapSize(), 0.5, 1e-4), "snap size reads back 0.5");
var badSnapRefused = false;
try { editor.setSnapSize(0); } catch (e) { badSnapRefused = true; }
assert(badSnapRefused, "setSnapSize(0) refused");
assert(near(editor.snapSize(), 0.5, 1e-4), "refused set left the value alone");
assert(editor.setSnapSize(1.0), "snap size restored to 1.0");

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

// Restore the defaults for anything running after this script.
assert(editor.setOverlays({ grid: true, lightWires: true, selectionWireframe: false }),
    "overlays restored");

console.log("editor_controls: overlays verified");
