// capture.helpers_switch — SCENE ONLY BY DEFAULT, THE FURNITURE ON THE SWITCH
// (VIDEO-REC-1; owner §10.5).
//
// Three short recordings of one still pose, each one's LAST frame turned back into
// display codes (capture.lastFrame):
//   A  the grid on, a cube selected (gizmo + outline), the light's icon — helpers OFF;
//   B  Game View (the mirror pushes no furniture at all) — helpers ON;
//   C  the same furniture as A — helpers ON.
// A == B (within the frames' own noise) says the switch's OFF hides every piece of the
// editor's furniture; A != C says ON draws it. The comparison is in display codes at a
// small tolerance, because each recording is a new view with young temporal histories of
// its own (the warm-up settles them; it does not make two views bit-identical).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

assert(project.create("capture helpers " + Date.now()) !== false, "project.create");
var cube = scene.addPrimitive("cube", { position: [0, 0.5, 0] });
editor.setCamera({ position: { x: 3, y: 2.5, z: 4 }, lookAt: { x: 0, y: 0.3, z: 0 } });
assert(editor.setOverlays({ grid: true, gameView: false }) === true, "the grid on");
assert(editor.select(cube) === true, "the cube selected: the gizmo and the outline are up");
editor.frame(4, 1 / 60);

var dir = app.dataRoot().root + "/capture-helpers";
function record(name, helpers) {
    var st = capture.start({ path: dir + "/" + name + ".mp4", helpers: helpers });
    assert(st !== null && st.helpers === helpers, name + ": recording, helpers " + helpers + " " + app.lastError());
    var guard = 0;
    while (capture.status().armed < 24 && guard++ < 200) editor.frame(1, 1 / 60);
    capture.stop({ wait: true, timeoutMs: 30000 });
    var done = capture.status();
    assert(done.state === "done", name + ": finished " + (done.error || ""));
    var png = dir + "/" + name + ".png";
    assert(capture.lastFrame(png) === true, name + ": its last frame");
    return png;
}

var a = record("a_furniture_off", false);
var c = record("c_furniture_on", true);
assert(editor.setOverlays({ gameView: true }) === true, "Game View: no furniture pushed at all");
editor.frame(2, 1 / 60);
var b = record("b_none_on", true);
editor.setOverlays({ gameView: false });

var ab = app.compareImages(a, b, 6);
var ac = app.compareImages(a, c, 6);
console.log("A vs B (off vs none): " + JSON.stringify(ab));
console.log("A vs C (off vs on):   " + JSON.stringify(ac));
assert(ab.overFraction < 0.002, "helpers OFF is the furniture-free picture (" + ab.over + " px over 6 codes)");
assert(ac.overFraction > 0.01, "helpers ON draws the furniture (" + ac.over + " px over 6 codes)");
console.log("capture.helpers_switch: PASS");
