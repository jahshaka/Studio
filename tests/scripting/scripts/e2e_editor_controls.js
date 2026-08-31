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
