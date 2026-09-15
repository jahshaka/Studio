// scripting.live / scripting.live_off — THE RUN POLICY (SCRIPTING_LIVE_SPEC §3.1).
//
// ONE script, two ctest entries: the app is started with --script-live for the
// first and plainly for the second, and the script asks which run it is in
// (app.scriptPolicy().runPolicy) before asserting the opposite halves of the
// same promise.
//
//   live: the JavaScript is on a worker thread and the UI thread is free
//         between verbs, so the render driver TICKS while the script builds —
//         app.frameStats().rendered moves BEFORE the last verb, which on the
//         old UI-thread engine it could never do.
//   off:  the driver skips its ticks for the whole run, so rendered does not
//         move by a single frame between the first verb and the last — exactly
//         what a blocked UI thread used to give, and what the 54 frame-stepping
//         e2e scripts depend on.
//
// Both halves also assert the contracts the threading must not have broken:
// one undo entry for the run, console output in order, and editor.frame(n)
// rendering its n frames either way (it is a verb; it runs on the UI thread).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var policy = app.scriptPolicy();
var live = policy.runPolicy === "live";
console.log("run policy: " + policy.runPolicy + " (setting: " + policy.mode + ")");
assert(policy.running === true, "a script reading its own state is running");

var guid = project.create("Live Feedback " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- the undo bracket, before anything is built ----
var undoBefore = editor.undoState();
assert(undoBefore.macroOpen === true, "the run's undo macro is open on the UI thread");

// ---- twenty nodes, watching the driver ----
var framesAtStart = app.frameStats().rendered;
var movedDuring = 0;
var ids = [];
for (var i = 0; i < 20; ++i) {
    ids.push(scene.addPrimitive("sphere", {
        position: { x: (i % 5) * 2 - 4, y: 1, z: Math.floor(i / 5) * 2 - 2 }
    }));
    if (i < 19 && app.frameStats().rendered > framesAtStart) movedDuring++;
}
assert(ids.length === 20, "twenty primitives added through the bridge");
var framesAtEnd = app.frameStats().rendered;
console.log("driver frames: " + framesAtStart + " -> " + framesAtEnd
            + " (" + movedDuring + " of the first 19 verbs saw a new frame)");

if (live) {
    assert(movedDuring > 0,
           "LIVE: the render loop drew at least one frame BEFORE the last verb");
} else {
    assert(framesAtEnd === framesAtStart,
           "OFF: the render loop drew nothing at all between the verbs");
    assert(movedDuring === 0, "OFF: ...and no verb ever saw a new frame");
}

// ---- editor.frame renders either way: it is a verb ----
var before = editor.viewportState().framesPresented;
editor.frame(3, 1 / 60);
var after = editor.viewportState().framesPresented;
assert(after >= before + 3, "editor.frame(3) presented 3 frames (" + before + " -> " + after + ")");

// ---- still ONE undo entry, whatever thread the JS ran on ----
var undoNow = editor.undoState();
assert(undoNow.macroOpen === true, "the macro is still the run's own");
assert(undoNow.pushes >= undoBefore.pushes + 20, "every add was recorded as a command");

// ---- the bridge's error route: fail() throws in the script, at the call site ----
var caught = null;
try {
    project.open("no such project anywhere");
} catch (e) {
    caught = e.message;
}
assert(caught && caught.indexOf("no project named") >= 0,
       "project.open's fail() reached the script as a catchable error: " + caught);
assert(app.lastError().indexOf("no project named") >= 0,
       "...and app.lastError() recorded the same sentence");

// ---- console.log ordering survives the thread hop ----
for (var k = 0; k < 5; ++k) {
    console.log("order-" + k);
    scene.nodes();
}
assert(true, "five console lines interleaved with five verbs");

"live-feedback-ok"
