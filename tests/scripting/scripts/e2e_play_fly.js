// scripting.e2e.play_fly — PLAY-FLY-1: THE PLAY-IN-PLACE FLY MOVES THE CAMERA.
//
// The owner's report: in play-in-place the fly keys appear dead. A static read
// found the key path intact to the controller (EngineSceneViewport::event claims
// the key for the run -> keyPressEvent -> PlayBack::keyPressEvent -> the
// KeyboardState the fly reads), with two suspects it could not settle: the
// camera latch in PlayBack::setController and the host's camera hand-off after
// the fly. This is the repro: enter play, HOLD a bound fly key through the
// viewport's own key path (editor.key), step 30 frames and read the camera the
// run renders through (the editor camera — no scene camera is active here).
// It must move every frame, along the key's axis, and stop when released.
// Frames, never time (the engine's clock is fixed).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function sub(a, b) { return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z }; }
function dot(a, b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
function len(a) { return Math.sqrt(dot(a, a)); }
// q * v * q^-1 for a unit quaternion {x, y, z, scalar}
function rotate(q, v) {
    var w = q.scalar, x = q.x, y = q.y, z = q.z;
    var tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
    return { x: v.x + w * tx + (y * tz - z * ty),
             y: v.y + w * ty + (z * tx - x * tz),
             z: v.z + w * tz + (x * ty - y * tx) };
}

project.create("Play fly " + Date.now());
assert(!scene.activeCamera(), "no scene camera is active: play renders the free viewer (" + J(scene.activeCamera()) + ")");
editor.frame(10);
// WHILE EDITING, W is the translate tool's SHORTCUT: a real press fires it and
// never reaches the viewport, so the verb refuses rather than take a path no
// user can (SMALL-FIXES-1 F5).
var refusal = "";
try { editor.key("W"); } catch (e) { refusal = e.message; }
assert(/shortcut tool\.translate/.test(refusal), "editing: editor.key('W') is refused — " + refusal);
assert(editor.play() === true, "editor.play()");
assert(editor.playing() === true, "the viewport is playing");
assert(editor.playInputOwner() === "editor" || editor.playInputOwner() === "controller",
       "input owner reads " + editor.playInputOwner());
editor.frame(10);

function flyArm(keyName, axis, label) {
    var cam0 = editor.camera();
    var start = cam0.position;
    var dir = rotate(cam0.rotation, axis);
    console.log(label + ": start " + J(start) + " axis " + J(dir));
    assert(editor.key(keyName, "press") === true, "editor.key('" + keyName + "', 'press')");
    var along = [], off = [];
    for (var f = 0; f < 30; ++f) {
        editor.frame(1, 1 / 60);   // the fixed step: frames, not the wall clock
        var d = sub(editor.camera().position, start);
        along.push(dot(d, dir));
        off.push(len(sub(d, { x: dir.x * along[f], y: dir.y * along[f], z: dir.z * along[f] })));
    }
    assert(editor.key(keyName, "release") === true, "editor.key('" + keyName + "', 'release')");
    console.log(label + ": along " + J(along.map(function (v) { return +v.toFixed(4); })));
    var monotonic = true;
    for (var i = 1; i < along.length; ++i) if (!(along[i] > along[i - 1])) monotonic = false;
    assert(along[0] > 0 && monotonic,
           label + ": the camera moves along the key's axis on EVERY one of 30 frames ("
           + along[0].toFixed(4) + " -> " + along[along.length - 1].toFixed(4) + " m)");
    var worstOff = Math.max.apply(null, off);
    assert(worstOff < 0.05 * along[along.length - 1] + 1e-4,
           label + ": …and only along it (worst sideways " + worstOff.toFixed(5) + " m)");
    var stopped = editor.camera().position;
    editor.frame(5, 1 / 60);
    assert(len(sub(editor.camera().position, stopped)) < 1e-4,
           label + ": released, the camera stays where it is");
}

// W flies FORWARD (the camera's -Z); D strafes RIGHT (+X).
flyArm("W", { x: 0, y: 0, z: -1 }, "W (forward)");
flyArm("D", { x: 1, y: 0, z: 0 }, "D (strafe right)");

assert(editor.stop() === true, "editor.stop()");
console.log("play_fly: all checks passed");
