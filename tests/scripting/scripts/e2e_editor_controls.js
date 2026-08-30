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

console.log("editor_controls: verbs verified");
